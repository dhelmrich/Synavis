#include "SynavisStreamer.h"

#include "SynavisCommunicationInterface.h"
#include "SynavisVp9SendoffHandler.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "RendererInterface.h"
#include "RenderUtils.h"
#include "Engine/Texture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Misc/ScopeLock.h"
#include <memory>
#include <string>
#include <span>
#include "Async/Async.h"
#include "SynavisStreamerRendering.h"
#include "Misc/Char.h"
#include "Containers/StringConv.h"
#include "Logging/LogVerbosity.h"

THIRD_PARTY_INCLUDES_START
#include "rtc/rtc.h"


extern "C" {



#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libavutil/buffer.h>
}

static USynavisStreamer* GGlobalStreamer = nullptr;
static FCriticalSection GGlobalStreamerMutex;

// Sequence counter for tracking rtcAddTrackEx vs rtcSetLocalDescription order
static std::atomic<int32> GRtcSequenceCounter{0};

static FString GetDataChannelLabelSafe(int dc)
{
  char buf[256];
  int r = rtcGetDataChannelLabel(dc, buf, sizeof(buf));
  if (r > 0 && buf[0] != '\0') return FString(UTF8_TO_TCHAR(buf));
  return FString(TEXT("<no-label>"));
}

void USynavisStreamer::TeardownConnection(int32 PlayerID)
{
  // Grab a shared pointer to ensure the connection object remains alive for
  // the duration of teardown operations even if we remove it from the map.
  TSharedPtr<FSynavisConnection> ConnShared;
    TSharedPtr<FSynavisConnection>* p = Connections.Find(PlayerID);
    if (!p || !(*p))
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: Teardown requested for unknown connection %d"), PlayerID);
      return;
    }
    ConnShared = *p;

  // Local raw pointer for convenience; ConnShared keeps the object alive
  FSynavisConnection* Conn = ConnShared.Get();

  // Close peerconnection
  if (Conn->PeerConnection)
  {
    // Close and delete the C API peer connection id
    rtcClosePeerConnection(Conn->PeerConnection);
    rtcDeletePeerConnection(Conn->PeerConnection);
    // Close and delete the C API peer connection id
    rtcClosePeerConnection(Conn->PeerConnection);
    rtcDeletePeerConnection(Conn->PeerConnection);
    Conn->PeerConnection = 0;
  }
  if (Conn->DataChannel)
  {
    rtcClose(Conn->DataChannel);
    rtcDeleteDataChannel(Conn->DataChannel);
    Conn->DataChannel = 0;
  }

  // Close any per-handler datachannels and remove global mappings
  for (auto& kv : Conn->HandlersByChannel)
  {
    int dc = kv.Key;
    if (dc)
    {
      rtcClose(dc);
      rtcDeleteDataChannel(dc);
    }
  }

  // Delete any created tracks for this connection
  for (auto& kv : Conn->TracksByHandler)
  {
    int tr = kv.Value;
    if (tr)
    {
      rtcDeleteTrack(tr);
    }
  }

  // Finally remove from map; ConnShared keeps the object alive until function exit.
  Connections.Remove(PlayerID);
  UE_LOG(LogTemp, Log, TEXT("Synavis: Teardown complete for connection %d"), PlayerID);

  // Close control/data channels
  if (Conn->DataChannel)
  {
    rtcClose(Conn->DataChannel);
    rtcDeleteDataChannel(Conn->DataChannel);
    Conn->DataChannel = 0;
  }

  // Close any per-handler datachannels and remove global mappings
  for (auto& kv : Conn->HandlersByChannel)
  {
    int dc = kv.Key;
    if (dc)
    {
      rtcClose(dc);
      rtcDeleteDataChannel(dc);
    }
  }

  // Delete any created tracks for this connection
  for (auto& kv : Conn->TracksByHandler)
  {
    int tr = kv.Value;
    if (tr)
    {
      rtcDeleteTrack(tr);
    }
  }


  // Finally remove from map; ConnShared keeps the object alive until function exit.
  Connections.Remove(PlayerID);
  UE_LOG(LogTemp, Log, TEXT("Synavis: Teardown complete for connection %d"), PlayerID);
}



// Use TArray<uint8> for binary payloads instead of rtc::binary to avoid C++ ABI crossing

THIRD_PARTY_INCLUDES_END

// Forward declarations for helper functions defined later in this file but used earlier.
static uint32 CreatePawnHandle();
static uint32 CreateConnectionHandle();
static FString LogSetup(uint32 ID, USceneComponent* Child);

// DataChannelCtx is declared in the public header and owned by
// USynavisStreamer::DataChannelContexts. See SynavisStreamer.h for layout.


// C-style logger callback for libdatachannel. Matches rtcLogCallbackFunc = void(*)(rtcLogLevel,const char*)
static void Synavis_Rtc_Logger(rtcLogLevel level, const char* message)
{
  switch (level)
  {
  case RTC_LOG_ERROR:
    UE_LOG(LogTemp, Error, TEXT("Synavis LibDataChannel: %s"), ANSI_TO_TCHAR(message));
    break;
  case RTC_LOG_WARNING:
    UE_LOG(LogTemp, Warning, TEXT("Synavis LibDataChannel: %s"), ANSI_TO_TCHAR(message));
    break;
  case RTC_LOG_INFO:
    UE_LOG(LogTemp, Log, TEXT("Synavis LibDataChannel: %s"), ANSI_TO_TCHAR(message));
    break;
  case RTC_LOG_DEBUG:
  case RTC_LOG_VERBOSE:
  default:
    UE_LOG(LogTemp, Verbose, TEXT("Synavis LibDataChannel: %s"), ANSI_TO_TCHAR(message));
    break;
  }
}

extern "C" {
  static void av_log_callback(void *ptr, int level, const char *fmt, va_list vargs)
  {
    if (level > av_log_get_level()) return;  // Respect global level

    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt, vargs);
    buf[sizeof(buf)-1] = 0;  // Force null-terminate [web:14]

    // Use FString for safe UE conversion (UTF8_TO_TCHAR can fail on malformed UTF-8)
    FString Msg = FString(UTF8_TO_TCHAR(buf));

    // Map AV_LOG_* to UE_LOG levels (ensure LogTemp is visible/compiled)
    switch (level) {
    case AV_LOG_PANIC:
    case AV_LOG_FATAL:
    case AV_LOG_ERROR:
      UE_LOG(LogTemp, Error, TEXT("LIBAV[%d]: %s"), level, *Msg);
      break;
    case AV_LOG_WARNING:
      UE_LOG(LogTemp, Warning, TEXT("LIBAV[%d]: %s"), level, *Msg);
      break;
    case AV_LOG_INFO:
      UE_LOG(LogTemp, Log, TEXT("LIBAV[%d]: %s"), level, *Msg);
      break;
    default:  // DEBUG, VERBOSE
      UE_LOG(LogTemp, Verbose, TEXT("LIBAV[%d]: %s"), level, *Msg);
      break;
    }
  }
}



// Static hex-dump logger. Logs the byte values of the provided buffer as hex
// for debugging encoding/round-trip issues. Keep this function static to
// limit linkage to this TU.
static void LogHex(const char* Data, size_t Len, const TCHAR* Prefix)
{
  if (!Data || Len == 0)
  {
    UE_LOG(LogTemp, Log, TEXT("%s: <empty>"), Prefix);
    return;
  }
  FString Hex;
  Hex.Reserve(static_cast<int32>(Len * 3));
  for (size_t i = 0; i < Len; ++i)
  {
    Hex += FString::Printf(TEXT("%02X "), static_cast<uint8>(Data[i]));
  }
  UE_LOG(LogTemp, Log, TEXT("%s: %s"), Prefix, *Hex);
}

// Verbose variant of hex dump so we can hide it under Verbose logging level
static void LogHexVerbose(const char* Data, size_t Len, const TCHAR* Prefix)
{
  if (!Data || Len == 0)
  {
    UE_LOG(LogTemp, Verbose, TEXT("%s: <empty>"), Prefix);
    return;
  }
  FString Hex;
  Hex.Reserve(static_cast<int32>(Len * 3));
  for (size_t i = 0; i < Len; ++i)
  {
    Hex += FString::Printf(TEXT("%02X "), static_cast<uint8>(Data[i]));
  }
  UE_LOG(LogTemp, Verbose, TEXT("%s: %s"), Prefix, *Hex);
}

// Helper to munge incoming answer SDP for UE libdatachannel C API acceptance.
// Fixes: m=video 9 → m=video 0 (rejected tracks), BUNDLE mids → dynamic from actual tracks,
// remove a=group:LS, ensure CRLF. Call before rtcSetRemoteDescription(pc, munged.c_str(), "answer").
// NOTE: For offers, the BUNDLE group is constructed dynamically based on registered tracks.
static FString MungSDPForLibdatachannel(const FString& RawSdp, FSynavisConnection* Conn = nullptr)
{
  FString S = RawSdp.Replace(TEXT("\n"), TEXT("\r\n"));
  TArray<FString> Lines;
  S.ParseIntoArrayLines(Lines, true);

  // Build dynamic BUNDLE mids if connection is provided
  FString DynamicBundleMids = TEXT("0");
  if (Conn)
  {
    TArray<FString> BundleMidsArray;
    BundleMidsArray.Add(TEXT("0")); // Datachannel always first
    
    // Collect actual track mids from registered tracks
    for (const auto& kv : Conn->TracksByHandler)
    {
      int32 TrackId = kv.Value;
      if (TrackId > 0)
      {
        // Construct mid from track id: track-<connid>-<trackid>
        FString Mid = FString::Printf(TEXT("track-%d-%d"), Conn->ConnectionID, TrackId);
        BundleMidsArray.Add(Mid);
      }
    }
    
    // Join with spaces
    DynamicBundleMids = FString::Join(BundleMidsArray, TEXT(" "));
    UE_LOG(LogTemp, Verbose, TEXT("Synavis: Dynamic BUNDLE mids for conn %d: %s"), Conn->ConnectionID, *DynamicBundleMids);
  }

  for (int i = 0; i < Lines.Num(); ++i)
  {
    FString& Line = Lines[i];
    // Fix ONLY port: m=video 09 → m=video 9
    if (Line.Left(10) == TEXT("m=video 09")) Line = TEXT("m=video 9") + Line.Mid(10);
    // BUNDLE → dynamic track mids based on actual registered tracks
    if (Line.Contains(TEXT("a=group:BUNDLE ")) && !Line.Contains(TEXT("track-")))
    {
      if (!Conn)
      {
        // Fallback to hardcoded if no connection provided (legacy behavior)
        Line = TEXT("a=group:BUNDLE 0 track-1-101 track-2-101");
      }
      else
      {
        Line = TEXT("a=group:BUNDLE ") + DynamicBundleMids;
      }
    }
  }
  Lines.RemoveAll([](const FString& L){ return L.StartsWith(TEXT("a=group:LS")); });
  FString Fixed;
  for (const FString& L : Lines) Fixed += L + TEXT("\r\n");
  Fixed.TrimEndInline();
  return Fixed;
}

// helper for low-level pointer validation
bool is_canonical_x64(const void* ptr) {
    uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
    return ((address >> 47) & 0xFFFFULL) == 0 || ((address >> 47) & 0xFFFFULL) == 0xFFFFULL;
}



// C callbacks used with the libdatachannel C API (rtc/rtc.h). These callbacks are plain C-style
// functions which receive the websocket id and the user pointer we attached with
// rtcSetUserPointer. We forward the events to the UE member handlers on the game thread.
extern "C" {
  void Synavis_Rtc_OnOpen(int id, void* user_ptr)
  {
    USynavisStreamer* self = nullptr;
    {
      FScopeLock lock(&GGlobalStreamerMutex);
      self = GGlobalStreamer;
    }
    if (!self) return;
    AsyncTask(ENamedThreads::GameThread, []() {
      USynavisStreamer* selfLocal = nullptr;
      {
        FScopeLock lock(&GGlobalStreamerMutex);
        selfLocal = GGlobalStreamer;
      }
      if (selfLocal) selfLocal->HandleSignallingOpen();
    });
  }

  void Synavis_Rtc_OnClosed(int id, void* user_ptr)
  {
    USynavisStreamer* self = nullptr;
    {
      FScopeLock lock(&GGlobalStreamerMutex);
      self = GGlobalStreamer;
    }
    if (!self) return;
    AsyncTask(ENamedThreads::GameThread, []() {
      USynavisStreamer* selfLocal = nullptr;
      {
        FScopeLock lock(&GGlobalStreamerMutex);
        selfLocal = GGlobalStreamer;
      }
      if (selfLocal) selfLocal->HandleSignallingClose();
    });
  }

  void Synavis_Rtc_OnError(int id, const char* err, void* user_ptr)
  {
    USynavisStreamer* self = nullptr;
    {
      FScopeLock lock(&GGlobalStreamerMutex);
      self = GGlobalStreamer;
    }
    if (!self) return;
    std::string s = err ? std::string(err) : std::string();
    AsyncTask(ENamedThreads::GameThread, [s]() {
      USynavisStreamer* selfLocal = nullptr;
      {
        FScopeLock lock(&GGlobalStreamerMutex);
        selfLocal = GGlobalStreamer;
      }
      if (selfLocal) selfLocal->HandleSignallingError(s);
    });
  }

  void Synavis_Rtc_OnMessage(int id, const char* data, int size, void* user_ptr)
  {
    USynavisStreamer* self = nullptr;
    {
      FScopeLock lock(&GGlobalStreamerMutex);
      self = GGlobalStreamer;
    }
    if (!self) return;
    // size < 0 indicates a null-terminated text message according to the C API conventions
    if (size < 0)
    {
      std::string s = data ? std::string(data) : std::string();
      AsyncTask(ENamedThreads::GameThread, [s]() {
        USynavisStreamer* selfLocal = nullptr;
        {
          FScopeLock lock(&GGlobalStreamerMutex);
          selfLocal = GGlobalStreamer;
        }
        if (selfLocal) selfLocal->HandleSignallingMessage(std::variant<TArray<uint8>, std::string>(s));
      });
    }
    else
    {
      TArray<uint8> b;
      if (size > 0 && data)
      {
        b.AddUninitialized(size);
        memcpy(b.GetData(), data, static_cast<size_t>(size));
      }
      AsyncTask(ENamedThreads::GameThread, [b]() mutable {
        USynavisStreamer* selfLocal = nullptr;
        {
          FScopeLock lock(&GGlobalStreamerMutex);
          selfLocal = GGlobalStreamer;
        }
        if (selfLocal) selfLocal->HandleSignallingMessage(std::variant<TArray<uint8>, std::string>(b));
      });
    }
  }

}

// PeerConnection and DataChannel callbacks
// File-local helpers to resolve captured per-datachannel info on the game thread.
struct CapturedDCInfo
{
  FSynavisConnection* RawConn = nullptr;
  int32 ConnectionID = 0;
  uint32 HandlerID = 0;
};

static FCriticalSection G_CapturedDcMutex;
static TMap<int32, CapturedDCInfo> G_CapturedDcMap;

// Resolved helper functions removed; C callbacks now forward directly to member handlers.

  // Forward-declare DataChannel callbacks so they can be referenced by PC callbacks below
void Synavis_Rtc_DataChannel_OnMessage(int id, const char* data, int size, void* user_ptr);
void Synavis_Rtc_DataChannel_OnOpen(int id, void* user_ptr);
void Synavis_Rtc_DataChannel_OnClosed(int id, void* user_ptr);
void Synavis_Rtc_DataChannel_OnError(int id, const char* error, void* user_ptr);
  // Forward-declare Track callbacks (tracks use same open/closed/error signatures)
  void Synavis_Rtc_Track_OnOpen(int id, void* user_ptr);
  void Synavis_Rtc_Track_OnClosed(int id, void* user_ptr);
  void Synavis_Rtc_Track_OnError(int id, const char* error, void* user_ptr);

static FORCEINLINE bool __isValidContext(DataChannelCtx* ctx, int id)
{
  // Check for null context
  if (!ctx) return false;
  // Check for null streamer
  if (!ctx->Streamer) return false;
  // Check for obviously invalid raw pointer (not nullptr, not a low/bad address)
  if (!ctx->ConnRaw || reinterpret_cast<uintptr_t>(ctx->ConnRaw) < 0x10000)
    return false;
  return true;
}

// Enhanced validation with per-tier logging
static FORCEINLINE bool __isValidContextWithDiagnostics(
    DataChannelCtx* ctx,
    int dcId,
    bool bLogVerbose = true)
{
  static const uint64_t CANARY_MAGIC = 0xDEADBEEFCAFEBABEULL;

  if (!ctx) {
    if (bLogVerbose) UE_LOG(LogTemp, Warning, TEXT("DC %d: ctx is null"), dcId);
    return false;
  }
  // Validate canaries first to detect cross-thread memory corruption
  if (ctx->CANARY_FRONT != CANARY_MAGIC || ctx->CANARY_BACK != CANARY_MAGIC) {
    if (bLogVerbose) {
      UE_LOG(LogTemp, Error, TEXT("DC %d ctx=%p: CANARY mismatch front=0x%016llx back=0x%016llx (expected 0x%016llx)"),
        dcId, ctx, (unsigned long long)ctx->CANARY_FRONT, (unsigned long long)ctx->CANARY_BACK, (unsigned long long)CANARY_MAGIC);
    }
    return false;
  }
  if (!ctx->Streamer) {
    if (bLogVerbose) UE_LOG(LogTemp, Warning, TEXT("DC %d ctx=%p: Streamer is null"), dcId, ctx);
    return false;
  }
  uintptr_t rawPtr = reinterpret_cast<uintptr_t>(ctx->ConnRaw);
  if (rawPtr == 0 || rawPtr < 0x10000) {
    if (bLogVerbose) {
      UE_LOG(LogTemp, Warning, TEXT("DC %d ctx=%p: ConnRaw invalid (ptr=%p). ConnID=%d will trigger numeric fallback."), dcId, ctx, ctx->ConnRaw, ctx->ConnectionID);
    }
    return false;
  }
  return true;
}

void Synavis_Rtc_OnPcLocalDescription(int pc, const char* sdp, const char* type, void* user_ptr)
{
  USynavisStreamer* self = nullptr;
  {
    FScopeLock lock(&GGlobalStreamerMutex);
    self = GGlobalStreamer;
  }
  if (!self) return;
  std::string s = sdp ? std::string(sdp) : std::string();
  std::string t = type ? std::string(type) : std::string();
  //AsyncTask(ENamedThreads::GameThread, [self, pc, s, t]() {
  self->HandlePcLocalDescriptionCallback(pc, s.c_str(), t.c_str());
  //});
}

void Synavis_Rtc_OnPcLocalCandidate(int pc, const char* cand, const char* mid, void* user_ptr)
{
  USynavisStreamer* self = nullptr;
  {
    FScopeLock lock(&GGlobalStreamerMutex);
    self = GGlobalStreamer;
  }
  if (!self) return;
  //AsyncTask(ENamedThreads::GameThread, [self, pc, cand = cand ? std::string(cand) : std::string(), mid = mid ? std::string(mid) : std::string()]() {
  self->AddIcetoPc(pc, cand);
  //});
}

void Synavis_Rtc_OnPcGatheringStateChange(int pc, rtcGatheringState state, void* user_ptr)
{
  USynavisStreamer* self = nullptr;
  {
    FScopeLock lock(&GGlobalStreamerMutex);
    self = GGlobalStreamer;
  }
  if (!self) return;
  int istate = static_cast<int>(state);
  AsyncTask(ENamedThreads::GameThread, [self, pc, istate]() {
    self->HandlePcGatheringStateChangeCallback(pc, istate);
  });
}

void Synavis_Rtc_OnPcSignalingStateChange(int pc, rtcSignalingState state, void* user_ptr)
{
  USynavisStreamer* self = nullptr;
  {
    FScopeLock lock(&GGlobalStreamerMutex);
    self = GGlobalStreamer;
  }
  if (!self) return;
  int istate = static_cast<int>(state);
  AsyncTask(ENamedThreads::GameThread, [self, pc, istate]() {
    self->HandlePcSignalingStateChangeCallback(pc, istate);
  });
}

void Synavis_Rtc_OnPcDataChannel(int pc, int dc, void* user_ptr)
{
  USynavisStreamer* self = reinterpret_cast<USynavisStreamer*>(user_ptr);
  if (!self) return;
  // Configure per-datachannel callbacks and forward open/message events
  // Ensure a per-datachannel context exists. rtcGetUserPointer may return
  // a previously-set context (for locally-created channels). Only allocate
  // when none exists.
  void* existing = rtcGetUserPointer(dc);
  if (!existing)
  {
    // Create a shared context and attach its raw pointer to the datachannel.
    TSharedPtr<DataChannelCtx> ctx = MakeShared<DataChannelCtx>();
    ctx->Streamer = self;
    ctx->ConnectionID = 0;
    ctx->HandlerID = 0;
    ctx->PeerPC = pc;
    // Attach the per-datachannel context pointer for the C API callbacks
    // so callbacks can quickly find the authoritative context.
    rtcSetUserPointer(dc, ctx.Get());
      UE_LOG(LogTemp, Warning, TEXT("PcDataChannel: rtcSetUserPointer dc=%d newUser=%p ctx=%p streamer=%p"), dc, rtcGetUserPointer(dc), ctx.Get(), self);
      // Log incoming datachannel label for diagnostics (uses C API)
      {
        char buf[256] = {0};
        int got = rtcGetDataChannelLabel(dc, buf, static_cast<int>(sizeof(buf)));
        if (got > 0 && buf[0]) {
          UE_LOG(LogTemp, Verbose, TEXT("Synavis: Incoming DC %d label: %s"), dc, ANSI_TO_TCHAR(buf));
        } else {
          UE_LOG(LogTemp, Verbose, TEXT("Synavis: Incoming DC %d label: <null>"), dc);
        }
      }
      existing = ctx.Get();
      // Defer adding ownership to the streamer's central container onto the game thread
      AsyncTask(ENamedThreads::GameThread, [self, dc, ctx]() {
        if (self) {
          self->AddDataChannelContext(dc, ctx);
        }
      });
  }
  rtcSetMessageCallback(dc, Synavis_Rtc_DataChannel_OnMessage);
  rtcSetOpenCallback(dc, Synavis_Rtc_DataChannel_OnOpen);
  rtcSetClosedCallback(dc, Synavis_Rtc_DataChannel_OnClosed);
  rtcSetErrorCallback(dc, Synavis_Rtc_DataChannel_OnError);
  // Attempt to associate incoming datachannel with a registered handler by inspecting its label.
  AsyncTask(ENamedThreads::GameThread, [self, pc, dc]() {
    // Forward to member handler which is allowed to access protected members
    UE_LOG(LogTemp, Log, TEXT("Synavis: DataChannel %d label='%s' created for PC %d (forwarding to member handler)"), dc, *GetDataChannelLabelSafe(dc), pc);
    self->HandlePcDataChannelCreated(pc, dc);
  });
}

// DataChannel callbacks used above
void Synavis_Rtc_DataChannel_OnOpen(int id, void* user_ptr)
{
  // Forward the event to the single instance streamer and dispatch on game thread.
  USynavisStreamer* streamer = nullptr;
  {
    FScopeLock lock(&GGlobalStreamerMutex);
    streamer = GGlobalStreamer;
  }
  if (!streamer) return;
  UE_LOG(LogTemp, Warning, TEXT("DC_OnOpen cthread dc=%d label='%s' userPtr=%p"), id, *GetDataChannelLabelSafe(id), rtcGetUserPointer(id));
  AsyncTask(ENamedThreads::GameThread, [id]() {
    USynavisStreamer* streamerLocal = nullptr;
    {
      FScopeLock lock(&GGlobalStreamerMutex);
      streamerLocal = GGlobalStreamer;
    }
    if (!streamerLocal) return;
    TSharedPtr<DataChannelCtx> ctx = streamerLocal->GetDataChannelContext(id);
    DataChannelCtx* p = ctx.Get();
    UE_LOG(LogTemp, Warning, TEXT("DC_OnOpen game dc=%d label='%s' ctx=%p conn=%d handler=%u userPtr=%p"), id, *GetDataChannelLabelSafe(id), p, p ? p->ConnectionID : 0, p ? p->HandlerID : 0, rtcGetUserPointer(id));
    streamerLocal->HandleDataChannelOpenCallback(id);
  });
}

void Synavis_Rtc_DataChannel_OnClosed(int id, void* user_ptr)
{
  USynavisStreamer* streamer = nullptr;
  {
    FScopeLock lock(&GGlobalStreamerMutex);
    streamer = GGlobalStreamer;
  }
  if (!streamer) return;
  UE_LOG(LogTemp, Warning, TEXT("DC_OnClosed cthread dc=%d userPtr=%p"), id, rtcGetUserPointer(id));
  AsyncTask(ENamedThreads::GameThread, [id]() {
    USynavisStreamer* streamerLocal = nullptr;
    {
      FScopeLock lock(&GGlobalStreamerMutex);
      streamerLocal = GGlobalStreamer;
    }
    if (!streamerLocal && is_canonical_x64(streamerLocal))
      return;
    // Defensive: avoid dereferencing potentially-invalid pointers returned
    // from the C callbacks. Prefer using rtcGetUserPointer() first and
    // validate any raw context with diagnostics before touching fields.
    DataChannelCtx* p = nullptr;
    void* uptr = rtcGetUserPointer(id);
    if (uptr && is_canonical_x64(uptr))
    {
      p = reinterpret_cast<DataChannelCtx*>(uptr);
      if (!__isValidContextWithDiagnostics(p, id, true))
      {
        // If the raw user-pointer looks invalid, try central map as fallback
        TSharedPtr<DataChannelCtx> ctxFallback;
        // Only call GetDataChannelContext if streamerLocal still looks sane
        if (streamerLocal)
          ctxFallback = streamerLocal->GetDataChannelContext(id);
        if (ctxFallback.IsValid())
          p = ctxFallback.Get();
        else
          p = nullptr;
      }
    }
    else
    {
      // No user pointer: try central map
      TSharedPtr<DataChannelCtx> ctxFallback = streamerLocal->GetDataChannelContext(id);
      if (ctxFallback.IsValid()) p = ctxFallback.Get();
    }

    // Log safely without dereferencing invalid pointers
    UE_LOG(LogTemp, Warning, TEXT("DC_OnClosed game dc=%d ctx=%p conn=%d handler=%u userPtr=%p"),
      id, p, p ? p->ConnectionID : 0, p ? p->HandlerID : 0, uptr);

    streamerLocal->HandleDataChannelClosedCallback(id);
  });
}

void Synavis_Rtc_DataChannel_OnError(int id, const char* error, void* user_ptr)
{
  USynavisStreamer* self = reinterpret_cast<USynavisStreamer*>(user_ptr);
  UE_LOG(LogTemp, Warning, TEXT("DC_OnError cthread dc=%d userPtr=%p err=%s"), id, rtcGetUserPointer(id), error ? ANSI_TO_TCHAR(error) : TEXT("<null}"));
  if (!self) {
    // schedule fallback/diagnostic on game thread
    AsyncTask(ENamedThreads::GameThread, [self, id]() {
      if (self) self->HandleDataChannelClosedCallback(id);
    });
    return;
  }

  TSharedPtr<DataChannelCtx> ctxPtr = self->GetDataChannelContext(id);
  DataChannelCtx* ctx = ctxPtr.Get();
  if (!__isValidContextWithDiagnostics(ctx, id, true)) {
    // Best-effort: schedule a game-thread diagnostic log to try numeric fallback
    AsyncTask(ENamedThreads::GameThread, [self, id]() {
      FSynavisConnection* C = nullptr;
      // final fallback on game thread will inspect rtcGetUserPointer if needed
      if (self) self->HandleDataChannelClosedCallback(id);
      if (C) {
        UE_LOG(LogTemp, Warning, TEXT("DC %d: Error callback numeric fallback found conn %d"), id, C->ConnectionID);
      } else {
        UE_LOG(LogTemp, Warning, TEXT("DC %d: Error callback numeric fallback failed"), id);
      }
    });
    return;
  }

  USynavisStreamer* selfLocal = ctx->Streamer;
  std::string s = error ? std::string(error) : std::string();
  // Capture weak + ids for diagnostics if needed on game thread
  FSynavisConnection* CapturedConn = ctx->ConnRaw;
  int32 SavedConnId = ctx->ConnectionID;
  uint32 SavedHandlerId = ctx->HandlerID;
  AsyncTask(ENamedThreads::GameThread, [selfLocal, id, s, CapturedConn, SavedConnId, SavedHandlerId]() {
    (void)CapturedConn; (void)SavedConnId; (void)SavedHandlerId;
    UE_LOG(LogTemp, Warning, TEXT("Synavis: DataChannel %d error (game): %s ctx=%p conn=%d handler=%u userPtr=%p"), id, ANSI_TO_TCHAR(s.c_str()), (void*)CapturedConn, SavedConnId, SavedHandlerId, rtcGetUserPointer(id));
    UE_LOG(LogTemp, Error, TEXT("Synavis: DataChannel %d error: %s"), id, ANSI_TO_TCHAR(s.c_str()));
  });
}

// Track-level callbacks ----------------------------------------------------
void Synavis_Rtc_Track_OnOpen(int id, void* user_ptr)
{
  // user_ptr expected to be FSynavisConnection* when set from Synavis_Rtc_OnPcTrack
  FSynavisConnection* Conn = reinterpret_cast<FSynavisConnection*>(user_ptr);
  // Forward to game thread for safe UE logging/state access
  AsyncTask(ENamedThreads::GameThread, [id, Conn]() {
    int maxMsg = rtcMaxMessageSize(id);
    int buffered = rtcGetBufferedAmount(id);
    bool open = rtcIsOpen(id);
    if (Conn)
    {
      Conn->MaxMessageSize = static_cast<uint32>(maxMsg);
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: Track_OnOpen id=%d conn=%d open=%d maxMsg=%d buffered=%d"), id, Conn->ConnectionID, open ? 1 : 0, maxMsg, buffered);
      
      // Set the FTransportResource to Open
      auto TrackIT = Conn->TracksByHandler.FilterByPredicate([id](auto a){
        return a.ID == id;
      }).begin();

      if(TrackIT)
      {
        // push the track to state==open
        (*TrackIT).Value.state = ETransportState::OPEN;
      }
      else
      {
        UE_LOG(LogTemp, Error, TEXT("Synavis: Unrecoverable error, did not find track %d reference in connection %d"), id, Conn->ConnectionID);
      }
    }
    else
    {
      char descBuf[1024] = {0};
      int got = rtcGetTrackDescription(id, descBuf, static_cast<int>(sizeof(descBuf)));
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: Track_OnOpen id=%d open=%d maxMsg=%d buffered=%d desc_len=%d (no conn pointer)"), id, open ? 1 : 0, maxMsg, buffered, got);
      if (got > 0) UE_LOG(LogTemp, Verbose, TEXT("Synavis: Track_OnOpen description: %s"), ANSI_TO_TCHAR(descBuf));
    }
  });
}

void Synavis_Rtc_Track_OnClosed(int id, void* user_ptr)
{
  FSynavisConnection* Conn = reinterpret_cast<FSynavisConnection*>(user_ptr);
  AsyncTask(ENamedThreads::GameThread, [id, Conn]() {
    if (Conn)
    {
      UE_LOG(LogTemp, Log, TEXT("Synavis: Track_OnClosed id=%d conn=%d"), id, Conn->ConnectionID);
    }
    else
    {
      UE_LOG(LogTemp, Log, TEXT("Synavis: Track_OnClosed id=%d (no conn pointer)"), id);
    }
  });
}

void Synavis_Rtc_Track_OnError(int id, const char* error, void* user_ptr)
{
  FSynavisConnection* Conn = reinterpret_cast<FSynavisConnection*>(user_ptr);
  std::string s = error ? std::string(error) : std::string();
  AsyncTask(ENamedThreads::GameThread, [id, s, Conn]() {
    if (Conn)
    {
      UE_LOG(LogTemp, Error, TEXT("Synavis: Track_OnError id=%d conn=%d err=%s"), id, Conn->ConnectionID, ANSI_TO_TCHAR(s.c_str()));
    }
    else
    {
      UE_LOG(LogTemp, Error, TEXT("Synavis: Track_OnError id=%d err=%s (no conn pointer)"), id, ANSI_TO_TCHAR(s.c_str()));
    }
  });
}

void Synavis_Rtc_DataChannel_OnMessage(int id, const char* data, int size, void* user_ptr)
{
  USynavisStreamer* streamer = nullptr;
  {
    FScopeLock lock(&GGlobalStreamerMutex);
    streamer = GGlobalStreamer;
  }
  if (!streamer)
  {
    UE_LOG(LogTemp, Warning, TEXT("DC_OnMessage cthread dc=%d label='%s' no global streamer userPtr=%p size=%d"), id, *GetDataChannelLabelSafe(id), rtcGetUserPointer(id), size);
    return;
  }

  UE_LOG(LogTemp, Warning, TEXT("DC_OnMessage cthread dc=%d label='%s' userPtr=%p size=%d"), id, *GetDataChannelLabelSafe(id), rtcGetUserPointer(id), size);
  if (size < 0)
  {
    std::string s = data ? std::string(data) : std::string();
    AsyncTask(ENamedThreads::GameThread, [streamer, id, s]() {
      TSharedPtr<DataChannelCtx> ctx = streamer->GetDataChannelContext(id);
      DataChannelCtx* p = ctx.Get();
      UE_LOG(LogTemp, Warning, TEXT("DC_OnMessage game dc=%d label='%s' ctx=%p conn=%d handler=%u userPtr=%p len=%d isText=1"), id, *GetDataChannelLabelSafe(id), p, p ? p->ConnectionID : 0, p ? p->HandlerID : 0, rtcGetUserPointer(id), (int)s.size());
      streamer->ResolveAndHandleDataChannelMessage(id, std::variant<TArray<uint8>, std::string>(s));
    });
  }
  else
  {
    // Heuristic: treat as textual JSON if the first/last non-space characters
    // are matching braces/brackets. Otherwise treat as binary.
    bool looksLikeJson = false;
    if (data && size > 0)
    {
      const char* begin = data;
      const char* end = data + size - 1;
      // skip leading whitespace
      while (begin <= end && (*begin == ' ' || *begin == '\t' || *begin == '\r' || *begin == '\n')) ++begin;
      // skip trailing whitespace
      while (end >= begin && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) --end;
      if (begin <= end)
      {
        if ((*begin == '{' && *end == '}') || (*begin == '[' && *end == ']')) looksLikeJson = true;
      }
    }

    if (looksLikeJson)
    {
      std::string s = data ? std::string(data, static_cast<size_t>(size)) : std::string();
      AsyncTask(ENamedThreads::GameThread, [streamer, id, s]() {
        TSharedPtr<DataChannelCtx> ctx = streamer->GetDataChannelContext(id);
        DataChannelCtx* p = ctx.Get();
        UE_LOG(LogTemp, Warning, TEXT("DC_OnMessage game dc=%d label='%s' ctx=%p conn=%d handler=%u userPtr=%p len=%d isText=1"), id, *GetDataChannelLabelSafe(id), p, p ? p->ConnectionID : 0, p ? p->HandlerID : 0, rtcGetUserPointer(id), (int)s.size());
        streamer->ResolveAndHandleDataChannelMessage(id, std::variant<TArray<uint8>, std::string>(s));
      });
    }
    else
    {
      TArray<uint8> b;
      if (size > 0 && data)
      {
        b.AddUninitialized(size);
        memcpy(b.GetData(), data, static_cast<size_t>(size));
      }
      AsyncTask(ENamedThreads::GameThread, [streamer, id, b]() mutable {
        TSharedPtr<DataChannelCtx> ctx = streamer->GetDataChannelContext(id);
        DataChannelCtx* p = ctx.Get();
        UE_LOG(LogTemp, Warning, TEXT("DC_OnMessage game dc=%d label='%s' ctx=%p conn=%d handler=%u userPtr=%p len=%d isText=0"), id, *GetDataChannelLabelSafe(id), p, p ? p->ConnectionID : 0, p ? p->HandlerID : 0, rtcGetUserPointer(id), b.Num());
        streamer->ResolveAndHandleDataChannelMessage(id, std::variant<TArray<uint8>, std::string>(b));
      });
    }
  }
}

void USynavisStreamer::ResolveAndHandleDataChannelMessage(int dc, const std::variant<TArray<uint8>, std::string>& message)
{
  // Quick path: if we have a central DataChannelCtx, use it to dispatch immediately
  {
    TSharedPtr<DataChannelCtx> ctxptr = GetDataChannelContext(dc);
    if (ctxptr.IsValid()) {
      DataChannelCtx* ctx = ctxptr.Get();
      if (ctx && ctx->Streamer == this && ctx->ConnectionID != 0) {
        CapturedDCInfo foundInfo{ctx->ConnRaw, ctx->ConnectionID, ctx->HandlerID};
        {
          FScopeLock lock(&G_CapturedDcMutex);
          G_CapturedDcMap.Add(dc, foundInfo);
        }
        // Dispatch to member handler which will consult G_CapturedDcMap
        HandleDataChannelMessageCallback(dc, message);
        FScopeLock lock2(&G_CapturedDcMutex);
        G_CapturedDcMap.Remove(dc);
        return;
      }
    }
  }

  // On the game thread: attempt linear search of connections to find owning connection/handler
  CapturedDCInfo foundInfo{};
  for (auto& Pair : Connections)
  {
    TSharedPtr<FSynavisConnection> Conn = Pair.Value;
    if (!Conn) continue;
    auto it = Conn->HandlersByChannel.Find(dc);
    if (it)
    {
      foundInfo.RawConn = Conn.Get();
      foundInfo.ConnectionID = Conn->ConnectionID;
      foundInfo.HandlerID = *it;
      break;
    }
    if (Conn->DataChannel == dc)
    {
      foundInfo.RawConn = Conn.Get();
      foundInfo.ConnectionID = Conn->ConnectionID;
      foundInfo.HandlerID = 0;
      break;
    }
  }

  if (foundInfo.RawConn || foundInfo.ConnectionID != 0)
  {
    FScopeLock lock(&G_CapturedDcMutex);
    G_CapturedDcMap.Add(dc, foundInfo);
  }

  // Dispatch to the existing member handler which will consult G_CapturedDcMap
  HandleDataChannelMessageCallback(dc, message);

  if (foundInfo.RawConn || foundInfo.ConnectionID != 0)
  {
    FScopeLock lock(&G_CapturedDcMutex);
    G_CapturedDcMap.Remove(dc);
  }
}

// Additional PeerConnection callbacks: track/state/ice state notifications
void Synavis_Rtc_OnPcTrack(int pc, int tr, void* user_ptr)
{
  USynavisStreamer* self = reinterpret_cast<USynavisStreamer*>(user_ptr);
  if (!self) return;
  UE_LOG(LogTemp, Warning, TEXT("Synavis: Dropping incoming track %d for PC %d (we generally do not expect incoming tracks)"), tr, pc);
}

void Synavis_Rtc_OnPcStateChange(int pc, rtcState state, void* user_ptr)
{
  USynavisStreamer* self = reinterpret_cast<USynavisStreamer*>(user_ptr);
  if (!self) return;
  AsyncTask(ENamedThreads::GameThread, [self, pc, state]() {
    UE_LOG(LogTemp, Verbose, TEXT("Synavis: PC %d state change %d"), pc, static_cast<int>(state));
    });
}

void Synavis_Rtc_OnPcIceStateChange(int pc, rtcIceState state, void* user_ptr)
{
  USynavisStreamer* self = reinterpret_cast<USynavisStreamer*>(user_ptr);
  if (!self) return;
  AsyncTask(ENamedThreads::GameThread, [self, pc, state]() {
    UE_LOG(LogTemp, Verbose, TEXT("Synavis: PC %d ice state change %d"), pc, static_cast<int>(state));
    });
}

static const TMap<FString, TArray<TTuple<FString, FString, uint32>>> DataConnectionHeaderMap
{
    {TEXT("control"), { {TEXT("ID"), TEXT("int"), 1} }},
    {TEXT("video"),   {
      {TEXT("Width"), TEXT("int"), 2},
    {TEXT("Height"), TEXT("int"), 2},
    }},
  {TEXT("geometry"), {
    {TEXT("VertexCount"), TEXT("int"), 4},
    {TEXT("TriangleCount"), TEXT("int"), 4},
    {TEXT("HasNTCVMap"), TEXT("uint"), 1}, /*Has Normal, Tangent, Cotangent, Vertex Colour: bool packed NNTTCCVV as uint8*/
  }},
  {TEXT("INCamera"), {
        {TEXT("parameter"), TEXT("type"), 4 /*size of parameter in bytes*/},
    }},
  {TEXT("messagetemplate"), {
        {TEXT("parameter"), TEXT("type"), 4 /*size of parameter in bytes*/},
    }},
    // Additional command types mirrored from DataConnector
    {TEXT("buffer"), {
          {TEXT("start"), TEXT("string"), 0},
          {TEXT("stop"), TEXT("string"), 0},
          {TEXT("size"), TEXT("int"), 4},
          {TEXT("format"), TEXT("string"), 0},
      }},
    {TEXT("directbase64"), {
          {TEXT("points"), TEXT("string"), 0},
          {TEXT("triangles"), TEXT("string"), 0},
      }},
    {TEXT("message"), {
          {TEXT("text"), TEXT("string"), 0},
      }},
    {TEXT("candidate"), {
          {TEXT("candidate"), TEXT("string"), 0},
          {TEXT("sdpMLineIndex"), TEXT("int"), 4},
          {TEXT("sdpMid"), TEXT("string"), 0},
      }},
    {TEXT("iceCandidate"), {
          {TEXT("candidate"), TEXT("string"), 0},
          {TEXT("sdpMLineIndex"), TEXT("int"), 4},
          {TEXT("sdpMid"), TEXT("string"), 0},
      }},
};


// Sets default values for this component's properties
USynavisStreamer::USynavisStreamer()
{
  // Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
  // off to improve performance if you don't need them.
  PrimaryComponentTick.bCanEverTick = true;
  this->WebSocketUri.reserve(100);
  
  // Register this instance as the global streamer for C callbacks (single-instance policy)
  {
    FScopeLock lock(&GGlobalStreamerMutex);
    GGlobalStreamer = this;
  }
  DataChannelContexts = MakeUnique<TMap<int32, TSharedPtr<DataChannelCtx>>>();
}

uint32_t USynavisStreamer::GetNextSSRC()
{
  // Use the per-instance atomic counter declared in the header (seeded at 1001)
  return static_cast<uint32_t>(NextSSRC.fetch_add(1));
}

USynavisStreamer::~USynavisStreamer()
{
  // Clear global streamer pointer under lock so C callbacks don't race
  // against a partially-destroyed object (prevents use-after-free).
  {
    FScopeLock lock(&GGlobalStreamerMutex);
    if (GGlobalStreamer == this) GGlobalStreamer = nullptr;
  }
  if (SignallingId != 0)
  {
    // send close first
    rtcClose(SignallingId);

    rtcDeleteWebSocket(SignallingId);
    SignallingId = 0;
  }


  // Clean up any remaining connections (ensure libdatachannel objects are closed and heap objects freed)
  if (Connections.Num() > 0)
  {
    TArray<int32> Keys;
    Keys.Reserve(Connections.Num());
    for (const auto& Pair : Connections) Keys.Add(Pair.Key);
    for (int32 K : Keys) { TeardownConnection(K); }
  }
}

// Called when the game starts
void USynavisStreamer::BeginPlay()
{
  Super::BeginPlay();

  // set the logging level for libdatachannel to verbose only when UE global verbosity is VeryVerbose
  if (UE_GET_LOG_VERBOSITY(LogTemp) >= ELogVerbosity::VeryVerbose)
  {
    rtcInitLogger(RTC_LOG_VERBOSE, Synavis_Rtc_Logger);
  }

  // Force DEBUG level so libav/avcodec emits useful diagnostic messages during development.
  // Adjust this if the log stream becomes too noisy in production.
  av_log_set_level(AV_LOG_DEBUG);
  // Include the AV severity label in codec messages and skip repeated messages
  // to make logs easier to parse in the UE log stream.
  av_log_set_flags(AV_LOG_PRINT_LEVEL | AV_LOG_SKIP_REPEATED);
  av_log_set_callback(av_log_callback);
  UE_LOG(LogTemp, Log, TEXT("Synavis: Installed libav log callback with av_log_level=Debug"));


  // sanity check: fire function
  Synavis_Rtc_Logger(RTC_LOG_INFO, "SynavisStreamer Synavis_Rtc_Logger initialized");
  av_log(nullptr, AV_LOG_INFO, "SynavisStreamer av_log_callback initialized with level Debug");


  // Non-blocking sendoff handler: offloads readback locking, encoding and packetization
  if (!SendoffHandler)
  {
    SendoffHandler = NewObject<USynavisVp9SendoffHandler>(this);
    {
      int EffectiveMtu = (this->Mtu > 0) ? this->Mtu : 1280;
      int Overhead = 12 + 8 + 40; // RTP + UDP + IP/SRTP estimate
      int MaxPayload = EffectiveMtu - Overhead;
      if (MaxPayload < 200) MaxPayload = 200;
      if (MaxPayload > 1400) MaxPayload = 1400;
      uint16_t PayloadSize = static_cast<uint16_t>(MaxPayload);
      SendoffHandler->Initialize(this->VideoPayloadType, GetNextSSRC(), PayloadSize);
      // Ensure streamer LibAVState points to the handler-owned encoder state
      LibAVState = SendoffHandler->GetOrCreateLibAVEncoderState();
      UE_LOG(LogTemp, Warning, TEXT("Synavis: Linked LibAVState to SendoffHandler %p -> LibAVState=%p"), SendoffHandler, LibAVState);
    }
  }

}


void USynavisStreamer::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
  // If no connection currently requests streaming, early-out. Streaming is
  // managed per-connection via FSynavisConnection::bStreaming.
  bool anyStreaming = false;
  for (const auto& Pair : Connections) { TSharedPtr<FSynavisConnection> C = Pair.Value; if (C && C->bStreaming) { anyStreaming = true; break; } }
  if (!anyStreaming)
    return;

  // Capture frames for all registered handlers for this tick. This will enqueue
  // I420 GPU readbacks (FRHIGPUTextureReadback) for each active handler/source.
  CaptureFrame();

  // Block-and-poll pattern: process pending readbacks until they have been
  // encoded & sent or a per-tick timeout expires. This ensures capture->readback->encode->send
  // completes before the tick continues (as requested). Note: this will block the game thread
  // for up to MaxTickWait seconds if the GPU readback is not ready; choose the timeout
  // according to responsiveness needs.
  const double MaxTickWait = 0.5; // seconds maximum to wait inside a single tick
  const double NowStart = FPlatformTime::Seconds();
  const double Deadline = NowStart + MaxTickWait;
  const double LocalReadbackTimeout = 0.5; // per-readback age timeout

  if (LibAVState)
  {
    // Keep looping until all pending readbacks are handled or we hit the deadline
    while (FPlatformTime::Seconds() < Deadline)
    {
      bool DidWorkThisIteration = false;

      for (int32 i = PendingReadbacks.Num() - 1; i >= 0; --i)
      {
        FPendingI420Readback& rec = PendingReadbacks[i];

        // Validate readbacks
        if (!rec.ReadbackY || !rec.ReadbackU || !rec.ReadbackV)
        {
          PendingReadbacks.RemoveAtSwap(i);
          DidWorkThisIteration = true;
          continue;
        }

        // If readbacks ready, perform zero-copy encode and send to all target tracks
        if (rec.ReadbackY->IsReady() && rec.ReadbackU->IsReady() && rec.ReadbackV->IsReady())
        {
          UE_LOG(LogTemp, Verbose, TEXT("Synavis: Pending readback ready, starting zero-copy encode (Width=%d Height=%d)"), rec.Width, rec.Height);
          // temporarily report on the size of the GPU buffers for diagnostics (note: these may be larger than the actual encoded frame size due to padding/row pitch)
          UE_LOG(LogTemp, Verbose, TEXT("Synavis: Readback buffer sizes Y=%d U=%d V=%d"), rec.ReadbackY->GetGPUSizeBytes(), rec.ReadbackU->GetGPUSizeBytes(), rec.ReadbackV->GetGPUSizeBytes());
          // Enqueue non-blocking sendoff via the centralized sendoff handler (takes ownership)
          if (SendoffHandler)
          {
            SendoffHandler->EnqueueReadbackNonBlocking(rec.ReadbackY, rec.ReadbackU, rec.ReadbackV, rec.Width, rec.Height, rec.TargetTracks, SendoffHandler->GetOrCreateLibAVEncoderState());
          }
          // EnqueueReadbackNonBlocking takes ownership of the readbacks via AVBuffer free callbacks,
          // so do not unlock/delete them here - remove entry from queue.
          PendingReadbacks.RemoveAtSwap(i);
          DidWorkThisIteration = true;
          continue;
        }

        // If timeout expired: clean up and drop the readback (unlock & delete on render thread)
        if (FPlatformTime::Seconds() - rec.EnqueuedAt > LocalReadbackTimeout)
        {
          UE_LOG(LogTemp, Warning, TEXT("Synavis: Pending readback timed out after %.3fs, cleaning up"), FPlatformTime::Seconds() - rec.EnqueuedAt);
          FRHIGPUTextureReadback* Yrb = rec.ReadbackY;
          FRHIGPUTextureReadback* Urb = rec.ReadbackU;
          FRHIGPUTextureReadback* Vrb = rec.ReadbackV;
          ENQUEUE_RENDER_COMMAND(Synavis_CleanupReadback)([Yrb, Urb, Vrb](FRHICommandListImmediate& RHICmdList)
            {
              if (Yrb) { Yrb->Unlock(); delete Yrb; }
              if (Urb) { Urb->Unlock(); delete Urb; }
              if (Vrb) { Vrb->Unlock(); delete Vrb; }
            });
          PendingReadbacks.RemoveAtSwap(i);
          DidWorkThisIteration = true;
        }
      }

      for (auto i = PendingRGBReadbacks.Num() - 1; i >= 0; --i)
      {
        // check if the TFuture is ready
        FPendingRGBReadback& rec = PendingRGBReadbacks[i];
        if (rec.ReadbackFuture.IsReady())
        {
          UE_LOG(LogTemp, Verbose, TEXT("Synavis: Pending RGB readback ready, processing result (Width=%d Height=%d)"), rec.Width, rec.Height);
          // Process the RGB readback result (e.g., encode and send)
          TArray<FColor> PixelData = rec.ReadbackFuture.Get();
          // hand off to SynavisVP9SendoffHandler for encoding/sending
          this->SendoffHandler->EnqueueSoftwareNonBlocking(PixelData, rec.Width, rec.Height, rec.TargetTracks, this->SendoffHandler->GetOrCreateLibAVEncoderState());
        }
      }

      // If no pending work and queue empty, we're done for this tick
      if (PendingReadbacks.Num() == 0)
      {
        {
          FScopeLock lock(&GGlobalStreamerMutex);
          if (GGlobalStreamer == this) GGlobalStreamer = nullptr;
        }
        break;
      }
    }
  }
}



// Public helpers for external callbacks to manage the central DataChannelCtx map
void USynavisStreamer::AddDataChannelContext(int32 DcId, TSharedPtr<DataChannelCtx> Ctx)
{
  if (!Ctx) return;
  if (DataChannelContexts) {
    FScopeLock lock(&DataChannelContextsMutex);
    DataChannelContexts->Add(DcId, Ctx);
    UE_LOG(LogTemp, Warning, TEXT("AddDC dc=%d ctx=%p conn=%d handler=%d canF=0x%016llx canB=0x%016llx userPtr=%p"),
      DcId, Ctx.Get(), Ctx->ConnectionID, Ctx->HandlerID,
      (unsigned long long)Ctx->CANARY_FRONT, (unsigned long long)Ctx->CANARY_BACK,
      rtcGetUserPointer(DcId));
  }
}

void USynavisStreamer::RemoveDataChannelContext(int32 DcId)
{
  if (DataChannelContexts)
  {
    FScopeLock lock(&DataChannelContextsMutex);
    if (DataChannelContexts->Contains(DcId))
    {
      DataChannelContexts->Remove(DcId);
      UE_LOG(LogTemp, Warning, TEXT("RemoveDC dc=%d removed from central map remaining=%d userPtr=%p"),
        DcId, DataChannelContexts->Num(), rtcGetUserPointer(DcId));
    }
  }
}

FSynavisConnection* USynavisStreamer::GetConnectionFromPC(int pc)
{
  for (auto& Pair : Connections)
  {
    TSharedPtr<FSynavisConnection> Ptr = Pair.Value;
    if (Ptr && Ptr->PeerConnection == pc) return Ptr.Get();
  }
  return nullptr;
}

const FSynavisConnection* USynavisStreamer::GetConnectionFromPC(int pc) const
{
  for (const auto& Pair : Connections)
  {
    TSharedPtr<FSynavisConnection> Ptr = Pair.Value;
    if (Ptr && Ptr->PeerConnection == pc) return Ptr.Get();
  }
  return nullptr;
}

FSynavisHandler* USynavisStreamer::GetHandlerById(uint32 HandlerId)
{
  for (FSynavisHandler& H : RegisteredDataHandlers)
  {
    if (H.HandlerID == HandlerId) return &H;
  }
  return nullptr;
}

const FSynavisHandler* USynavisStreamer::GetHandlerById(uint32 HandlerId) const
{
  for (const FSynavisHandler& H : RegisteredDataHandlers)
  {
    if (H.HandlerID == HandlerId) return &H;
  }
  return nullptr;
}

FSynavisHandler* USynavisStreamer::FirstWithoutVideoTrack(FSynavisConnection* Conn)
{
  if (!Conn) return nullptr;
  for (FSynavisHandler& H : RegisteredDataHandlers)
  {
    if (!H.Video) continue;
    if (!Conn->TracksByHandler.Contains(H.HandlerID))
      return &H;
  }
  return nullptr;
}

const FSynavisHandler* USynavisStreamer::FirstWithoutVideoTrack(const FSynavisConnection* Conn) const
{
  if (!Conn) return nullptr;
  for (const FSynavisHandler& H : RegisteredDataHandlers)
  {
    if (!H.Video) continue;
    if (!Conn->TracksByHandler.Contains(H.HandlerID))
      return &H;
  }
  return nullptr;
}

void USynavisStreamer::RTCReport() const
{
  int32 NumHandlers = RegisteredDataHandlers.Num();
  int32 NumConns = Connections.Num();
  int32 NumDcCtx = (DataChannelContexts ? DataChannelContexts->Num() : 0);
  UE_LOG(LogTemp, Log, TEXT("Synavis: RTCReport: handlers=%d connections=%d datachannelctx=%d"), NumHandlers, NumConns, NumDcCtx);

  for (const FSynavisHandler& H : RegisteredDataHandlers)
  {
    UE_LOG(LogTemp, Log, TEXT("  Handler id=%u video=%s wantsDedicated=%d acceptsInbound=%d"), H.HandlerID, (H.Video ? TEXT("yes") : TEXT("no")), H.WantsDedicatedChannel ? 1 : 0, H.AcceptsInboundMessages ? 1 : 0);
  }

  for (const auto& Pair : Connections)
  {
    TSharedPtr<FSynavisConnection> C = Pair.Value;
    if (!C) continue;
    int32 Tracks = static_cast<int32>(C->TracksByHandler.Num());
    int32 Channels = static_cast<int32>(C->HandlersByChannel.Num());
    UE_LOG(LogTemp, Log, TEXT("  Conn %d PC=%d DataChannel=%d tracks=%d channels=%d"), C->ConnectionID, C->PeerConnection, C->DataChannel, Tracks, Channels);
    // Detailed libdatachannel diagnostics for this PeerConnection
    if (C->PeerConnection != 0)
    {
      char buf[2048];
      int len = rtcGetLocalDescription(C->PeerConnection, buf, sizeof(buf));
      UE_LOG(LogTemp, Log, TEXT("    PC %d: Local desc len=%d"), C->PeerConnection, len);
      UE_LOG(LogTemp, Verbose, TEXT("    PC %d: Local desc:\n%s"), C->PeerConnection, ANSI_TO_TCHAR(buf));
      len = rtcGetRemoteDescription(C->PeerConnection, buf, sizeof(buf));
      UE_LOG(LogTemp, Log, TEXT("    PC %d: Remote desc len=%d"), C->PeerConnection, len);
      UE_LOG(LogTemp, Verbose, TEXT("    PC %d: Remote desc:\n%s"), C->PeerConnection, ANSI_TO_TCHAR(buf));
      len = rtcGetLocalDescriptionType(C->PeerConnection, buf, sizeof(buf));
      if (len > 0) UE_LOG(LogTemp, Log, TEXT("    PC %d: Local type=%s"), C->PeerConnection, ANSI_TO_TCHAR(buf));
      len = rtcGetRemoteDescriptionType(C->PeerConnection, buf, sizeof(buf));
      if (len > 0) UE_LOG(LogTemp, Log, TEXT("    PC %d: Remote type=%s"), C->PeerConnection, ANSI_TO_TCHAR(buf));
      rtcGetLocalAddress(C->PeerConnection, buf, sizeof(buf));
      UE_LOG(LogTemp, Log, TEXT("    PC %d: Local addr=%s"), C->PeerConnection, ANSI_TO_TCHAR(buf));
      rtcGetRemoteAddress(C->PeerConnection, buf, sizeof(buf));
      UE_LOG(LogTemp, Log, TEXT("    PC %d: Remote addr=%s"), C->PeerConnection, ANSI_TO_TCHAR(buf));
      bool needsNeg = rtcIsNegotiationNeeded(C->PeerConnection);
      UE_LOG(LogTemp, Log, TEXT("    PC %d: Negotiation needed=%d"), C->PeerConnection, needsNeg ? 1 : 0);
      int maxStream = rtcGetMaxDataChannelStream(C->PeerConnection);
      UE_LOG(LogTemp, Log, TEXT("    PC %d: Max data channel stream id=%d"), C->PeerConnection, maxStream);
      int maxMsg = rtcGetRemoteMaxMessageSize(C->PeerConnection);
      UE_LOG(LogTemp, Log, TEXT("    PC %d: Remote max message size=%d"), C->PeerConnection, maxMsg);
    }
    else
    {
      UE_LOG(LogTemp, Warning, TEXT("    PC ID is 0 (not established)"));
    }
    for (const auto& kv : C->TracksByHandler)
    {
      UE_LOG(LogTemp, Log, TEXT("    TracksByHandler: handler=%u -> track=%d"), kv.Key, kv.Value);
      int tr = kv.Value;
      if (tr != 0)
      {
        bool topen = rtcIsOpen(tr);
        UE_LOG(LogTemp, Log, TEXT("      Track %d open=%d"), tr, topen ? 1 : 0);
        char tbuf[2048] = {0};
        for (int i = 0; i < 2048; ++i) tbuf[i] = 0;
        int tlen = rtcGetTrackDescription(tr, tbuf, static_cast<int>(sizeof(tbuf)));
        if (tlen > 0)
        {
          UE_LOG(LogTemp, Verbose, TEXT("        Track %d desc len=%d:\n%s"), tr, tlen, ANSI_TO_TCHAR(tbuf));
        }
        else
        {
          UE_LOG(LogTemp, Verbose, TEXT("        rtcGetTrackDescription returned %d for track %d"), tlen, tr);
        }
      }
    }
    for (const auto& kv : C->HandlersByChannel)
    {
      UE_LOG(LogTemp, Log, TEXT("    HandlersByChannel: dc=%d -> handler=%u"), kv.Key, kv.Value);
      int dc = kv.Key;
      if (dc != 0)
      {
        bool dcopen = rtcIsOpen(dc);
        int buffered = rtcGetBufferedAmount(dc);
        int maxDcMsg = rtcMaxMessageSize(dc);
        UE_LOG(LogTemp, Log, TEXT("      DC %d open=%d buffered=%d max_msg=%d label=%s"), dc, dcopen ? 1 : 0, buffered, maxDcMsg, *GetDataChannelLabelSafe(dc));
      }
    }
  }
}

FString USynavisStreamer::GetNegotiationStateReport() const
{
  FString Report;
  Report += FString::Printf(TEXT("=== Negotiation State Report ===\n"));
  Report += FString::Printf(TEXT("Global Hold: %s\n"), bHoldNegotiation ? TEXT("ACTIVE") : TEXT("INACTIVE"));
  Report += FString::Printf(TEXT("Negotiation Delay: %.2fs\n"), NegotiationDelaySeconds);
  Report += FString::Printf(TEXT("Connections: %d\n\n"), Connections.Num());

  for (const auto& Pair : Connections)
  {
    const TSharedPtr<FSynavisConnection>& C = Pair.Value;
    if (!C) continue;

    Report += FString::Printf(TEXT("Connection %d:\n"), C->ConnectionID);
    Report += FString::Printf(TEXT("  PC ID: %d\n"), C->PeerConnection);
    Report += FString::Printf(TEXT("  State: %s\n"), *UEnum::GetDisplayValueAsText(C->State).ToString());
    Report += FString::Printf(TEXT("  bStreaming: %s\n"), C->bStreaming ? TEXT("YES") : TEXT("NO"));
    Report += FString::Printf(TEXT("  Tracks Registered: %d\n"), static_cast<int32>(C->TracksByHandler.Num()));
    
    bool need = false;
    if (C->PeerConnection != 0)
    {
      need = rtcIsNegotiationNeeded(C->PeerConnection);
    }
    Report += FString::Printf(TEXT("  rtcIsNegotiationNeeded: %s\n\n"), need ? TEXT("YES") : TEXT("NO"));
  }

  return Report;
}

TSharedPtr<DataChannelCtx> USynavisStreamer::GetDataChannelContext(int32 DcId) const
{
  if (!DataChannelContexts) return nullptr;
  FScopeLock lock(&DataChannelContextsMutex);
  TSharedPtr<DataChannelCtx> const* p = DataChannelContexts->Find(DcId);
  if (!p) return nullptr;
  return *p;
}


ESynavisState USynavisStreamer::GetConnectionState() const
{
  // return connection state --> we need to do callback-based updates here
  return this->ConnectionState;
}

int USynavisStreamer::SetupDataChannel(const FSynavisHandler &Handler)
{
  // If handler does not accept inbound messages, skip creating datachannels/reverse mappings
  if (!Handler.AcceptsInboundMessages)
  {
    return -1;
  }
  bool CreatedAny = false;
  // Create a dedicated data channel for the given handler on every active connection.
  for (auto& Pair : Connections)
  {
    TSharedPtr<FSynavisConnection> Conn = Pair.Value;
    if (!Conn) continue;
    if (Conn->PeerConnection == 0) continue;

    std::string channelName = std::string("synavis-handler-") + std::to_string(Handler.HandlerID) + std::string("-") + std::to_string(Conn->ConnectionID);
    int dcid = rtcCreateDataChannel(Conn->PeerConnection, channelName.c_str());
    if (dcid > 0)
    {
      TSharedPtr<DataChannelCtx> ctx = MakeShared<DataChannelCtx>();
      ctx->Streamer = this;
      ctx->ConnectionID = Conn->ConnectionID;
      ctx->HandlerID = Handler.HandlerID;
      ctx->ConnRaw = Conn.Get();
      rtcSetUserPointer(dcid, ctx.Get());
      UE_LOG(LogTemp, Warning, TEXT("SetupDataChannel: rtcSetUserPointer dc=%d newUser=%p ctx=%p conn=%d handler=%u"), dcid, rtcGetUserPointer(dcid), ctx.Get(), Conn->ConnectionID, Handler.HandlerID);
      // Ownership: store in central container (we are on game thread)
      if (DataChannelContexts) DataChannelContexts->Add(dcid, ctx);
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: Allocated DataChannelCtx %p for dc=%d (handler=%u player=%d) [SetupDataChannel]"), ctx.Get(), dcid, Handler.HandlerID, Conn->ConnectionID);
      rtcSetMessageCallback(dcid, Synavis_Rtc_DataChannel_OnMessage);
      rtcSetOpenCallback(dcid, Synavis_Rtc_DataChannel_OnOpen);
      rtcSetClosedCallback(dcid, Synavis_Rtc_DataChannel_OnClosed);
      rtcSetErrorCallback(dcid, Synavis_Rtc_DataChannel_OnError);
      // record mapping so incoming messages can be dispatched to the handler
      Conn->HandlersByChannel[dcid] = Handler.HandlerID;
      CreatedAny = true;
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: Created data channel %d for handler %d on connection %d (PC %d)"), dcid, Handler.HandlerID, Conn->ConnectionID, Conn->PeerConnection);
    }
    else
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: Failed to create data channel for handler %d on connection %d"), Handler.HandlerID, Conn->ConnectionID);
    }
  }


  return CreatedAny ? 0 : -1;
}
bool USynavisStreamer::AnyConnectionStreaming() const
{
  for (const auto& Pair : Connections)
  {
    TSharedPtr<FSynavisConnection> C = Pair.Value;
    if (C && C->bStreaming) return true;
  }
  return false;
}

void USynavisStreamer::StartStreaming()
{
  UE_LOG(LogTemp, Log, TEXT("Synavis: StartStreaming called"));




  for (auto& Pair : Connections)
  {
    TSharedPtr<FSynavisConnection> C = Pair.Value;
    if (C) C->bStreaming = true;
  }
}


void USynavisStreamer::StopStreaming()
{
  // Disable streaming on all connections and tear down encoder state.
  for (auto& Pair : Connections)
  {
    TSharedPtr<FSynavisConnection> C = Pair.Value;
    if (C) C->bStreaming = false;
  }



}

void USynavisStreamer::StopStreamingConnection(int32 ConnectionID)
{
  FSynavisConnection* Conn = FindConnectionByPlayerID(ConnectionID);
  if (!Conn)
  {
    UE_LOG(LogTemp, Warning, TEXT("StopStreaming: connection %d not found"), ConnectionID);
    return;
  }

  Conn->bStreaming = false;



}

void USynavisStreamer::StartSignalling()
{
  // Signalling server
  //open/connect/autodiscover
  // Add handler for trickle ICE
  // Add handler for initial setup of connection
  // Synavis might attempt to setup first for simulation coupling.

  static bool bInitDone = false;
  if (!bInitDone) { rtcInitLogger(RTC_LOG_INFO, nullptr); bInitDone = true; }

  // Diagnostic: log size of std::string on this module to detect ABI mismatch across DLLs.
  UE_LOG(LogTemp, Log, TEXT("Synavis: sizeof(std::string) = %d"), (int)sizeof(std::string));

  // Build URI and store an owning std::string for lifetime stability
  FString UriF = FString::Printf(TEXT("ws://%s:%d"), *SignallingIP, SignallingPort);
  UE_LOG(LogTemp, Log, TEXT("Synavis: Connecting to signalling server at %s"), *UriF);
  this->WebSocketUri = TCHAR_TO_UTF8(*UriF);

  // Create a websocket via the libdatachannel C API. rtcCreateWebSocket will open the socket
    // and return an integer id. Attach the streamer pointer as the datachannel user pointer so
    // callbacks can look up the authoritative context from the central map.
    rtcSetUserPointer(SignallingId, this);
  if (SignallingId != 0)
  {
    // already created
    UE_LOG(LogTemp, Warning, TEXT("Synavis: Signalling websocket already created (id=%d)"), SignallingId);
  }
  else
  {
    int ws = rtcCreateWebSocket(this->WebSocketUri.c_str());
    if (ws <= 0)
    {
      UE_LOG(LogTemp, Error, TEXT("Synavis: Failed to create signalling websocket (rtcCreateWebSocket returned %d)"), ws);
      return;
    }
    SignallingId = ws;
    rtcSetUserPointer(SignallingId, this);
    // Register callbacks (C API)
    rtcSetOpenCallback(SignallingId, Synavis_Rtc_OnOpen);
    rtcSetClosedCallback(SignallingId, Synavis_Rtc_OnClosed);
    rtcSetErrorCallback(SignallingId, Synavis_Rtc_OnError);
    rtcSetMessageCallback(SignallingId, Synavis_Rtc_OnMessage);
    UE_LOG(LogTemp, Log, TEXT("Synavis: Created signalling websocket id=%d"), SignallingId);
  }
}

void USynavisStreamer::HandlePcLocalDescriptionCallback(int pc, const char* sdp, const char* type)
{
  FSynavisConnection* Conn = this->GetConnectionFromPC(pc);
  if (!Conn) return;
  // Verbose: dump local description type and SDP for diagnosis
  FString t = type ? FString(ANSI_TO_TCHAR(type)) : FString(TEXT("<null>"));
  FString sdff = sdp ? FString(ANSI_TO_TCHAR(sdp)) : FString(TEXT("<null>"));
  UE_LOG(LogTemp, Verbose, TEXT("Synavis: Storing local description for PC %d (conn %d) type=%s sdp len=%d"), pc, Conn->ConnectionID, *t, sdff.Len());
  Conn->SDP = sdff;
}

void USynavisStreamer::HandlePcGatheringStateChangeCallback(int pc, int state)
{
  // state is rtcGatheringState enum as int; RTC_GATHERING_COMPLETE == 2
  if (state == 2)
  {
    UE_LOG(LogTemp, Log, TEXT("Synavis: PeerConnection %d gathering state complete - no action as thread waits for open events"), pc);
  }
  else
  {
    UE_LOG(LogTemp, Log, TEXT("Synavis: PeerConnection %d gathering state changed to %d"), pc, state);
  }
}

void USynavisStreamer::HandlePcSignalingStateChangeCallback(int pc, int state)
{
  // rtcSignalingState: 0=Stable, 1=HaveLocalOffer, 2=HaveRemoteOffer, 3=HaveLocalPranswer, 4=HaveRemotePranswer
  // When we have a local offer or local pranswer, attempt to apply any queued remote answers.
  if (state == 1 || state == 3)
  {
    DrainPendingAnswersForPC(pc);
  }
}


void USynavisStreamer::HandleDataChannelMessageCallback(int dc, const std::variant<TArray<uint8>, std::string>& message)
{
  // First check for a captured context stored by the callback thread
  {
    FScopeLock lock(&G_CapturedDcMutex);
    if (G_CapturedDcMap.Contains(dc))
    {
      CapturedDCInfo info = G_CapturedDcMap[dc];
      FSynavisConnection* ConnShared = info.RawConn;
      int32 ConnectionPlayerID = info.ConnectionID;
      uint32 HandlerId = info.HandlerID;
      if (ConnShared)
      {
        if (HandlerId == 0)
        {
          UE_LOG(LogTemp, Log, TEXT("Synavis: Received message on system datachannel %d (conn %d) - invoking generic handler [captured]"), dc, ConnShared->ConnectionID);
          OnDataChannelMessage(message);
          return;
        }
        FSynavisHandler Key; Key.HandlerID = HandlerId;
        const FSynavisHandler* H = RegisteredDataHandlers.Find(Key);
        if (H)
        {
          UE_LOG(LogTemp, Log, TEXT("Synavis: Dispatching DataChannel %d -> Handler %u (Conn=%d) [captured]"), dc, HandlerId, ConnShared->ConnectionID);
          if (std::holds_alternative<std::string>(message))
          {
            const std::string& s = std::get<std::string>(message);
            FString Msg = FString(UTF8_TO_TCHAR(s.c_str()));
            if (H->MsgHandler.IsBound()) H->MsgHandler.Execute(Msg);
            if (H->MsgCbCpp) H->MsgCbCpp(ConnShared->ConnectionID, Msg);
            return;
          }
          else
          {
            const TArray<uint8>& b = std::get<TArray<uint8>>(message);
            if (H->DataHandler.IsBound()) H->DataHandler.Execute(b);
            if (H->DataCbCpp) H->DataCbCpp(ConnShared->ConnectionID, b);
            return;
          }
        }
      }
      else if (ConnectionPlayerID != 0 && Connections.Contains(ConnectionPlayerID))
      {
        FSynavisConnection* C = FindConnectionByPlayerID(ConnectionPlayerID);
        if (C)
        {
          if (HandlerId == 0)
          {
            UE_LOG(LogTemp, Log, TEXT("Synavis: Received message on system datachannel %d (conn %d) - invoking generic handler [id fallback]"), dc, C->ConnectionID);
            OnDataChannelMessage(message);
            return;
          }
          FSynavisHandler Key; Key.HandlerID = HandlerId;
          const FSynavisHandler* H = RegisteredDataHandlers.Find(Key);
          if (H)
          {
            if (std::holds_alternative<std::string>(message))
            {
              const std::string& s = std::get<std::string>(message);
              FString Msg = FString(UTF8_TO_TCHAR(s.c_str()));
              if (H->MsgHandler.IsBound()) H->MsgHandler.Execute(Msg);
              if (H->MsgCbCpp) H->MsgCbCpp(C->ConnectionID, Msg);
              return;
            }
            else
            {
              const TArray<uint8>& b = std::get<TArray<uint8>>(message);
              if (H->DataHandler.IsBound()) H->DataHandler.Execute(b);
              if (H->DataCbCpp) H->DataCbCpp(C->ConnectionID, b);
              return;
            }
          }
        }
      }
      // If captured context present but did not resolve, fall through to original logic
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: Captured context present for dc=%d but could not dispatch (connPtr=%p connId=%d handler=%u) - falling through to fallback logic"), dc, info.RawConn, info.ConnectionID, info.HandlerID);
    }
  }

  // Fast-path: if a per-datachannel context exists, use it directly
  void* uptr = rtcGetUserPointer(dc);
  if (uptr)
  {
    DataChannelCtx* ctx = reinterpret_cast<DataChannelCtx*>(uptr);
    UE_LOG(LogTemp, Verbose, TEXT("Synavis: Data channel %d label='%s' has user pointer set"), dc, *GetDataChannelLabelSafe(dc));
    if (ctx && ctx->Streamer == this)
    {
      uint32 HandlerId = ctx->HandlerID;
      int32 ConnectionPlayerID = ctx->ConnectionID;
      // If this is a system channel (HandlerId==0), invoke generic handler
      if (HandlerId == 0)
      {
        UE_LOG(LogTemp, Log, TEXT("Synavis: Received message on system datachannel %d (conn %d) - invoking generic handler"), dc, ConnectionPlayerID);
        OnDataChannelMessage(message);
        return;
      }

      // find handler entry in registered set (uses HandlerID equality)
      FSynavisHandler Key;
      Key.HandlerID = HandlerId;
      const FSynavisHandler* H = RegisteredDataHandlers.Find(Key);
      if (H)
      {
        UE_LOG(LogTemp, Log, TEXT("Synavis: Dispatching DataChannel %d -> Handler %u (Conn=%d)"), dc, HandlerId, ConnectionPlayerID);
        if (std::holds_alternative<std::string>(message))
        {
          const std::string& s = std::get<std::string>(message);
          FString Prefix = FString::Printf(TEXT("DataChannelTextRawHex dc=%d"), dc);
          //LogHexVerbose(s.c_str(), s.size(), *Prefix);
          FString Msg = FString(UTF8_TO_TCHAR(s.c_str()));
          UE_LOG(LogTemp, Verbose, TEXT("Synavis: Received text message on dc %d, handler %u: %s"), dc, HandlerId, *Msg.Left(512));
          if (H->MsgHandler.IsBound()) H->MsgHandler.Execute(Msg);
          if (H->MsgCbCpp) H->MsgCbCpp(ConnectionPlayerID, Msg);
          return;
        }
        else
        {
          const TArray<uint8>& b = std::get<TArray<uint8>>(message);
          FString Prefix = FString::Printf(TEXT("DataChannelBinaryRawHex dc=%d size=%d"), dc, b.Num());
          //LogHexVerbose(reinterpret_cast<const char*>(b.GetData()), b.Num(), *Prefix);
          UE_LOG(LogTemp, Verbose, TEXT("Synavis: Received binary message on dc %d, handler %u, size=%d"), dc, HandlerId, b.Num());
          if (H->DataHandler.IsBound()) H->DataHandler.Execute(b);
          if (H->DataCbCpp) H->DataCbCpp(ConnectionPlayerID, b);
          return;
        }
      }
      // If there is a ctx but the handler is not registered, fall through to diagnostics below
    }
  }
  else
  {
    UE_LOG(LogTemp, Warning, TEXT("Synavis: Data channel %d has no user pointer set - invoking generic handler"), dc);
    OnDataChannelMessage(message);
    return;
  }

  // Diagnostic fallback: we had a user pointer but no handler executed
  {
    void* uptr2 = rtcGetUserPointer(dc);
    DataChannelCtx* ctx2 = uptr2 ? reinterpret_cast<DataChannelCtx*>(uptr2) : nullptr;
    UE_LOG(LogTemp, Warning, TEXT("Synavis: Unhandled message on dc=%d ctx=%p - falling back to generic handler"), dc, uptr2);
    if (ctx2) {
      UE_LOG(LogTemp, Warning, TEXT("  ctx info: streamer=%p conn=%d handler=%u peerpc=%d connRaw=%p"), ctx2->Streamer, ctx2->ConnectionID, ctx2->HandlerID, ctx2->PeerPC, ctx2->ConnRaw);
    }
    UE_LOG(LogTemp, Warning, TEXT("  RegisteredDataHandlers count=%d Connections count=%d"), RegisteredDataHandlers.Num(), Connections.Num());
    for (const FSynavisHandler& H : RegisteredDataHandlers) {
      UE_LOG(LogTemp, Verbose, TEXT("    Registered handler id=%u wantsDedicated=%d acceptsInbound=%d"), H.HandlerID, H.WantsDedicatedChannel ? 1 : 0, H.AcceptsInboundMessages ? 1 : 0);
    }
    for (const auto& Pair : Connections) {
      TSharedPtr<FSynavisConnection> C = Pair.Value;
      if (!C) continue;
      FString Mappings;
      for (const auto& kv : C->HandlersByChannel) {
        Mappings += FString::Printf(TEXT("dc=%d->h=%u "), kv.Key, kv.Value);
      }
      UE_LOG(LogTemp, Verbose, TEXT("  Conn %d PC=%d DataChannel=%d Mappings=[%s]"), C->ConnectionID, C->PeerConnection, C->DataChannel, *Mappings);
    }
    UE_LOG(LogTemp, Warning, TEXT("Synavis: Invoking generic handler as last resort for dc=%d"), dc);
    OnDataChannelMessage(message);
    return;
  }
}


int32 USynavisStreamer::RegisterDataSourceCpp(const std::function<void(int32, const TArray<uint8>&)>& OnData,
  const std::function<void(int32, const FString&)>& OnMessage,
  USceneCaptureComponent2D* SceneCapture,
  bool DedicatedChannel,
  bool AcceptsInboundMessages)
{
  UE_LOG(LogTemp, Log, TEXT("Synavis: RegisterDataSourceCpp called (DedicatedChannel=%d AcceptsInbound=%d)"), DedicatedChannel ? 1 : 0, AcceptsInboundMessages ? 1 : 0);
  // if very verbose check the parent of SceneCapture and print its name
  UE_LOG(LogTemp, Verbose, TEXT("Synavis: RegisterDataSourceCpp:  SceneCapture=%p, name=%s and parent-name=%s"), SceneCapture,
    SceneCapture ? *SceneCapture->GetName() : TEXT("<null>"),
    (SceneCapture && SceneCapture->GetOwner()) ? *SceneCapture->GetOwner()->GetName() : TEXT("<no-owner>"));

  FSynavisHandler H;
  H.HandlerID = NextHandlerId++;
  if (AcceptsInboundMessages)
  {
    H.DataCbCpp = OnData;
    H.MsgCbCpp = OnMessage;
  }
  H.AcceptsInboundMessages = AcceptsInboundMessages;
  H.Video = SceneCapture;
  H.WantsDedicatedChannel = DedicatedChannel;
  RegisteredDataHandlers.Add(H);
  UE_LOG(LogTemp, Log, TEXT("Synavis: RegisterDataSourceCpp -> assigned handler id=%u (video=%d wantsDedicated=%d acceptsInbound=%d)"), H.HandlerID, H.Video ? 1 : 0, H.WantsDedicatedChannel ? 1 : 0, H.AcceptsInboundMessages ? 1 : 0);
  // Report RTC state after handler registration
  RTCReport();
  return static_cast<int32>(H.HandlerID);
}

int32 USynavisStreamer::RegisterVideoSourceCpp(USceneCaptureComponent2D* SceneCapture,
  bool DedicatedChannel,
  bool AcceptsInboundMessages)
{
  // Create a handler that provides video but does not accept inbound messages by default.
  // Reuse the C++ registration implementation with empty callbacks.
  return RegisterDataSourceCpp(std::function<void(int32, const TArray<uint8>&)>(),
    std::function<void(int32, const FString&)>(),
    SceneCapture, DedicatedChannel, AcceptsInboundMessages);
}

int USynavisStreamer::RegisterVideoSource(USceneCaptureComponent2D* SceneCapture,
  bool DedicatedChannel,
  bool AcceptsInboundMessages)
{
  // Blueprint wrapper: call the C++ registration helper
  return RegisterVideoSourceCpp(SceneCapture, DedicatedChannel, AcceptsInboundMessages);
}

void USynavisStreamer::UnregisterDataSource(int32 HandlerId)
{
  FSynavisHandler ToRemove;
  ToRemove.HandlerID = HandlerId;
  RegisteredDataHandlers.Remove(ToRemove);
  // remove per-connection mappings and reverse map entries
  for (auto& Pair : Connections)
  {
    TSharedPtr<FSynavisConnection> C = Pair.Value;
    (void)C;
  }
}

bool USynavisStreamer::SendBinaryToConnection(int32 HandlerId, int32 ConnectionPlayerID, const TArray<uint8>& Data)
{
  // find connection
  bool Sent = false;
  FSynavisConnection* Conn = FindConnectionByPlayerID(ConnectionPlayerID);
  if (!Conn) return false;
  auto dcid = Conn->DataChannel;
  if (dcid != 0 && rtcIsOpen(dcid))
  {
    int sendRes = rtcSendMessage(dcid, reinterpret_cast<const char*>(Data.GetData()), static_cast<int>(Data.Num()));
    Sent = (sendRes == RTC_ERR_SUCCESS);
    if (sendRes == RTC_ERR_SUCCESS)
    {
      UE_LOG(LogTemp, VeryVerbose, TEXT("Synavis: Sent binary to conn=%d handler=%d dc=%d size=%d"), ConnectionPlayerID, HandlerId, dcid, Data.Num());
    }
  }
  return Sent;
}
  

bool USynavisStreamer::SendTextToConnection(int32 HandlerId, int32 ConnectionPlayerID, const FString& Text)
{
  // If ConnectionPlayerID < 0 treat as broadcast to all connections
  if (ConnectionPlayerID < 0)
  {
    bool any = false;
    UE_LOG(LogTemp, Log, TEXT("Synavis: SendTextToConnection broadcast requested (handler=%d) to %d connections"), HandlerId, Connections.Num());
    for (const auto& Pair : Connections)
    {
      int32 cid = Pair.Key;
      if (SendTextToConnection(HandlerId, cid, Text)) any = true;
    }
    return any;
  }
  UE_LOG(LogTemp, Verbose, TEXT("Synavis: SendTextToConnection called Handler=%d Player=%d TextPreview=%s"), HandlerId, ConnectionPlayerID, *Text.Left(200));
  FTCHARToUTF8 Utf8(*Text);
  bool Sent = false;
  const char* ptr = Utf8.Get();
  int len = Utf8.Length();
  auto* Conn = FindConnectionByPlayerID(ConnectionPlayerID);
  if (!Conn)
  {
    UE_LOG(LogTemp, Warning, TEXT("Synavis: SendTextToConnection - no connection found for player %d (handler=%d). TextPreview=%s"), ConnectionPlayerID, HandlerId, *Text.Left(200));
    return false;
  }
  int chosenDc = 0;
  // Prefer a per-handler dedicated channel if present and open
  for (const auto& kv : Conn->HandlersByChannel)
  {
    int dc = kv.Key;
    int hid = kv.Value;
    if (hid == HandlerId)
    {
      if (dc != 0 && rtcIsOpen(dc)) { chosenDc = dc; break; }
    }
  }
  // Fallback to the system datachannel
  if (chosenDc == 0)
  {
    if (Conn->DataChannel != 0 && rtcIsOpen(Conn->DataChannel)) chosenDc = Conn->DataChannel;
  }
  if (chosenDc == 0)
  {
    // Diagnostic: log why we couldn't send, include call parameters and channel states
    UE_LOG(LogTemp, Warning, TEXT("Synavis: SendTextToConnection failed - no open datachannel (handler=%d conn=%d). TextPreview=%s"), HandlerId, ConnectionPlayerID, *Text.Left(200));
    UE_LOG(LogTemp, Verbose, TEXT("  Conn->DataChannel = %d rtcIsOpen=%d"), Conn->DataChannel, Conn->DataChannel ? rtcIsOpen(Conn->DataChannel) : 0);
    for (const auto& kv : Conn->HandlersByChannel)
    {
      UE_LOG(LogTemp, Verbose, TEXT("  handler-channel: dc=%d -> handler=%d rtcIsOpen=%d"), kv.Key, kv.Value, kv.Key ? rtcIsOpen(kv.Key) : 0);
    }
    return false;
  }

  // Send as text (negative size per C API convention)
  int sendRes = rtcSendMessage(chosenDc, ptr, - (len + 1));
  if (sendRes != RTC_ERR_SUCCESS)
  {
    UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcSendMessage returned %d when sending text (handler=%d conn=%d) -> dc=%d TextPreview=%s"), sendRes, HandlerId, ConnectionPlayerID, chosenDc, *Text.Left(200));
    // Verbose: dump outgoing payload bytes and channel state
    FTCHARToUTF8 OutUtf8(*Text);
    //LogHexVerbose(OutUtf8.Get(), static_cast<size_t>(OutUtf8.Length()), TEXT("OutgoingTextRawHex"));
    UE_LOG(LogTemp, Verbose, TEXT("  rtcIsOpen(chosenDc)=%d rtcMaxMessageSize=%d"), rtcIsOpen(chosenDc), rtcMaxMessageSize(chosenDc));
    return false;
  }
  Sent = true;
  UE_LOG(LogTemp, VeryVerbose, TEXT("Synavis: Sent text (handler=%d conn=%d) -> dc=%d size=%d TextPreview=%s"), HandlerId, ConnectionPlayerID, chosenDc, len, *Text.Left(200));
  return Sent;
}

void USynavisStreamer::HandleDataChannelOpenCallback(int dc)
{
  // Prefer reading the per-datachannel context on the game thread to avoid
  // any race or use-after-free from callback threads. Capture only the
  // datachannel id here and resolve the context on the game thread.
  //AsyncTask(ENamedThreads::GameThread, [this, dc]() {
  void* uptr = rtcGetUserPointer(dc);
  if (!uptr)
  {
    UE_LOG(LogTemp, Warning, TEXT("Synavis: DataChannel %d label='%s' opened but had no user-pointer context on game thread"), dc, *GetDataChannelLabelSafe(dc));
    return;
  }
  DataChannelCtx* ctx = reinterpret_cast<DataChannelCtx*>(uptr);
  if (!ctx || ctx->Streamer != this)
  {
    UE_LOG(LogTemp, Warning, TEXT("Synavis: DataChannel %d label='%s' opened with mismatched context"), dc, *GetDataChannelLabelSafe(dc));
    return;
  }
  // Prefer pinned shared pointer to the connection when available
  FSynavisConnection* ConnShared = ctx->ConnRaw;
  if (ConnShared)
  {
    int maxMsg = rtcMaxMessageSize(dc);
    ConnShared->MaxMessageSize = static_cast<uint32>(maxMsg);
    ConnShared->DataChannel.state = ETransportState::OPEN;
    UE_LOG(LogTemp, Log, TEXT("Synavis: DataChannel %d label='%s' opened for connection %d (max message size=%u) [resolved on game thread]"), dc, *GetDataChannelLabelSafe(dc), ConnShared->ConnectionID, ConnShared->MaxMessageSize);
  }
  UE_LOG(LogTemp, Warning, TEXT("Synavis: DataChannel %d label='%s' opened but owning connection not found"), dc, *GetDataChannelLabelSafe(dc));
  //});
}

void USynavisStreamer::HandleDataChannelClosedCallback(int dc)
{
  // Resolve the per-datachannel context on the game thread. Do NOT free the
  // context here — the lower-level C callback path currently deletes the
  // allocation after NotifyDataChannelClosed returns. Only perform logical
  // cleanup of connection state and handler mappings.
  //AsyncTask(ENamedThreads::GameThread, [this, dc]() {
    void* uptr = rtcGetUserPointer(dc);
    if (!uptr)
    {
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: DataChannel %d label='%s' closed (no associated user-pointer on game thread)"), dc, *GetDataChannelLabelSafe(dc));
      return;
    }
    DataChannelCtx* ctx = reinterpret_cast<DataChannelCtx*>(uptr);
    if (!ctx || ctx->Streamer != this)
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: DataChannel %d label='%s' closed with mismatched context"), dc, *GetDataChannelLabelSafe(dc));
      return;
    }
    // Remove central ownership entry if present; freeing occurs when the
    // TSharedPtr reference count drops out of the DataChannelContexts map.
    if (DataChannelContexts && DataChannelContexts->Contains(dc))
    {
      DataChannelContexts->Remove(dc);
      rtcSetUserPointer(dc, nullptr);
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: Removed DataChannelCtx for dc=%d from central map and cleared user pointer"), dc);
    }
    // Try to pin the owning connection first
    FSynavisConnection* ConnShared = ctx->ConnRaw;
    if (ConnShared)
    {
      ConnShared->State = EPeerState::NoConnection;
      UE_LOG(LogTemp, Log, TEXT("Synavis: DataChannel %d label='%s' closed for connection %d"), dc, *GetDataChannelLabelSafe(dc), ConnShared->ConnectionID);
      if (ConnShared->HandlersByChannel.Contains(dc)) ConnShared->HandlersByChannel.Remove(dc);
      return;
    }
    UE_LOG(LogTemp, Warning, TEXT("Synavis: DataChannel %d label='%s' closed but owning connection not found"), dc, *GetDataChannelLabelSafe(dc));
  //});
}

bool USynavisStreamer::TryParseJSON(std::string message, FJsonObject& OutJsonObject)
{
  FString In = FString(UTF8_TO_TCHAR(message.c_str()));
  TSharedPtr<FJsonObject> Parsed;
  TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(In);
  if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
    return false;
  // Copy parsed into OutJsonObject
  OutJsonObject = *Parsed;
  return true;
}



void USynavisStreamer::CommunicateSDPForConnection(const FSynavisConnection& Conn)
{
  if (SignallingId == 0 || !rtcIsOpen(SignallingId))
  {
    UE_LOG(LogTemp, Warning, TEXT("Synavis: CommunicateSDPForConnection: signalling websocket not open (SignallingId=%d) - cannot send SDP for conn %d"), SignallingId, Conn.ConnectionID);
    return;
  }

  int pc = Conn.PeerConnection;
  if (pc == 0)
    return;

  // Retrieve local description via C API
  const int BufSize = 65536;
  std::vector<char> sdpBuf(BufSize);
  int got = rtcGetLocalDescription(pc, sdpBuf.data(), BufSize);
  if (got <= 0)
  {
    UE_LOG(LogTemp, Warning, TEXT("Synavis: CommunicateSDPForConnection: rtcGetLocalDescription returned %d for pc %d"), got, pc);
    return;
  }
  std::string sdpStr(sdpBuf.data(), static_cast<size_t>(got));
  char typeBuf[64] = { 0 };
  rtcGetLocalDescriptionType(pc, typeBuf, static_cast<int>(sizeof(typeBuf)));
  std::string typestr = typeBuf;

  TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
  Obj->SetStringField(TEXT("type"), FString(UTF8_TO_TCHAR(typestr.c_str())));
  Obj->SetStringField(TEXT("sdp"), FString(UTF8_TO_TCHAR(sdpStr.c_str())));
  Obj->SetNumberField(TEXT("playerId"), Conn.ConnectionID);

  FString Out;
  TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
  if (FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer))
  {
    auto OutAnsi = StringCast<ANSICHAR>(*Out);
    ////LogHex(OutAnsi.Get(), static_cast<size_t>(OutAnsi.Length()), TEXT("StringCast(Out) bytes"));
    std::string outcpp(OutAnsi.Get(), OutAnsi.Length());
    ////LogHex(outcpp.c_str(), outcpp.size(), TEXT("std::string(outcpp) bytes"));

    // Use the C API to send a text message. For text we pass a negative size according to the C API
    // convention (-(length+1)).
    int sendSize = -static_cast<int>(outcpp.size() + 1);
    // Diagnostic: log SignallingId and whether the websocket is open before send
    bool wsOpen = (SignallingId != 0) && rtcIsOpen(SignallingId);
    UE_LOG(LogTemp, Log, TEXT("Synavis: Sending SDP for conn %d via SignallingId=%d rtcIsOpen=%d sendSize=%d preview='%s'"), Conn.ConnectionID, SignallingId, wsOpen ? 1 : 0, sendSize, *FString(UTF8_TO_TCHAR(outcpp.c_str())).Left(200));
    int sendRes = rtcSendMessage(SignallingId, outcpp.c_str(), sendSize);
    if (sendRes != RTC_ERR_SUCCESS)
    {
      UE_LOG(LogTemp, Error, TEXT("Synavis: rtcSendMessage returned %d when sending SDP for conn %d (SignallingId=%d)"), sendRes, Conn.ConnectionID, SignallingId);
    }
    else
    {
      UE_LOG(LogTemp, Log, TEXT("Synavis: Sent SDP for conn %d size=%d via SignallingId=%d"), Conn.ConnectionID, static_cast<int>(outcpp.size()), SignallingId);
    }

  }
}

FSynavisConnection* USynavisStreamer::FindConnectionByPlayerID(int32 PlayerID)
{
  TSharedPtr<FSynavisConnection>* Ptr = Connections.Find(PlayerID);
  if (!Ptr || !(*Ptr)) return nullptr;
  return Ptr->Get();
}

const FSynavisConnection* USynavisStreamer::FindConnectionByPlayerID(int32 PlayerID) const
{
  const FSynavisConnection* Found = nullptr;
  const TMap<int32, TSharedPtr<FSynavisConnection>>* mapPtr = &Connections;
  TSharedPtr<FSynavisConnection> const* Ptr = mapPtr->Find(PlayerID);
  if (Ptr && *Ptr) Found = Ptr->Get();
  return Found;
}

// Implementation
TArray<ANSICHAR>* FSynavisConnection::AddPersistentUtf8(const FString& Str) {
    FTCHARToUTF8 Utf8(*Str);
    int32 Len = Utf8.Length();
    TArray<ANSICHAR>* Buf = new TArray<ANSICHAR>(); // or use Emplace if TArray supports
    Buf->SetNum(Len + 1);
    FMemory::Memcpy(Buf->GetData(), Utf8.Get(), Len);
    (*Buf)[Len] = '\0';
    PersistentTrackUtf8.Add(*Buf);
    return Buf;
}

void USynavisStreamer::CreateConnectionForPlayer(int32 PlayerID)
{
  // Skip if a connection already exists
  if (Connections.Contains(PlayerID))
  {
    UE_LOG(LogTemp, Log, TEXT("Synavis: Connection for player %d already exists"), PlayerID);
    return;
  }
  // Allocate a shared connection and store it in the Connections map so
  // ownership is shared and lifetime is managed by TSharedPtr.
  TSharedPtr<FSynavisConnection> Conn = MakeShared<FSynavisConnection>();
  Conn->ConnectionID = PlayerID;
  Conn->bStreaming = AnyConnectionStreaming();
  Conn->State = EPeerState::NoConnection;

  // Insert into connections map before starting ICE so callbacks can find it
  Connections.Add(PlayerID, Conn);

  UE_LOG(LogTemp, Log, TEXT("Synavis: Created connection object for player %d (pc=%d dc=%d)"), PlayerID, StoredConn ? StoredConn->PeerConnection : 0, StoredConn ? StoredConn->DataChannel : 0);

  // start the negotiation thread for this connection
  AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, PlayerID]() {
    FSynavisConnection* ConnPtr = FindConnectionByPlayerID(PlayerID);
    if (!ConnPtr)
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: CreateConnectionForPlayer background task - connection for player %d not found"), PlayerID);
      return;
    }
    // Start ICE negotiation for this connection
    StartConnectionNegotiation(*ConnPtr);
  });
}

void USynavisStreamer::HandleSignallingOpen()
{
  UE_LOG(LogTemp, Log, TEXT("Synavis: Signalling websocket opened (member handler)"));
  this->ConnectionState = ESynavisState::SignallingUp;
}

void USynavisStreamer::HandleSignallingClose()
{
  UE_LOG(LogTemp, Log, TEXT("Synavis: Signalling websocket closed (member handler)"));
  this->ConnectionState = ESynavisState::Offline;
}

void USynavisStreamer::HandleSignallingError(const std::string& Err)
{
  UE_LOG(LogTemp, Error, TEXT("Synavis: Signalling websocket error: %s"), ANSI_TO_TCHAR(Err.c_str()));
  this->ConnectionState = ESynavisState::Failure;
}

void USynavisStreamer::HandleSignallingMessage(const std::variant<TArray<uint8>, std::string>& messageOrData)
{
  if (std::holds_alternative<std::string>(messageOrData))
  {
    const std::string& s = std::get<std::string>(messageOrData);
    FJsonObject Parsed;
    if (!TryParseJSON(s, Parsed))
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: Received non-JSON signalling text"));
      return;
    }

    FString Type;
    if (Parsed.HasField(TEXT("type")))
    {
      Type = Parsed.GetStringField(TEXT("type"));
    }

    if (Type.Equals(TEXT("playerConnected"), ESearchCase::IgnoreCase))
    {
      int32 PlayerID = -1;
      if (Parsed.HasField(TEXT("playerId"))) PlayerID = static_cast<int32>(Parsed.GetNumberField(TEXT("playerId")));
      if (PlayerID == -1 && Parsed.HasField(TEXT("PlayerID"))) PlayerID = static_cast<int32>(Parsed.GetNumberField(TEXT("PlayerID")));
      if (PlayerID == -1)
      {
        // Assign an internal connection id starting at 101 when signalling didn't provide one
        PlayerID = static_cast<int32>(CreateConnectionHandle());
        UE_LOG(LogTemp, Warning, TEXT("Synavis: playerConnected message missing playerId - generated id %d"), PlayerID);
      }
      // start new async task non-game-thread to fully negotiate this connection
      AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, PlayerID]() {
        auto c = CreateConnectionForPlayer(PlayerID);
        this->ConnectionNegotiationThread(c);
      });
      return;
    }

    // Routing of SDP / ICE messages to correct connection
    int32 TargetPlayer = -1;
    if (Parsed.HasField(TEXT("playerId")))
    {
      TargetPlayer = static_cast<int32>(Parsed.GetNumberField(TEXT("playerId")));
    }
    else if (Parsed.HasField(TEXT("PlayerID")))
    {
      TargetPlayer = static_cast<int32>(Parsed.GetNumberField(TEXT("PlayerID")));
    }
    //if (Type.Equals(TEXT("answer"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("offer"), ESearchCase::IgnoreCase))
    if (Type.Equals(TEXT("answer"), ESearchCase::IgnoreCase))
    {
      if (TargetPlayer == -1)
      {
        UE_LOG(LogTemp, Warning, TEXT("Synavis: SDP message missing playerId; ignoring"));
        return;
      }
      FSynavisConnection* Conn = FindConnectionByPlayerID(TargetPlayer);
      if (!Conn)
      {
        UE_LOG(LogTemp, Warning, TEXT("Synavis: Received SDP for unknown player %d"), TargetPlayer);
        return;
      }
      else
      {
        Conn->SDP = Parsed;
      }

      return;
    }

    if (Type.Equals(TEXT("iceCandidate"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("candidate"), ESearchCase::IgnoreCase))
    {
      if (TargetPlayer == -1)
      {
        UE_LOG(LogTemp, Warning, TEXT("Synavis: iceCandidate message missing playerId; ignoring"));
        return;
      }
      FSynavisConnection* Conn = FindConnectionByPlayerID(TargetPlayer);
      if (!Conn)
      {
        UE_LOG(LogTemp, Warning, TEXT("Synavis: Received ICE for unknown player %d"), TargetPlayer);
        return;
      }
      if(Conn->State < EPeerState::RemoteICE)
      {
        UE_LOG(LogTemp, Log, TEXT("Synavis: Received ICE candidate for player %d (pc=%d) before being ready to parse remote ICE; ignoring!!"), TargetPlayer, Conn->PeerConnection);
        return;
      }
      Conn->ICE.Enqueue(Parsed);
    }
  }
  else
  {
    // Binary signalling frames not expected in this use-case
    UE_LOG(LogTemp, Verbose, TEXT("Synavis: Received binary signalling frame (ignored)"));
  }
}

static uint32 CreatePawnHandle()
{
  static uint32 NextPawnHandle = 1;
  return NextPawnHandle++;
}

static uint32 CreateConnectionHandle()
{
  static uint32 NextConnHandle = 101;
  return NextConnHandle++;
}

FORCEINLINE FString LogSetup(uint32 ID, USceneComponent* Child)
{
  // get actor name
  FString ActorName = Child->GetOwner() ? Child->GetOwner()->GetName() : TEXT("NoOwner");
  FString CompName = Child->GetName();
  return FString::Printf(TEXT("%d<%s:%s>"), ID, *ActorName, *CompName);
}


int USynavisStreamer::RegisterDataSource(
  FSynavisData DataHandler,
  FSynavisMessage MsgHandler,
  USceneCaptureComponent2D* SceneCapture,
  bool DedicatedChannel,
  bool AcceptsInboundMessages)
{
  FSynavisHandler Handler;
  Handler.DataHandler = DataHandler;
  Handler.MsgHandler = MsgHandler;
  Handler.HandlerID = CreatePawnHandle();
  auto LogPrefix = LogSetup(Handler.HandlerID, SceneCapture);

  // If we don't have an active PeerConnection yet, register the handler with a null track (offline).
  // If we do have a PeerConnection, create the send-only video track now according to the source policy.
  if (SceneCapture)
  {
    // Ensure the SceneCapture has a render target we can use
    UTextureRenderTarget2D* VideoSource = SceneCapture->TextureTarget;
    if (!VideoSource)
    {
      UE_LOG(LogTemp, Warning, TEXT("%s: SceneCapture provided but has no TextureTarget - skipping video registration"), *LogPrefix);
    }
    else
    {
      // Prepare a placeholder media descriptor id (we'll create the per-connection track when the PeerConnection exists)
      Handler.MediaDesc = 0;

      // Register the video source but do not create a PeerConnection-local track here. Tracks are created when a connection is established.
      Handler.Video = SceneCapture;
    }
  }

  // If a dedicated channel was requested, mark it for creation per-connection; otherwise use the system channel
  Handler.WantsDedicatedChannel = DedicatedChannel;
  Handler.AcceptsInboundMessages = AcceptsInboundMessages;

  // Depending on the SourcePolicy, attempt to set up per-connection datachannels now.
  switch(this->SourcePolicy)
  {
    case ESynavisSourcePolicy::RemainStatic:
      if (Handler.AcceptsInboundMessages && !IsInGame())
      {
        this->SetupDataChannel(Handler);
      }
      break;
    case ESynavisSourcePolicy::DynamicOptional:
      if (Handler.AcceptsInboundMessages)
        this->SetupDataChannel(Handler);
      break;
    case ESynavisSourcePolicy::DynamicMandatory:
    {
      if (Handler.AcceptsInboundMessages)
      {
        int Res = this->SetupDataChannel(Handler);
        if (Res < 0)
        {
          UE_LOG(LogTemp, Warning, TEXT("%s: Failed to setup data channel for dynamic mandatory source"), *LogPrefix);
          return -1;
        }
      }
    }
    break;
    default:
      break;
  }

  return Handler.HandlerID;
}

void USynavisStreamer::CaptureFrame()
{
  // Capture frames for registered handlers using the zero-copy I420 path.
  // The registration step is expected to have created a valid video track for each handler.
  // If there is no connection that requests streaming, skip capture.
  bool anyStreaming = false;
  for (const auto& Pair : Connections)
  {
    TSharedPtr<FSynavisConnection> C = Pair.Value;
    if (C && C->bStreaming)
    {
      anyStreaming = true;
      break;
    }
  }
  if (!anyStreaming)
    return;

  for (const FSynavisHandler& Handler : RegisteredDataHandlers)
  {
    if (!Handler.Video)
      continue;
    USceneCaptureComponent2D* SceneCapture = Handler.Video;
    if (!SceneCapture)
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: Handler missing required SceneCapture - skipping"));
      continue;
    }
    UTextureRenderTarget2D* HandlerRT = SceneCapture->TextureTarget;
    if (!HandlerRT)
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: SceneCapture has no TextureTarget - skipping"));
      continue;
    }

    FTextureRenderTargetResource* RTResource = HandlerRT->GameThread_GetRenderTargetResource();
    if (!RTResource)
      continue;

    int Width = HandlerRT->SizeX;
    int Height = HandlerRT->SizeY;
    // Gather target track ids across all connections for this handler (only connections
    // that currently request streaming will receive frames). Compute this first so we
    // avoid allocating/enqueueing GPU readbacks when there are no active tracks.
    TArray<int32> TracksToSend;
    for (const auto& Pair : Connections)
    {
      TSharedPtr<FSynavisConnection> Conn = Pair.Value;
      if (!Conn) continue;
      if (!Conn->bStreaming) continue;
      auto it = Conn->TracksByHandler.Find(Handler.HandlerID);
      if (it && *it != 0 && rtcIsOpen(*it))
      {
        TracksToSend.Add(*it);
      }
    }

    if (TracksToSend.Num() == 0)
    {
      UE_LOG(LogTemp, Error, TEXT("Synavis: No open tracks for handler %d, skipping readback"), Handler.HandlerID);
      RTCReport();
      // this is what we need to fix so there is no point going on after this
      // exit game
      GetWorld()->GetFirstPlayerController()->ConsoleCommand(TEXT("exit"));
    }
    else
    {
      if (this->TextureConversionMode == ESynavisTextureConversionMode::GPU)
      {
        // TEMP: log info on first track in TracksToSend for diagnostics
        UE_LOG(LogTemp, Verbose, TEXT("Synavis: First Track: %d, isOpen: %d, maxMessageSize: %d"), TracksToSend[0], rtcIsOpen(TracksToSend[0]) ? 1 : 0, rtcMaxMessageSize(TracksToSend[0]));
        FRHIGPUTextureReadback* ReadbackY = nullptr;
        FRHIGPUTextureReadback* ReadbackU = nullptr;
        FRHIGPUTextureReadback* ReadbackV = nullptr;
        if (EnqueueI420ReadbackFromRenderTarget(HandlerRT, ReadbackY, ReadbackU, ReadbackV))
        {
          FPendingI420Readback rec;
          rec.ReadbackY = ReadbackY;
          rec.ReadbackU = ReadbackU;
          rec.ReadbackV = ReadbackV;
          rec.EnqueuedAt = FPlatformTime::Seconds();
          rec.TargetTracks = TracksToSend;
          rec.Width = Width;
          rec.Height = Height;
          PendingReadbacks.Add(rec);
          UE_LOG(LogTemp, Verbose, TEXT("Synavis: Enqueued I420 readback for handler %d (W=%d H=%d) to %d tracks"), Handler.HandlerID, Width, Height, TracksToSend.Num());
        }
        else
        {
          UE_LOG(LogTemp, Warning, TEXT("Synavis: Failed to enqueue I420 readback for handler %d"), Handler.HandlerID);
        }
      }
      else
      {
        // Readback is very similar: We still need to enqueue a readback from the GPU and add it to PendingReadbacks,
        // because this is the only way to avoid game loop blocking
        FPendingRGBReadback rec;
        rec.TargetTracks = TracksToSend;
        rec.Width = Width;
        rec.Height = Height;
        // create a future object and then enqueue the readback
        rec.ReadbackFuture = EnqueueRGBReadbackFromRenderTarget(HandlerRT);
        rec.EnqueuedAt = FPlatformTime::Seconds();
        PendingRGBReadbacks.Add(MoveTemp(rec));
        UE_LOG(LogTemp, Verbose, TEXT("Synavis: Enqueued RGB readback for handler %d (W=%d H=%d) to %d tracks"), Handler.HandlerID, Width, Height, TracksToSend.Num());
      }
    }
  }
}

void USynavisStreamer::SendFrameBytes(const TArray<uint8>& Bytes, const FString& Name, const FString& Format, int32 TargetTrackId)
{
  // Send outbound bytes via the best available path: prefer the provided TargetTrack (if open),
  // otherwise fall back to the system data channel. Previously this depended on a global
  // container; now we simply check the relevant targets directly.
  size_t sz = static_cast<size_t>(Bytes.Num());
  if (sz == 0)
  {
    UE_LOG(LogTemp, Verbose, TEXT("Synavis: SendFrameBytes called with empty payload (Name=%s, Format=%s)"), *Name, *Format);
    return;
  }

  // If a handler-specific RTC track id is provided and open, use it (video path)
  if (TargetTrackId != 0 && rtcIsOpen(TargetTrackId))
  {
    int sendRes = rtcSendMessage(TargetTrackId, reinterpret_cast<const char*>(Bytes.GetData()), static_cast<int>(sz));
    if (sendRes != RTC_ERR_SUCCESS)
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcSendMessage returned %d when sending frame bytes to track %d"), sendRes, TargetTrackId);
    }
    else
    {
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: Sent frame bytes to track %d size=%d Name=%s Format=%s"), TargetTrackId, static_cast<int>(sz), *Name, *Format);
    }
    return;
  }

  // Otherwise try the global/system data channel
  if (SystemDataChannel != 0 && rtcIsOpen(SystemDataChannel))
  {
    int sendRes = rtcSendMessage(SystemDataChannel, reinterpret_cast<const char*>(Bytes.GetData()), static_cast<int>(sz));
    if (sendRes != RTC_ERR_SUCCESS)
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcSendMessage returned %d when sending frame bytes to system datachannel"), sendRes);
    }
    else
    {
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: Sent frame bytes to system datachannel=%d size=%d Name=%s Format=%s"), SystemDataChannel, static_cast<int>(sz), *Name, *Format);
    }
    return;
  }

  UE_LOG(LogTemp, Warning, TEXT("No open DataChannel or Track to send frame bytes (Name=%s, Format=%s)"), *Name, *Format);
}

void USynavisStreamer::OnDataChannelMessage(const std::variant<TArray<uint8>, std::string>& message)
{
  if (std::holds_alternative<TArray<uint8>>(message))
  {
    const TArray<uint8>& data = std::get<TArray<uint8>>(message);
    UE_LOG(LogTemp, Log, TEXT("Synavis: Generic handler received binary message, size=%d"), data.Num());
    if (data.Num() > 0)
    {
      const int dump = FMath::Min<int>(static_cast<int>(data.Num()), 64);
      FString Hex;
      Hex.Reserve(dump * 3);
      for (int i = 0; i < dump; ++i) Hex += FString::Printf(TEXT("%02X "), data[i]);
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: Binary prefix (first %d bytes): %s"), dump, *Hex);
    }
    // preprocessing of the data type using the control bytes (user-defined handling)
  }
  else if (std::holds_alternative<std::string>(message))
  {
    const std::string& msgStr = std::get<std::string>(message);
    FString Msg = FString(UTF8_TO_TCHAR(msgStr.c_str()));
    UE_LOG(LogTemp, Log, TEXT("Synavis: Generic handler received text message: %s"), *Msg.Left(1024));
    // (user-defined handling of textual messages)
  }
}

void USynavisStreamer::HandlePcDataChannelCreated(int pc, int dc)
{
  UE_LOG(LogTemp, Warning, TEXT("Synavis(member): PC %d created datachannel %d userPtr=%p"), pc, dc, rtcGetUserPointer(dc));

  // Find matching connection for this peer connection id
  for (auto& Pair : Connections)
  {
    TSharedPtr<FSynavisConnection> C = Pair.Value;
    if (!C) continue;
    if (C->PeerConnection == pc)
    {
      if (C->DataChannel == 0)
      {
        C->DataChannel = dc;
        C->HandlersByChannel[dc] = 0;
        // Prefer authoritative ctx from central map
        {
          TSharedPtr<DataChannelCtx> ctxptr = GetDataChannelContext(dc);
          if (ctxptr.IsValid()) {
            DataChannelCtx* ctx = ctxptr.Get();
            UE_LOG(LogTemp, Warning, TEXT("HandlePcDataChannelCreated: Found central ctx %p for dc=%d prior conn=%d prior handler=%u"), ctx, dc, ctx->ConnectionID, ctx->HandlerID);
            ctx->ConnectionID = C->ConnectionID;
            ctx->HandlerID = 0;
            ctx->ConnRaw = C.Get();
          } else {
            // final fallback: try user pointer if it actually contains a ctx
            void* uptr = rtcGetUserPointer(dc);
            if (uptr) {
              DataChannelCtx* ctx = reinterpret_cast<DataChannelCtx*>(uptr);
              UE_LOG(LogTemp, Warning, TEXT("HandlePcDataChannelCreated: Fallback userPtr=%p for dc=%d"), uptr, dc);
              if (ctx && ctx->Streamer == this) { ctx->ConnectionID = C->ConnectionID; ctx->HandlerID = 0; ctx->ConnRaw = C.Get(); UE_LOG(LogTemp, Warning, TEXT("HandlePcDataChannelCreated: Assigned fallback ctx %p -> conn=%d"), ctx, C->ConnectionID); }
            }
          }
        }
        UE_LOG(LogTemp, Warning, TEXT("Synavis(member): Adopted incoming datachannel %d as system channel for conn %d"), dc, C->ConnectionID);
        RTCReport();
        return;
      }

      TArray<uint32> MissingDedicatedHandlers;
      for (const FSynavisHandler& H : RegisteredDataHandlers)
      {
        if (!H.WantsDedicatedChannel || !H.AcceptsInboundMessages) continue;
        bool found = false;
        for (const auto& hb : C->HandlersByChannel)
        {
          if (hb.Value == H.HandlerID) { found = true; break; }
        }
        if (!found) MissingDedicatedHandlers.Add(H.HandlerID);
      }
      if (MissingDedicatedHandlers.Num() > 0)
      {
        // Assign the first missing dedicated handler (sequential / simple fallback)
        uint32 HandlerToAssign = MissingDedicatedHandlers[0];
        C->HandlersByChannel[dc] = HandlerToAssign;
        // update or create per-datachannel context (use central map if available)
        TSharedPtr<DataChannelCtx> ctxptr = GetDataChannelContext(dc);
        if (!ctxptr.IsValid()) {
          // create and register a new context
          ctxptr = MakeShared<DataChannelCtx>();
          ctxptr->Streamer = this;
          ctxptr->ConnectionID = C->ConnectionID;
          ctxptr->HandlerID = HandlerToAssign;
          ctxptr->ConnRaw = C.Get();
          ctxptr->PeerPC = pc;
          // store in central container
          AddDataChannelContext(dc, ctxptr);
          // attach raw ctx pointer to datachannel for C-level callbacks
          rtcSetUserPointer(dc, ctxptr.Get());
          UE_LOG(LogTemp, Warning, TEXT("HandlePcDataChannelCreated: Created central ctx %p for dc=%d assigning handler=%u"), ctxptr.Get(), dc, HandlerToAssign);
        } else {
          DataChannelCtx* ctx = ctxptr.Get();
          UE_LOG(LogTemp, Warning, TEXT("HandlePcDataChannelCreated: Found central ctx %p for dc=%d assigning handler=%u"), ctx, dc, HandlerToAssign);
          ctx->ConnectionID = C->ConnectionID;
          ctx->HandlerID = HandlerToAssign;
          ctx->ConnRaw = C.Get();
          ctx->PeerPC = pc;
          // ensure user pointer references the ctx for easier diagnostics
          rtcSetUserPointer(dc, ctx);
        }
        UE_LOG(LogTemp, Warning, TEXT("Synavis(member): Associated incoming datachannel %d label='%s' -> Handler %u (conn %d) by dedicated-channel heuristic"), dc, *GetDataChannelLabelSafe(dc), HandlerToAssign, C->ConnectionID);
        RTCReport();
        return;
      }

      // Otherwise leave unassociated and let the generic handler process it.
      UE_LOG(LogTemp, Verbose, TEXT("Synavis(member): Incoming datachannel %d label='%s' not associated (conn %d)"), dc, *GetDataChannelLabelSafe(dc), C->ConnectionID);
      RTCReport();
      return;
    }
  }
  UE_LOG(LogTemp, Warning, TEXT("Synavis(member): Could not find connection for PC %d to associate datachannel %d"), pc, dc);
  RTCReport();
}



TFuture<TArray<FColor>> USynavisStreamer::EnqueueRGBReadbackFromRenderTarget(UTextureRenderTarget2D* RenderTarget)
{
  TPromise<TArray<FColor>> Promise;
  TFuture<TArray<FColor>> Future = Promise.GetFuture();
  TWeakObjectPtr<UTextureRenderTarget2D> WeakRenderTarget(RenderTarget);

  AsyncTask(ENamedThreads::GameThread, [WeakRenderTarget, Promise = MoveTemp(Promise)]() mutable {
    TArray<FColor> PixelData;

    UTextureRenderTarget2D* RT = WeakRenderTarget.Get();
    if (!RT)
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: RGB readback skipped because render target is no longer valid"));
      Promise.SetValue(MoveTemp(PixelData));
      return;
    }

    FTextureRenderTargetResource* RTResource = RT->GameThread_GetRenderTargetResource();
    if (!RTResource)
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: Failed to get render target resource for RGB readback"));
      Promise.SetValue(MoveTemp(PixelData));
      return;
    }

    FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
    ReadFlags.SetLinearToGamma(false);

    if (!RTResource->ReadPixels(PixelData, ReadFlags))
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: RGB readback failed while reading pixels"));
      PixelData.Reset();
    }

    Promise.SetValue(MoveTemp(PixelData));
  });

  return Future;
}

bool USynavisStreamer::BroadcastText(int32 HandlerId, const FString& Text)
{
  bool Sent = false;
  for (const auto& Pair : Connections)
  {
    int32 ConnectionPlayerID = Pair.Key;
    if (SendTextToConnection(HandlerId, ConnectionPlayerID, Text))
    {
      Sent = true;
    }
  }
  return Sent;
}

bool USynavisStreamer::BroadcastBinary(int32 HandlerId, const TArray<uint8>& Data)
{
  bool Sent = false;
  for (const auto& Pair : Connections)
  {
    int32 ConnectionPlayerID = Pair.Key;
    if (SendBinaryToConnection(HandlerId, ConnectionPlayerID, Data))
    {
      Sent = true;
    }
  }
  return Sent;
}

bool USynavisStreamer::SendTextViaSystemChannel(const FString& Text)
{
  if (SystemDataChannel != 0 && rtcIsOpen(SystemDataChannel))
  {
    FTCHARToUTF8 Utf8(*Text);
    int sendRes = rtcSendMessage(SystemDataChannel, Utf8.Get(), - (Utf8.Length() + 1));
    if (sendRes == RTC_ERR_SUCCESS)
    {
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: Sent text via system channel size=%d"), Utf8.Length());
      return true;
    }
    else
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcSendMessage returned %d when sending text via system channel"), sendRes);
    }
  }
  else
  {
    UE_LOG(LogTemp, Warning, TEXT("Synavis: SystemDataChannel not open or invalid"));
  }
  return false;
}

bool USynavisStreamer::SendBinaryViaSystemChannel(const TArray<uint8>& Data)
{
  if (SystemDataChannel != 0 && rtcIsOpen(SystemDataChannel))
  {
    int sendRes = rtcSendMessage(SystemDataChannel, reinterpret_cast<const char*>(Data.GetData()), static_cast<int>(Data.Num()));
    if (sendRes == RTC_ERR_SUCCESS)
    {
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: Sent binary via system channel size=%d"), Data.Num());
      return true;
    }
    else
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcSendMessage returned %d when sending binary via system channel"), sendRes);
    }
  }
  else
  {
    UE_LOG(LogTemp, Warning, TEXT("Synavis: SystemDataChannel not open or invalid"));
  }
  return false;
}

void await_state(auto returns_true_if_met, double yield_time = 0.01)
{
  while (!returns_true_if_met())
  {
    FPlatformProcess::Sleep(yield_time);
  }
}

#define AWAIT_STATE(condition) await_state([this,&]() noexcept { return (condition); })
#define AWAIT_STATE_CAPTURE(condition, capture) await_state([capture]() noexcept { return (condition); })

void USynavisStreamer::ConnectionNegotiationThread(TSharedPtr<FSynavisConnection> Connection)
{
  // flow:
  int res = 0;

  // 1. initialize state and make sure we are at NoConnection
  auto& ConnState = Connection->State;
  auto ConnID = Connection->PeerConnection;
  if (ConnID < 0)
  {
    // Create a PeerConnection via C API and register C callbacks
    rtcConfiguration cfg{}; // default-initialized configuration
    // Apply configurable max message size and MTU if provided in UPROPERTYs
    if (this->MaxMessageSize > 0) cfg.maxMessageSize = this->MaxMessageSize;
    if (this->Mtu > 0) cfg.mtu = this->Mtu;
    UE_LOG(LogTemp, Verbose, TEXT("Synavis: Creating PeerConnection with cfg.maxMessageSize=%d cfg.mtu=%d"), cfg.maxMessageSize, cfg.mtu);
    int pcid = rtcCreatePeerConnection(&cfg);
    if (pcid <= 0)
    {
      UE_LOG(LogTemp, Error, TEXT("Synavis: rtcCreatePeerConnection failed (rc=%d) for player %d"), pcid, PlayerID);
      return;
    }
    Connection->PeerConnection = pcid;

    // Attach user pointer so callbacks can find this USynavisStreamer instance
    rtcSetUserPointer(Connection->PeerConnection, this);
    rtcSetLocalDescriptionCallback(Connection->PeerConnection, Synavis_Rtc_OnPcLocalDescription);
    rtcSetLocalCandidateCallback(Connection->PeerConnection, Synavis_Rtc_OnPcLocalCandidate);
    rtcSetGatheringStateChangeCallback(Connection->PeerConnection, Synavis_Rtc_OnPcGatheringStateChange);
    rtcSetDataChannelCallback(Connection->PeerConnection, Synavis_Rtc_OnPcDataChannel);
    rtcSetTrackCallback(Connection->PeerConnection, Synavis_Rtc_OnPcTrack);
    rtcSetStateChangeCallback(Connection->PeerConnection, Synavis_Rtc_OnPcStateChange);
    rtcSetIceStateChangeCallback(Connection->PeerConnection, Synavis_Rtc_OnPcIceStateChange);
    // Notify on signaling state changes so we can apply queued remote answers
    rtcSetSignalingStateChangeCallback(Connection->PeerConnection, Synavis_Rtc_OnPcSignalingStateChange);
    // Media interceptor: allow passing opaque messages into libdatachannel's media pipeline
  }

  ConnState = EPeerState::SourceRegistration;
  

  // 2. all registrations of tracks and similar are done here, freezing a certain state of handlers
  auto handlers = RegisteredDataHandlers; // copy to freeze state
  
  // Create outgoing send-only tracks for any registered handlers that have video sources.
  for (const FSynavisHandler& HandlerCopy : RegisteredDataHandlers)
  {
    UE_LOG(LogTemp, Verbose, TEXT("Synavis: Checking handler %d for video track creation (video=%d)"), HandlerCopy.HandlerID, HandlerCopy.Video ? 1 : 0);
    if (HandlerCopy.Video)
    {
      rtcTrackInit tinit{};
      tinit.direction = RTC_DIRECTION_SENDONLY;
      tinit.codec = RTC_CODEC_VP9;
      tinit.payloadType = 96;
      tinit.ssrc = GetNextSSRC();

      FString MsidF = FString::Printf(TEXT("synavis-%d-%d"), HandlerCopy.HandlerID, Connection->ConnectionID);
      FString TrackIdF = FString::Printf(TEXT("track-%u-%d"), HandlerCopy.HandlerID, Connection->ConnectionID);
      FString NameF = TrackIdF;  // or customize, e.g., "video"

      auto* MsidUtf8 = Connection->AddPersistentUtf8(MsidF);
      auto* TrackUtf8 = Connection->AddPersistentUtf8(TrackIdF);
      auto* NameUtf8 = Connection->AddPersistentUtf8(NameF);

      tinit.msid = MsidUtf8->GetData();
      tinit.trackId = TrackUtf8->GetData();
      tinit.name = NameUtf8->GetData();
      tinit.mid = NameUtf8->GetData();  // Reuse name as mid
      tinit.profile = nullptr;

      UE_LOG(LogTemp, Log, TEXT("Synavis: [RTC-SEQ] #%d About to call rtcAddTrackEx for handler %d on pc %d (conn=%d)"), ++GRtcSequenceCounter, HandlerCopy.HandlerID, Connection->PeerConnection, Connection->ConnectionID);
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: rtcTrackInit fields before rtcAddTrackEx: ssrc=%u msid=%s trackId=%s name=%s"), tinit.ssrc, ANSI_TO_TCHAR(tinit.msid ? tinit.msid : ""), ANSI_TO_TCHAR(tinit.trackId ? tinit.trackId : ""), ANSI_TO_TCHAR(tinit.name ? tinit.name : ""));
      int trid = rtcAddTrackEx(Connection->PeerConnection, &tinit);
      UE_LOG(LogTemp, Log, TEXT("Synavis: [RTC-SEQ] #%d rtcAddTrackEx returned %d for handler %d on pc %d"), GRtcSequenceCounter.load(), trid, HandlerCopy.HandlerID, Connection->PeerConnection);
      if (trid > 0)
      {
        Connection->TracksByHandler.Add(HandlerCopy.HandlerID, trid);
        // Also update the authoritative handler instance so handler-level lookups reflect the new per-connection mapping
        FSynavisHandler* storedHandler = GetHandlerById(HandlerCopy.HandlerID);
        if (storedHandler)
        {
          storedHandler->VideoTracksByConnection.Add(Connection->ConnectionID, trid);
        }
        UE_LOG(LogTemp, Log, TEXT("Synavis: Created send-only track %d for handler %d on pc %d"), trid, HandlerCopy.HandlerID, Connection->PeerConnection);

        // NOTE: Local description will be generated when needed via signalling flow

        char trDescBuf[2048] = {0};
        int trDescLen = rtcGetTrackDescription(trid, trDescBuf, static_cast<int>(sizeof(trDescBuf)));
        if (trDescLen > 0)
        {
          UE_LOG(LogTemp, Verbose, TEXT("Synavis: rtcGetTrackDescription returned %d for track %d immediately after creation:\n%s"), trDescLen, trid, ANSI_TO_TCHAR(trDescBuf));
          // Parse the returned description once to extract the assigned SSRC
          const char* key = "a=ssrc:";
          const char* found = strstr(trDescBuf, key);
          if (found)
          {
            const char* numstart = found + strlen(key);
            char* endptr = nullptr;
            unsigned long parsed = strtoul(numstart, &endptr, 10);
            if (parsed > 0 && SendoffHandler)
            {
              SendoffHandler->RegisterTrackSsrc(trid, static_cast<uint32>(parsed));
            }
          }
        }
        else
        {
          UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcGetTrackDescription returned %d for track %d immediately after creation"), trDescLen, trid);
        }
        // Renegotiation will be triggered AFTER all tracks are added (see below)
      }
      else
      {
        UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcAddTrackEx failed for handler %d on pc %d (rc=%d)"), HandlerCopy.HandlerID, Connection->PeerConnection, trid);
      }
    }
  }

  

  // Create a per-connection data channel for control/messages using the C API
  std::string channelName = std::string("synavis-data-") + std::to_string(PlayerID);
  int dcid = rtcCreateDataChannel(Connection->PeerConnection, channelName.c_str());
  if (dcid > 0)
  {
    // Record datachannel id on the shared connection object
    Connection->DataChannel = dcid;
    TSharedPtr<DataChannelCtx> sysCtx = MakeShared<DataChannelCtx>();
    sysCtx->Streamer = this;
    sysCtx->ConnectionID = PlayerID;
    sysCtx->HandlerID = 0;
    sysCtx->ConnRaw = Conn.Get();
    rtcSetUserPointer(dcid, sysCtx.Get());
    if (DataChannelContexts) DataChannelContexts->Add(dcid, sysCtx);
    UE_LOG(LogTemp, Verbose, TEXT("Synavis: Allocated system DataChannelCtx %p for dc=%d (player=%d) [CreateConnectionForPlayer]"), sysCtx.Get(), dcid, PlayerID);
    UE_LOG(LogTemp, Warning, TEXT("Synavis: Created system DC id=%d label='%s' userPtr=%p ctx=%p conn=%d"), dcid, *GetDataChannelLabelSafe(dcid), rtcGetUserPointer(dcid), sysCtx.Get(), PlayerID);
    rtcSetMessageCallback(dcid, Synavis_Rtc_DataChannel_OnMessage);
    rtcSetOpenCallback(dcid, Synavis_Rtc_DataChannel_OnOpen);
    rtcSetClosedCallback(dcid, Synavis_Rtc_DataChannel_OnClosed);
    rtcSetErrorCallback(dcid, Synavis_Rtc_DataChannel_OnError);
  }

  // Create per-handler dedicated datachannels for any registered handlers that requested them
  for (const FSynavisHandler& HandlerCopy : RegisteredDataHandlers)
  {
    if (HandlerCopy.WantsDedicatedChannel && HandlerCopy.AcceptsInboundMessages)
    {
      std::string hname = std::string("synavis-handler-") + std::to_string(HandlerCopy.HandlerID) + std::string("-") + std::to_string(PlayerID);
      int hdc = rtcCreateDataChannel(Connection->PeerConnection, hname.c_str());
      if (hdc > 0)
      {
        TSharedPtr<DataChannelCtx> hctx = MakeShared<DataChannelCtx>();
        hctx->Streamer = this;
        hctx->ConnectionID = PlayerID;
        hctx->HandlerID = HandlerCopy.HandlerID;
        hctx->ConnRaw = Connection.Get();
        rtcSetUserPointer(hdc, hctx.Get());
        if (DataChannelContexts) DataChannelContexts->Add(hdc, hctx);
        UE_LOG(LogTemp, Verbose, TEXT("Synavis: Allocated handler DataChannelCtx %p for dc=%d (handler=%u player=%d) [CreateConnectionForPlayer]"), hctx.Get(), hdc, HandlerCopy.HandlerID, PlayerID);
        UE_LOG(LogTemp, Warning, TEXT("Synavis: Created handler DC id=%d label='%s' userPtr=%p ctx=%p conn=%d handler=%u"), hdc, *GetDataChannelLabelSafe(hdc), rtcGetUserPointer(hdc), hctx.Get(), PlayerID, HandlerCopy.HandlerID);
        rtcSetMessageCallback(hdc, Synavis_Rtc_DataChannel_OnMessage);
        rtcSetOpenCallback(hdc, Synavis_Rtc_DataChannel_OnOpen);
        rtcSetClosedCallback(hdc, Synavis_Rtc_DataChannel_OnClosed);
        rtcSetErrorCallback(hdc, Synavis_Rtc_DataChannel_OnError);
        Connection->HandlersByChannel[hdc] = HandlerCopy.HandlerID;
        UE_LOG(LogTemp, Log, TEXT("Synavis: Created per-handler datachannel %d for handler %u on pc %d"), hdc, HandlerCopy.HandlerID, Conn->PeerConnection);
      }
    }
  }

  ConnState = EPeerState::LocalDescription;

  // call setLocalDescription for the connection
  res = rtcSetLocalDescription(Connection->PeerConnection, "offer");

  AWAIT_STATE_CAPTURE(c->SDP.Len() > 0, c=Connection.Get());

  // communicate local SDP
  this->CommunicateSDPForConnection(*Connection);

  ConnState = EPeerState::OfferSent;
  Connection->SDP.Reset();

  AWAIT_STATE_CAPTURE(c->SDP.Len() > 0, c=Connection.Get());

  // we have the remote SDP
  res = rtcSetRemoteDescription(Connection->PeerConnection, TCHAR_TO_ANSI(*Connection->SDP));

  UE_LOG(LogTemp, Log, TEXT("Synavis: rtcSetRemoteDescription returned %d for conn %d"), res, Connection->ConnectionID);
  

  // Wait for remote description to come in.
  // while not all channels and tracks are open, we could still be receiving remoteICE
  while (Connection->DataChannel.state != ETransportState::OPEN || std::any_of(Connection->TracksByHandler.begin(), Connection->TracksByHandler.end(), [](const auto& pair) { return !rtcIsOpen(pair.Value); }))
  {
    AWAIT_STATE(Connection->ICE.Num() > 0);
    // if we have ICE candidates, add them to the connection
    while (!Connection->ICE.IsEmpty())
    {
      FString IceCandidate;
      if (Connection->ICE.Dequeue(IceCandidate))
      {
        UE_LOG(LogTemp, Verbose, TEXT("Synavis: Adding ICE candidate for conn %d: %s"), Connection->ConnectionID, *IceCandidate);
        this->AddRemoteCandidateForConnection(*Connection, IceCandidate);
      }
    }

  }

 // if we exited the above loop, we are open and can close this thread
 ConnState = EPeerState::Connected;
 
 return;
}

void USynavisStreamer::AddRemoteCandidateForConnection(FSynavisConnection& Conn, const FString& Candidate)
{
  if (Conn.PeerConnection <= 0)
  {
    UE_LOG(LogTemp, Warning, TEXT("Synavis: Cannot add remote candidate for conn %d because PeerConnection is invalid"), Conn.ConnectionID);
    return;
  }
  
  auto CandAnsi = StringCast<ANSICHAR>(*Candidate);
  std::string scand(CandAnsi.Get(), CandAnsi.Length());
  int addRes = rtcAddRemoteCandidate(Conn.PeerConnection, scand.c_str(), nullptr);
  if (addRes != RTC_ERR_SUCCESS)
  {
    UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcAddRemoteCandidate returned %d for conn %d"), addRes, Conn.ConnectionID);
  }
  else
  {
    UE_LOG(LogTemp, Log, TEXT("Synavis: Registered remote candidate for conn %d"), Conn.ConnectionID);
  }
} 

