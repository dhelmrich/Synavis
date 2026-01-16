#include "SynavisStreamer.h"
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
#if 0
// C++ API headers intentionally omitted to avoid C++ ABI crossing in this module.
// If you need the C++ API, include <rtc/rtc.hpp> etc. and adapt usage accordingly.
#include <rtc/rtc.hpp>
#include <rtc/websocket.hpp>
#include <rtc/rtppacketizer.hpp>
#include <rtc/rtppacketizationconfig.hpp>
#include <rtc/frameinfo.hpp>
#include <rtc/datachannel.hpp>
#include <rtc/configuration.hpp>
#endif
#if defined(LIBAV_AVAILABLE)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>
#include <libavutil/buffer.h>
}
#endif

static USynavisStreamer* GGlobalStreamer = nullptr;
static FCriticalSection GGlobalStreamerMutex;

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
    int dc = kv.first;
    if (dc)
    {
      rtcClose(dc);
      rtcDeleteDataChannel(dc);
    }
  }

  // Delete any created tracks for this connection
  for (auto& kv : Conn->TracksByHandler)
  {
    int tr = kv.second;
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
    int dc = kv.first;
    if (dc)
    {
      rtcClose(dc);
      rtcDeleteDataChannel(dc);
    }
  }

  // Delete any created tracks for this connection
  for (auto& kv : Conn->TracksByHandler)
  {
    int tr = kv.second;
    if (tr)
    {
      rtcDeleteTrack(tr);
    }
  }


  // Finally remove from map; ConnShared keeps the object alive until function exit.
  Connections.Remove(PlayerID);
  UE_LOG(LogTemp, Log, TEXT("Synavis: Teardown complete for connection %d"), PlayerID);
}
// C-style free callback for av_buffer_create when we allocated the memory via av_malloc
static void AvFreeOpaque(void* opaque, uint8_t* data)
{
  if (opaque) av_free(opaque);
}

// C-style free callback for av_buffer_create when the buffer wraps an FRHIGPUTextureReadback.
// The opaque is expected to be a pointer to ReadbackFreeCtx (defined locally where used),
// but we forward-declare a minimal struct here to avoid include cycles.
struct __ReadbackFreeCtx { FRHIGPUTextureReadback* RB; };
static void AvFreeReadback(void* opaque, uint8_t* data)
{
  __ReadbackFreeCtx* ctx = reinterpret_cast<__ReadbackFreeCtx*>(opaque);
  if (!ctx) return;
  FRHIGPUTextureReadback* RB = ctx->RB;
  // Unlock/delete must run on render thread
  ENQUEUE_RENDER_COMMAND(Synavis_FreeReadbackFromAVBuf)([RB](FRHICommandListImmediate& RHICmdList)
    {
      if (RB) { RB->Unlock(); delete RB; }
    });
  delete ctx;
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
    UE_LOG(LogActor, Error, TEXT("Synavis LibDataChannel: %s"), ANSI_TO_TCHAR(message));
    break;
  case RTC_LOG_WARNING:
    UE_LOG(LogActor, Warning, TEXT("Synavis LibDataChannel: %s"), ANSI_TO_TCHAR(message));
    break;
  case RTC_LOG_INFO:
  case RTC_LOG_DEBUG:
  case RTC_LOG_VERBOSE:
  default:
    UE_LOG(LogActor, Verbose, TEXT("DEBUG Synavis LibDataChannel: %s"), ANSI_TO_TCHAR(message));
    break;
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
// Fixes: m=video 9 → m=video 0 (rejected tracks), BUNDLE mids → 0 only,
// remove a=group:LS, ensure CRLF. Call before rtcSetRemoteDescription(pc, munged.c_str(), "answer").
static FString MungSDPForLibdatachannel(const FString& RawSdp)
{
  FString S = RawSdp.Replace(TEXT("\n"), TEXT("\r\n"));
  TArray<FString> Lines;
  S.ParseIntoArrayLines(Lines, true);

  for (int i = 0; i < Lines.Num(); ++i)
  {
    FString& Line = Lines[i];
    // Fix ONLY port: m=video 09 → m=video 9
    if (Line.Left(10) == TEXT("m=video 09")) Line = TEXT("m=video 9") + Line.Mid(10);
    // BUNDLE → 0 track-1-101 track-2-101 to match local
    if (Line.Contains(TEXT("a=group:BUNDLE ")) && !Line.Contains(TEXT("track-")))
    {
      Line = TEXT("a=group:BUNDLE 0 track-1-101 track-2-101");
    }
  }
  Lines.RemoveAll([](const FString& L){ return L.StartsWith(TEXT("a=group:LS")); });
  FString Fixed;
  for (const FString& L : Lines) Fixed += L + TEXT("\r\n");
  Fixed.TrimEndInline();
  return Fixed;
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
    AsyncTask(ENamedThreads::GameThread, [self]() { self->HandleSignallingOpen(); });
  }

  void Synavis_Rtc_OnClosed(int id, void* user_ptr)
  {
    USynavisStreamer* self = nullptr;
    {
      FScopeLock lock(&GGlobalStreamerMutex);
      self = GGlobalStreamer;
    }
    if (!self) return;
    AsyncTask(ENamedThreads::GameThread, [self]() { self->HandleSignallingClose(); });
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
    AsyncTask(ENamedThreads::GameThread, [self, s]() { self->HandleSignallingError(s); });
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
      AsyncTask(ENamedThreads::GameThread, [self, s]() {
        self->HandleSignallingMessage(std::variant<TArray<uint8>, std::string>(s));
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
      AsyncTask(ENamedThreads::GameThread, [self, b]() mutable {
        self->HandleSignallingMessage(std::variant<TArray<uint8>, std::string>(b));
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
  AsyncTask(ENamedThreads::GameThread, [self, pc, s, t]() {
    self->HandlePcLocalDescriptionCallback(pc, s.c_str(), t.c_str());
  });
}

void Synavis_Rtc_OnPcLocalCandidate(int pc, const char* cand, const char* mid, void* user_ptr)
{
  USynavisStreamer* self = nullptr;
  {
    FScopeLock lock(&GGlobalStreamerMutex);
    self = GGlobalStreamer;
  }
  if (!self) return;
  AsyncTask(ENamedThreads::GameThread, [self, pc, cand = cand ? std::string(cand) : std::string(), mid = mid ? std::string(mid) : std::string()]() {
    UE_LOG(LogTemp, Verbose, TEXT("Synavis: PC %d local candidate callback (mid=%s)"), pc, ANSI_TO_TCHAR(mid.c_str()));
  });
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
      UE_LOG(LogTemp, Warning, TEXT("[%0.6f] PcDataChannel: rtcSetUserPointer dc=%d newUser=%p ctx=%p streamer=%p"), FPlatformTime::Seconds(), dc, rtcGetUserPointer(dc), ctx.Get(), self);
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
  UE_LOG(LogTemp, Warning, TEXT("[%0.6f] DC_OnOpen cthread dc=%d label='%s' userPtr=%p"), FPlatformTime::Seconds(), id, *GetDataChannelLabelSafe(id), rtcGetUserPointer(id));
  AsyncTask(ENamedThreads::GameThread, [streamer, id]() {
    TSharedPtr<DataChannelCtx> ctx = streamer->GetDataChannelContext(id);
    DataChannelCtx* p = ctx.Get();
    UE_LOG(LogTemp, Warning, TEXT("[%0.6f] DC_OnOpen game dc=%d label='%s' ctx=%p conn=%d handler=%u userPtr=%p"), FPlatformTime::Seconds(), id, *GetDataChannelLabelSafe(id), p, p ? p->ConnectionID : 0, p ? p->HandlerID : 0, rtcGetUserPointer(id));
    streamer->HandleDataChannelOpenCallback(id);
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
  UE_LOG(LogTemp, Warning, TEXT("[%0.6f] DC_OnClosed cthread dc=%d userPtr=%p"), FPlatformTime::Seconds(), id, rtcGetUserPointer(id));
  AsyncTask(ENamedThreads::GameThread, [streamer, id]() {
    TSharedPtr<DataChannelCtx> ctx = streamer->GetDataChannelContext(id);
    DataChannelCtx* p = ctx.Get();
    UE_LOG(LogTemp, Warning, TEXT("[%0.6f] DC_OnClosed game dc=%d ctx=%p conn=%d handler=%u userPtr=%p"), FPlatformTime::Seconds(), id, p, p ? p->ConnectionID : 0, p ? p->HandlerID : 0, rtcGetUserPointer(id));
    streamer->HandleDataChannelClosedCallback(id);
  });
}

void Synavis_Rtc_DataChannel_OnError(int id, const char* error, void* user_ptr)
{
  USynavisStreamer* self = reinterpret_cast<USynavisStreamer*>(user_ptr);
  UE_LOG(LogTemp, Warning, TEXT("[%0.6f] DC_OnError cthread dc=%d userPtr=%p err=%s"), FPlatformTime::Seconds(), id, rtcGetUserPointer(id), error ? ANSI_TO_TCHAR(error) : TEXT("<null}"));
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
    UE_LOG(LogTemp, Warning, TEXT("[%0.6f] Synavis: DataChannel %d error (game): %s ctx=%p conn=%d handler=%u userPtr=%p"), FPlatformTime::Seconds(), id, ANSI_TO_TCHAR(s.c_str()), (void*)CapturedConn, SavedConnId, SavedHandlerId, rtcGetUserPointer(id));
    UE_LOG(LogTemp, Error, TEXT("Synavis: DataChannel %d error: %s"), id, ANSI_TO_TCHAR(s.c_str()));
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
    UE_LOG(LogTemp, Warning, TEXT("[%0.6f] DC_OnMessage cthread dc=%d label='%s' no global streamer userPtr=%p size=%d"), FPlatformTime::Seconds(), id, *GetDataChannelLabelSafe(id), rtcGetUserPointer(id), size);
    return;
  }

  UE_LOG(LogTemp, Warning, TEXT("[%0.6f] DC_OnMessage cthread dc=%d label='%s' userPtr=%p size=%d"), FPlatformTime::Seconds(), id, *GetDataChannelLabelSafe(id), rtcGetUserPointer(id), size);
  if (size < 0)
  {
    std::string s = data ? std::string(data) : std::string();
    AsyncTask(ENamedThreads::GameThread, [streamer, id, s]() {
      TSharedPtr<DataChannelCtx> ctx = streamer->GetDataChannelContext(id);
      DataChannelCtx* p = ctx.Get();
      UE_LOG(LogTemp, Warning, TEXT("[%0.6f] DC_OnMessage game dc=%d label='%s' ctx=%p conn=%d handler=%u userPtr=%p len=%d isText=1"), FPlatformTime::Seconds(), id, *GetDataChannelLabelSafe(id), p, p ? p->ConnectionID : 0, p ? p->HandlerID : 0, rtcGetUserPointer(id), (int)s.size());
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
        UE_LOG(LogTemp, Warning, TEXT("[%0.6f] DC_OnMessage game dc=%d label='%s' ctx=%p conn=%d handler=%u userPtr=%p len=%d isText=1"), FPlatformTime::Seconds(), id, *GetDataChannelLabelSafe(id), p, p ? p->ConnectionID : 0, p ? p->HandlerID : 0, rtcGetUserPointer(id), (int)s.size());
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
        UE_LOG(LogTemp, Warning, TEXT("[%0.6f] DC_OnMessage game dc=%d label='%s' ctx=%p conn=%d handler=%u userPtr=%p len=%d isText=0"), FPlatformTime::Seconds(), id, *GetDataChannelLabelSafe(id), p, p ? p->ConnectionID : 0, p ? p->HandlerID : 0, rtcGetUserPointer(id), b.Num());
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
    auto it = Conn->HandlersByChannel.find(dc);
    if (it != Conn->HandlersByChannel.end())
    {
      foundInfo.RawConn = Conn.Get();
      foundInfo.ConnectionID = Conn->ConnectionID;
      foundInfo.HandlerID = it->second;
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
  AsyncTask(ENamedThreads::GameThread, [self, pc, tr]() {
    UE_LOG(LogTemp, Verbose, TEXT("Synavis: PC %d track event for track %d"), pc, tr);
    // Dump low-level track info for diagnostics
    if (UE_GET_LOG_VERBOSITY(LogTemp) >= ELogVerbosity::Verbose)
    {
      bool open = rtcIsOpen(tr);
      char descBuf[2048] = {0};
      int got = rtcGetTrackDescription(tr, descBuf, static_cast<int>(sizeof(descBuf)));
      char midBuf[256] = {0};
      int gotmid = rtcGetTrackMid(tr, midBuf, static_cast<int>(sizeof(midBuf)));
      rtcDirection dir = RTC_DIRECTION_UNKNOWN;
      rtcGetTrackDirection(tr, &dir);
      const char* dirStr = "unknown";
      switch (dir)
      {
      case RTC_DIRECTION_SENDONLY: dirStr = "sendonly"; break;
      case RTC_DIRECTION_RECVONLY: dirStr = "recvonly"; break;
      case RTC_DIRECTION_SENDRECV: dirStr = "sendrecv"; break;
      case RTC_DIRECTION_INACTIVE: dirStr = "inactive"; break;
      default: dirStr = "unknown"; break;
      }
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: Track %d open=%d direction=%s desc_len=%d mid=%s"), tr, open ? 1 : 0, ANSI_TO_TCHAR(dirStr), got, gotmid > 0 ? ANSI_TO_TCHAR(midBuf) : TEXT("<null>"));
      if (got > 0)
      {
        FString s = ANSI_TO_TCHAR(descBuf);
        UE_LOG(LogTemp, Verbose, TEXT("Synavis: Track %d description:\n%s"), tr, *s);
      }
    }
    // Locate the connection object for this peer connection id
    FSynavisConnection* Conn = self->GetConnectionFromPC(pc);
    if (!Conn)
    {
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: PC %d track event - connection not found"), pc);
      return;
    }

    // If this track id is already mapped to a handler, nothing to do.
    for (const auto& kv : Conn->TracksByHandler)
    {
      if (kv.second == tr) return;
    }

    // Try to associate this incoming/new track with an existing registered handler
    // Find first handler that has a video source but not yet associated
    // with a track for this connection, and associate it.
    FSynavisHandler* Found = self->FirstWithoutVideoTrack(Conn);
    if (Found)
    {
      Conn->TracksByHandler.emplace(Found->HandlerID, tr);
      Found->VideoTracksByConnection.Add(Conn->ConnectionID, tr);
      UE_LOG(LogTemp, Log, TEXT("Synavis: Associated incoming track %d with handler %d on pc %d (auto-assigned)"), tr, Found->HandlerID, pc);
    }
  });
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

  // ...
  // Allocate heap-owned container for per-datachannel contexts
  
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
  if (SignallingId != 0)
  {
    // send close first
    rtcClose(SignallingId);

    rtcDeleteWebSocket(SignallingId);
    SignallingId = 0;
  }
  if (LibAVState)
  {
    if (LibAVState->Packet)
    {
      av_packet_free(&LibAVState->Packet); LibAVState->Packet = nullptr;
    }
    if (LibAVState->Frame)
    {
      av_frame_free(&LibAVState->Frame); LibAVState->Frame = nullptr;
    }
    if (LibAVState->CodecCtx)
    {
      avcodec_free_context(&LibAVState->CodecCtx); LibAVState->CodecCtx = nullptr;
    }
    delete LibAVState; LibAVState = nullptr;
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

  // temporarily set the log to Verbose for initialization diagnostics
  GetWorld()->GetFirstPlayerController()->ConsoleCommand(TEXT("log LogTemp VeryVerbose"), true);

  // set the logging level for libdatachannel to verbose only when UE global verbosity is VeryVerbose
  if (UE_GET_LOG_VERBOSITY(LogTemp) >= ELogVerbosity::VeryVerbose || true)
  {
    rtcInitLogger(RTC_LOG_VERBOSE, Synavis_Rtc_Logger);
  }

  // sanity check: fire function
  Synavis_Rtc_Logger(RTC_LOG_INFO, "SynavisStreamer initialized");

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
  // NV12 GPU readbacks (FRHIGPUTextureReadback) for each active handler/source.
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
        FPendingNV12Readback& rec = PendingReadbacks[i];

        // Validate readbacks
        if (!rec.ReadbackY || !rec.ReadbackUV)
        {
          PendingReadbacks.RemoveAtSwap(i);
          DidWorkThisIteration = true;
          continue;
        }

        // If readbacks ready, perform zero-copy encode and send to all target tracks
        if (rec.ReadbackY->IsReady() && rec.ReadbackUV->IsReady())
        {
          UE_LOG(LogTemp, Verbose, TEXT("Synavis: Pending readback ready, starting zero-copy encode (Width=%d Height=%d)"), rec.Width, rec.Height);
          EncodeNV12ReadbackAndSend(rec.ReadbackY, rec.ReadbackUV, rec.Width, rec.Height, rec.TargetTracks);
          // EncodeNV12ReadbackAndSend takes ownership of the readbacks via AVBuffer free callbacks,
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
          FRHIGPUTextureReadback* UVrb = rec.ReadbackUV;
          ENQUEUE_RENDER_COMMAND(Synavis_CleanupReadback)([Yrb, UVrb](FRHICommandListImmediate& RHICmdList)
            {
              if (Yrb) { Yrb->Unlock(); delete Yrb; }
              if (UVrb) { UVrb->Unlock(); delete UVrb; }
            });
          PendingReadbacks.RemoveAtSwap(i);
          DidWorkThisIteration = true;
        }
      }

      // If no pending work and queue empty, we're done for this tick
      if (PendingReadbacks.Num() == 0)
  
              // Clear global pointer if this instance was registered
              {
                FScopeLock lock(&GGlobalStreamerMutex);
                if (GGlobalStreamer == this) GGlobalStreamer = nullptr;
              }
        break;

      // If we made progress this iteration, continue polling immediately; otherwise sleep briefly
      if (!DidWorkThisIteration)
      {
        FPlatformProcess::Sleep(0.001f);
      }
    }
    // Any remaining PendingReadbacks (if deadline hit) will be processed in subsequent ticks or cleaned up via age/timeouts above.
  }
}

// (Struct declarations live in the header to satisfy UHT; destructor definitions follow)

// NOTE: The previous internal WebRTC container was removed in the refactor; cleanup
// of signalling is handled by the USynavisStreamer destructor and the libdatachannel C API websocket id (SignallingId).

// VP9 RTP packetizer: quality-preserving, frame-aligned fragmentation
// Implements simplified VP9 RTP payload descriptor per RFC/draft semantics
// and fragments frames to respect MTU. Sends RTP packets over the given
// libdatachannel track id by building RTP headers + VP9 payload descriptor
// + fragment payload and calling `rtcSendMessage(trackId, packet, len)`.

struct FPacketizerState
{
  uint16_t Sequence = 0;
  uint32_t SSRC = 0;
  // MTU in bytes for this packetizer (including IP/UDP/RTP headers outside scope).
  int MTU = 1200;
  FPacketizerState() {}
};

// Maintain per-track packetizer state
static std::unordered_map<int, FPacketizerState> GPacketizers;

// Helper to get or create packetizer state for a track
static FPacketizerState& GetOrCreatePacketizer(int trackId)
{
  auto it = GPacketizers.find(trackId);
  if (it != GPacketizers.end()) return it->second;
  FPacketizerState st;
  // initialize sequence with random start
  st.Sequence = static_cast<uint16_t>(FMath::Rand());
  // generate random SSRC
  st.SSRC = static_cast<uint32_t>((FMath::Rand() << 16) ^ FMath::Rand());
  // default MTU; can be tuned later
  st.MTU = 1200;
  auto r = GPacketizers.emplace(trackId, st);
  return r.first->second;
}

// Minimal RTP header builder (no extensions)
struct FRtpHeader
{
  uint8_t V_P_X_CC; // version(2),P,X,CC
  uint8_t M_PT;     // M bit + payload type
  uint16_t Sequence;
  uint32_t Timestamp;
  uint32_t SSRC;
};

// Build VP9 payload descriptor (basic, no PictureID by default). Returns number of bytes written.
static int BuildVp9PayloadDescriptor(uint8_t* OutBuf, int OutBufLen, bool startBit, bool endBit)
{
  if (OutBufLen < 1) return 0;
  // Basic descriptor: |I|P|L|F|B|E|V|Z| (I=PictureID present etc.)
  // We'll emit a 1-byte descriptor with S (start) mapped to 'B' bit per draft
  // Layout: extended control bits not used here; set I=0 (no PictureID), P/L/F=0, B= startBit, E=endBit, V=0, Z=0
  uint8_t desc = 0;
  // B bit: in the draft the S (start) is represented by 'B' bit in descriptor
  if (startBit) desc |= (1 << 3); // set B
  if (endBit) desc |= (1 << 2);   // set E
  OutBuf[0] = desc;
  return 1;
}

// Send raw bytes over libdatachannel track id. Returns rtcSendMessage result.
static int SendNative(int trackId, const uint8_t* Data, int Len)
{
  return rtcSendMessage(trackId, reinterpret_cast<const char*>(Data), Len);
}

// New packetizer signature: include RTP timestamp and optional MTU override
static void VP9PacketizeAndSend(int trackId, const uint8_t* data, size_t size, uint32_t rtpTimestamp, int mtuOverride = 0)
{
  if (trackId == 0 || data == nullptr || size == 0) return;
  UE_LOG(LogTemp, Verbose, TEXT("Synavis: VP9PacketizeAndSend entry track=%d size=%d ts=%u mtuOverride=%d"), trackId, static_cast<int>(size), rtpTimestamp, mtuOverride);
  if (!rtcIsOpen(trackId))
  {
    UE_LOG(LogTemp, Warning, TEXT("Synavis: VP9PacketizeAndSend - track %d not open, dropping packet"), trackId);
    return;
  }

  FPacketizerState& st = GetOrCreatePacketizer(trackId);
  if (mtuOverride > 0) st.MTU = mtuOverride;

  // RTP header size (bytes) without extensions
  const int RTP_HEADER_SIZE = 12;

  // Build payload descriptor (one byte baseline); calculate available payload per RTP packet
  uint8_t payloadDescBuf[4];
  int pdLen = BuildVp9PayloadDescriptor(payloadDescBuf, sizeof(payloadDescBuf), true, true); // will be adjusted per-fragment

  int maxPayloadPerPacket = st.MTU - RTP_HEADER_SIZE - pdLen;
  if (maxPayloadPerPacket <= 0)
  {
    UE_LOG(LogTemp, Error, TEXT("Synavis: MTU %d too small for VP9 payload descriptor"), st.MTU);
    return;
  }

  // Frame-aligned fragmentation: only fragment when necessary.
  size_t offset = 0;
  bool firstFragment = true;
  while (offset < size)
  {
    size_t remaining = size - offset;
    int chunkSize = static_cast<int>(FMath::Min<size_t>(remaining, static_cast<size_t>(maxPayloadPerPacket)));

    bool isStart = firstFragment;
    bool isEnd = (offset + chunkSize) >= size;

    // Build descriptor for this fragment
    uint8_t descBuf[4];
    int thisPdLen = BuildVp9PayloadDescriptor(descBuf, sizeof(descBuf), isStart, isEnd);

    // Compose packet into a temporary buffer
    int packetLen = RTP_HEADER_SIZE + thisPdLen + chunkSize;
    TArray<uint8> packet;
    packet.SetNumUninitialized(packetLen);

    // Write RTP header directly (big-endian network order)
    uint8_t* ptr = packet.GetData();
    ptr[0] = 0x80; // Version 2, no padding, no extensions, CC=0
    ptr[1] = static_cast<uint8_t>((isEnd ? 0x80u : 0x00u) | 96u); // Marker on last packet, PT=96
    ptr[2] = static_cast<uint8_t>((st.Sequence >> 8) & 0xFF);
    ptr[3] = static_cast<uint8_t>((st.Sequence >> 0) & 0xFF);
    // Timestamp (32-bit big-endian)
    ptr[4] = static_cast<uint8_t>((rtpTimestamp >> 24) & 0xFF);
    ptr[5] = static_cast<uint8_t>((rtpTimestamp >> 16) & 0xFF);
    ptr[6] = static_cast<uint8_t>((rtpTimestamp >> 8) & 0xFF);
    ptr[7] = static_cast<uint8_t>((rtpTimestamp >> 0) & 0xFF);
    // SSRC (32-bit big-endian)
    ptr[8] = static_cast<uint8_t>((st.SSRC >> 24) & 0xFF);
    ptr[9] = static_cast<uint8_t>((st.SSRC >> 16) & 0xFF);
    ptr[10] = static_cast<uint8_t>((st.SSRC >> 8) & 0xFF);
    ptr[11] = static_cast<uint8_t>((st.SSRC >> 0) & 0xFF);

    // Copy payload descriptor
    memcpy(ptr + RTP_HEADER_SIZE, descBuf, thisPdLen);

    // Copy payload chunk
    memcpy(ptr + RTP_HEADER_SIZE + thisPdLen, data + offset, chunkSize);

    // Send packet
    int seqBefore = st.Sequence;
    int sendRes = SendNative(trackId, packet.GetData(), packetLen);
    if (sendRes != RTC_ERR_SUCCESS)
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcSendMessage returned %d when sending RTP packet to track %d (seq=%d len=%d offset=%d chunk=%d)"), sendRes, trackId, seqBefore, packetLen, static_cast<int>(offset), chunkSize);
    }
    else
    {
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: Sent RTP packet to track %d seq=%d len=%d offset=%d chunk=%d ts=%u ssrc=%u"), trackId, seqBefore, packetLen, static_cast<int>(offset), chunkSize, rtpTimestamp, st.SSRC);
    }

    // Advance
    offset += chunkSize;
    firstFragment = false;
    st.Sequence = static_cast<uint16_t>(st.Sequence + 1);
  }
}

USynavisStreamer::FLibAVEncoderState::~FLibAVEncoderState()
{
  if (Packet) { av_packet_free(&Packet); Packet = nullptr; }
  if (Frame) { av_frame_free(&Frame); Frame = nullptr; }
  if (CodecCtx) { avcodec_free_context(&CodecCtx); CodecCtx = nullptr; }
}

// Public helpers for external callbacks to manage the central DataChannelCtx map
void USynavisStreamer::AddDataChannelContext(int32 DcId, TSharedPtr<DataChannelCtx> Ctx)
{
  if (!Ctx) return;
  if (DataChannelContexts) {
    FScopeLock lock(&DataChannelContextsMutex);
    DataChannelContexts->Add(DcId, Ctx);
    UE_LOG(LogTemp, Warning, TEXT("[%0.6f] AddDC dc=%d ctx=%p conn=%d handler=%d canF=0x%016llx canB=0x%016llx userPtr=%p"),
      FPlatformTime::Seconds(), DcId, Ctx.Get(), Ctx->ConnectionID, Ctx->HandlerID,
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
      UE_LOG(LogTemp, Warning, TEXT("[%0.6f] RemoveDC dc=%d removed from central map remaining=%d userPtr=%p"),
        FPlatformTime::Seconds(), DcId, DataChannelContexts->Num(), rtcGetUserPointer(DcId));
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
    if (Conn->TracksByHandler.find(H.HandlerID) == Conn->TracksByHandler.end())
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
    if (Conn->TracksByHandler.find(H.HandlerID) == Conn->TracksByHandler.end())
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
    int32 Tracks = static_cast<int32>(C->TracksByHandler.size());
    int32 Channels = static_cast<int32>(C->HandlersByChannel.size());
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
      UE_LOG(LogTemp, Log, TEXT("    TracksByHandler: handler=%u -> track=%d"), kv.first, kv.second);
      int tr = kv.second;
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
      UE_LOG(LogTemp, Log, TEXT("    HandlersByChannel: dc=%d -> handler=%u"), kv.first, kv.second);
      int dc = kv.first;
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
      UE_LOG(LogTemp, Warning, TEXT("[%0.6f] SetupDataChannel: rtcSetUserPointer dc=%d newUser=%p ctx=%p conn=%d handler=%u"), FPlatformTime::Seconds(), dcid, rtcGetUserPointer(dcid), ctx.Get(), Conn->ConnectionID, Handler.HandlerID);
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
  // Initialize persistent libav encoder state lazily and enable streaming on
  // existing connections. New connections default to bStreaming=false.
  if (!LibAVState)
  {
    LibAVState = new FLibAVEncoderState();
    LibAVState->Codec = avcodec_find_encoder(AV_CODEC_ID_VP9);
    // actual codec context will be created when first frame with size arrives
  }

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

  if (LibAVState)
  {
    FScopeLock lock(&LibAVState->Mutex);
    if (LibAVState->Packet) { av_packet_free(&LibAVState->Packet); LibAVState->Packet = nullptr; }
    if (LibAVState->Frame) { av_frame_free(&LibAVState->Frame); LibAVState->Frame = nullptr; }
    if (LibAVState->CodecCtx) { avcodec_free_context(&LibAVState->CodecCtx); LibAVState->CodecCtx = nullptr; }
    delete LibAVState;
    LibAVState = nullptr;
  }
}

void USynavisStreamer::StopStreaming(int32 ConnectionID)
{
  FSynavisConnection* Conn = FindConnectionByPlayerID(ConnectionID);
  if (!Conn)
  {
    UE_LOG(LogTemp, Warning, TEXT("StopStreaming: connection %d not found"), ConnectionID);
    return;
  }

  Conn->bStreaming = false;

  // If no connections are streaming anymore, free libav state
  if (!AnyConnectionStreaming())
  {
    if (LibAVState)
    {
      FScopeLock lock(&LibAVState->Mutex);
      if (LibAVState->Packet) { av_packet_free(&LibAVState->Packet); LibAVState->Packet = nullptr; }
      if (LibAVState->Frame) { av_frame_free(&LibAVState->Frame); LibAVState->Frame = nullptr; }
      if (LibAVState->CodecCtx) { avcodec_free_context(&LibAVState->CodecCtx); LibAVState->CodecCtx = nullptr; }
      delete LibAVState;
      LibAVState = nullptr;
    }
  }
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
  FSynavisConnection* Conn = nullptr;
  for (auto& Pair : Connections)
  {
    TSharedPtr<FSynavisConnection> maybe = Pair.Value;
    if (maybe && maybe->PeerConnection == pc) { Conn = maybe.Get(); break; }
  }
  if (!Conn) return;
  // Verbose: dump local description type and SDP for diagnosis
  FString t = type ? FString(ANSI_TO_TCHAR(type)) : FString(TEXT("<null>"));
  FString sdff = sdp ? FString(ANSI_TO_TCHAR(sdp)) : FString(TEXT("<null>"));
  UE_LOG(LogTemp, Verbose, TEXT("Synavis: LocalDescription for conn %d type=%s:\n%s"), Conn->ConnectionID, *t, *sdff);
  // When local description becomes available, send via signalling
  CommunicateSDPForConnection(*Conn);
}

void USynavisStreamer::HandlePcGatheringStateChangeCallback(int pc, int state)
{
  // state is rtcGatheringState enum as int; RTC_GATHERING_COMPLETE == 2
  if (state == 2)
  {
    FSynavisConnection* Conn = nullptr;
    for (auto& Pair : Connections)
    {
      TSharedPtr<FSynavisConnection> maybe = Pair.Value;
      if (maybe && maybe->PeerConnection == pc) { Conn = maybe.Get(); break; }
    }
    if (Conn) CommunicateSDPForConnection(*Conn);
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
          LogHexVerbose(s.c_str(), s.size(), *Prefix);
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
          LogHexVerbose(reinterpret_cast<const char*>(b.GetData()), b.Num(), *Prefix);
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
        Mappings += FString::Printf(TEXT("dc=%d->h=%u "), kv.first, kv.second);
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
    int dc = kv.first;
    int hid = kv.second;
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
      UE_LOG(LogTemp, Verbose, TEXT("  handler-channel: dc=%d -> handler=%d rtcIsOpen=%d"), kv.first, kv.second, kv.first ? rtcIsOpen(kv.first) : 0);
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
    LogHexVerbose(OutUtf8.Get(), static_cast<size_t>(OutUtf8.Length()), TEXT("OutgoingTextRawHex"));
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
  AsyncTask(ENamedThreads::GameThread, [this, dc]() {
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
      ConnShared->State = EPeerState::ChannelOpen;
      int maxMsg = rtcMaxMessageSize(dc);
      ConnShared->MaxMessageSize = static_cast<uint32>(maxMsg);
      UE_LOG(LogTemp, Log, TEXT("Synavis: DataChannel %d label='%s' opened for connection %d (max message size=%u) [resolved on game thread]"), dc, *GetDataChannelLabelSafe(dc), ConnShared->ConnectionID, ConnShared->MaxMessageSize);
      return;
    }
    // Fallback: if ConnRaw is invalid, try the numeric id (still validated)
    if (ctx->ConnectionID != 0 && Connections.Contains(ctx->ConnectionID))
    {
      FSynavisConnection* C = FindConnectionByPlayerID(ctx->ConnectionID);
      if (C)
      {
        C->State = EPeerState::ChannelOpen;
        int maxMsg = rtcMaxMessageSize(dc);
        C->MaxMessageSize = static_cast<uint32>(maxMsg);
        UE_LOG(LogTemp, Log, TEXT("Synavis: DataChannel %d label='%s' opened for connection %d (max message size=%u) [by id fallback]"), dc, *GetDataChannelLabelSafe(dc), C->ConnectionID, C->MaxMessageSize);
        return;
      }
    }
    UE_LOG(LogTemp, Warning, TEXT("Synavis: DataChannel %d label='%s' opened but owning connection not found"), dc, *GetDataChannelLabelSafe(dc));
  });
}

void USynavisStreamer::HandleDataChannelClosedCallback(int dc)
{
  // Resolve the per-datachannel context on the game thread. Do NOT free the
  // context here — the lower-level C callback path currently deletes the
  // allocation after NotifyDataChannelClosed returns. Only perform logical
  // cleanup of connection state and handler mappings.
  AsyncTask(ENamedThreads::GameThread, [this, dc]() {
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
      auto it = ConnShared->HandlersByChannel.find(dc);
      if (it != ConnShared->HandlersByChannel.end()) ConnShared->HandlersByChannel.erase(it);
      return;
    }
    // Fallback: numeric id if available
    if (ctx->ConnectionID != 0 && Connections.Contains(ctx->ConnectionID))
    {
      FSynavisConnection* C = FindConnectionByPlayerID(ctx->ConnectionID);
      if (C)
      {
        C->State = EPeerState::NoConnection;
        UE_LOG(LogTemp, Log, TEXT("Synavis: DataChannel %d label='%s' closed for connection %d (by id fallback)"), dc, *GetDataChannelLabelSafe(dc), C->ConnectionID);
        auto it = C->HandlersByChannel.find(dc);
        if (it != C->HandlersByChannel.end()) C->HandlersByChannel.erase(it);
        return;
      }
    }
    UE_LOG(LogTemp, Warning, TEXT("Synavis: DataChannel %d label='%s' closed but owning connection not found"), dc, *GetDataChannelLabelSafe(dc));
  });
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

void USynavisStreamer::CommunicateSDPs()
{
  // Communicate SDP for each active connection via signalling
  if (SignallingId == 0 || !rtcIsOpen(SignallingId))
  {
    UE_LOG(LogTemp, Warning, TEXT("Synavis: Signalling websocket not open - cannot send local SDP"));
    return;
  }

  for (const auto& Pair : Connections)
  {
    TSharedPtr<FSynavisConnection> C = Pair.Value;
    if (C) CommunicateSDPForConnection(*C);
  }
}

void USynavisStreamer::CommunicateSDPForConnection(const FSynavisConnection& Conn)
{
  if (SignallingId == 0 || !rtcIsOpen(SignallingId))
    return;

  int pc = Conn.PeerConnection;
  if (pc == 0)
    return;

  // Retrieve local description via C API
  const int BufSize = 65536;
  std::vector<char> sdpBuf(BufSize);
  int got = rtcGetLocalDescription(pc, sdpBuf.data(), BufSize);
  if (got <= 0)
    return;
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
    LogHex(OutAnsi.Get(), static_cast<size_t>(OutAnsi.Length()), TEXT("StringCast(Out) bytes"));
    std::string outcpp(OutAnsi.Get(), OutAnsi.Length());
    LogHex(outcpp.c_str(), outcpp.size(), TEXT("std::string(outcpp) bytes"));

    // Use the C API to send a text message. For text we pass a negative size according to the C API
    // convention (-(length+1)).
    int sendSize = -static_cast<int>(outcpp.size() + 1);
    int sendRes = rtcSendMessage(SignallingId, outcpp.c_str(), sendSize);
    if (sendRes != RTC_ERR_SUCCESS)
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcSendMessage returned %d when sending SDP for conn %d"), sendRes, Conn.ConnectionID);
    }
    else
    {
      UE_LOG(LogTemp, VeryVerbose, TEXT("Synavis: Sent SDP for conn %d size=%d"), Conn.ConnectionID, static_cast<int>(outcpp.size()));
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

  // Create a PeerConnection via C API and register C callbacks
  rtcConfiguration cfg{}; // default-initialized configuration
  int pcid = rtcCreatePeerConnection(&cfg);
  if (pcid <= 0)
  {
    UE_LOG(LogTemp, Error, TEXT("Synavis: rtcCreatePeerConnection failed (rc=%d) for player %d"), pcid, PlayerID);
    return;
  }
  Conn->PeerConnection = pcid;

  // Attach user pointer so callbacks can find this USynavisStreamer instance
  rtcSetUserPointer(Conn->PeerConnection, this);
  rtcSetLocalDescriptionCallback(Conn->PeerConnection, Synavis_Rtc_OnPcLocalDescription);
  rtcSetLocalCandidateCallback(Conn->PeerConnection, Synavis_Rtc_OnPcLocalCandidate);
  rtcSetGatheringStateChangeCallback(Conn->PeerConnection, Synavis_Rtc_OnPcGatheringStateChange);
  rtcSetDataChannelCallback(Conn->PeerConnection, Synavis_Rtc_OnPcDataChannel);
  rtcSetTrackCallback(Conn->PeerConnection, Synavis_Rtc_OnPcTrack);
  rtcSetStateChangeCallback(Conn->PeerConnection, Synavis_Rtc_OnPcStateChange);
  rtcSetIceStateChangeCallback(Conn->PeerConnection, Synavis_Rtc_OnPcIceStateChange);

  // Create a per-connection data channel for control/messages using the C API
  std::string channelName = std::string("synavis-data-") + std::to_string(PlayerID);
  int dcid = rtcCreateDataChannel(Conn->PeerConnection, channelName.c_str());
  if (dcid > 0)
  {
    // Record datachannel id on the shared connection object
    Conn->DataChannel = dcid;
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
      int hdc = rtcCreateDataChannel(Conn->PeerConnection, hname.c_str());
      if (hdc > 0)
      {
        TSharedPtr<DataChannelCtx> hctx = MakeShared<DataChannelCtx>();
        hctx->Streamer = this;
        hctx->ConnectionID = PlayerID;
        hctx->HandlerID = HandlerCopy.HandlerID;
        hctx->ConnRaw = Conn.Get();
        rtcSetUserPointer(hdc, hctx.Get());
        if (DataChannelContexts) DataChannelContexts->Add(hdc, hctx);
        UE_LOG(LogTemp, Verbose, TEXT("Synavis: Allocated handler DataChannelCtx %p for dc=%d (handler=%u player=%d) [CreateConnectionForPlayer]"), hctx.Get(), hdc, HandlerCopy.HandlerID, PlayerID);
        UE_LOG(LogTemp, Warning, TEXT("Synavis: Created handler DC id=%d label='%s' userPtr=%p ctx=%p conn=%d handler=%u"), hdc, *GetDataChannelLabelSafe(hdc), rtcGetUserPointer(hdc), hctx.Get(), PlayerID, HandlerCopy.HandlerID);
        rtcSetMessageCallback(hdc, Synavis_Rtc_DataChannel_OnMessage);
        rtcSetOpenCallback(hdc, Synavis_Rtc_DataChannel_OnOpen);
        rtcSetClosedCallback(hdc, Synavis_Rtc_DataChannel_OnClosed);
        rtcSetErrorCallback(hdc, Synavis_Rtc_DataChannel_OnError);
        Conn->HandlersByChannel[hdc] = HandlerCopy.HandlerID;
        UE_LOG(LogTemp, Log, TEXT("Synavis: Created per-handler datachannel %d for handler %u on pc %d"), hdc, HandlerCopy.HandlerID, Conn->PeerConnection);
      }
    }
  }

  // Create outgoing send-only tracks for any registered handlers that have video sources.
  for (const FSynavisHandler& HandlerCopy : RegisteredDataHandlers)
  {
    if (HandlerCopy.Video)
    {
      rtcTrackInit tinit{};
      tinit.direction = RTC_DIRECTION_SENDONLY;
      tinit.codec = RTC_CODEC_VP9;
      tinit.payloadType = 96;
      tinit.ssrc = GetNextSSRC();

      FString MsidF = FString::Printf(TEXT("synavis-%d-%d"), HandlerCopy.HandlerID, Conn->ConnectionID);
      FString TrackIdF = FString::Printf(TEXT("track-%u-%d"), HandlerCopy.HandlerID, Conn->ConnectionID);
      FString NameF = TrackIdF;  // or customize, e.g., "video"

      auto* MsidUtf8 = Conn->AddPersistentUtf8(MsidF);
      auto* TrackUtf8 = Conn->AddPersistentUtf8(TrackIdF);
      auto* NameUtf8 = Conn->AddPersistentUtf8(NameF);

      tinit.msid = MsidUtf8->GetData();
      tinit.trackId = TrackUtf8->GetData();
      tinit.name = NameUtf8->GetData();
      tinit.mid = NameUtf8->GetData();  // Reuse name as mid
      tinit.profile = nullptr;

      UE_LOG(LogTemp, Log, TEXT("Synavis: About to add send-only track for handler %d on pc %d (conn=%d)"), HandlerCopy.HandlerID, Conn->PeerConnection, Conn->ConnectionID);
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: rtcTrackInit fields before rtcAddTrackEx: ssrc=%u msid=%s trackId=%s name=%s"), tinit.ssrc, ANSI_TO_TCHAR(tinit.msid ? tinit.msid : ""), ANSI_TO_TCHAR(tinit.trackId ? tinit.trackId : ""), ANSI_TO_TCHAR(tinit.name ? tinit.name : ""));
      int trid = rtcAddTrackEx(Conn->PeerConnection, &tinit);
      if (trid > 0)
      {
        Conn->TracksByHandler.emplace(HandlerCopy.HandlerID, trid);
        // Also update the authoritative handler instance so handler-level lookups reflect the new per-connection mapping
        FSynavisHandler* storedHandler = GetHandlerById(HandlerCopy.HandlerID);
        if (storedHandler)
        {
          storedHandler->VideoTracksByConnection.Add(Conn->ConnectionID, trid);
        }
        UE_LOG(LogTemp, Log, TEXT("Synavis: Created send-only track %d for handler %d on pc %d"), trid, HandlerCopy.HandlerID, Conn->PeerConnection);

        char trDescBuf[2048] = {0};
        int trDescLen = rtcGetTrackDescription(trid, trDescBuf, static_cast<int>(sizeof(trDescBuf)));
        if (trDescLen > 0)
        {
          UE_LOG(LogTemp, Verbose, TEXT("Synavis: rtcGetTrackDescription returned %d for track %d immediately after creation:\n%s"), trDescLen, trid, ANSI_TO_TCHAR(trDescBuf));
        }
        else
        {
          UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcGetTrackDescription returned %d for track %d immediately after creation"), trDescLen, trid);
        }
        // Attempt an explicit renegotiation immediately so the new track appears in local SDP
        int setResAfterAdd = rtcSetLocalDescription(Conn->PeerConnection, "offer");
          if (setResAfterAdd != RTC_ERR_SUCCESS)
          {
            UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcSetLocalDescription returned %d after adding track %d on pc %d"), setResAfterAdd, trid, Conn->PeerConnection);
          }
          else
          {
            UE_LOG(LogTemp, Log, TEXT("Synavis: rtcSetLocalDescription succeeded after adding track %d on pc %d"), trid, Conn->PeerConnection);
            char localBufAfter[8192] = {0};
            int gotLocalAfter = rtcGetLocalDescription(Conn->PeerConnection, localBufAfter, static_cast<int>(sizeof(localBufAfter)));
            if (gotLocalAfter > 0)
            {
              UE_LOG(LogTemp, Verbose, TEXT("Synavis: New local SDP for pc %d (len=%d):\n%s"), Conn->PeerConnection, gotLocalAfter, ANSI_TO_TCHAR(localBufAfter));
            }
            else
            {
              UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcGetLocalDescription returned %d after renegotiation for pc %d"), gotLocalAfter, Conn->PeerConnection);
            }
            // Dump per-track descriptions after local description update
            for (const auto& kv : Conn->TracksByHandler)
            {
              int tr = kv.second;
              char trbuf[2048] = {0};
              int trlen = rtcGetTrackDescription(tr, trbuf, static_cast<int>(sizeof(trbuf)));
              if (trlen > 0)
              {
                UE_LOG(LogTemp, Verbose, TEXT("Synavis: Post-setLocalDescription track %d description (len=%d):\n%s"), tr, trlen, ANSI_TO_TCHAR(trbuf));
              }
              else
              {
                UE_LOG(LogTemp, Warning, TEXT("Synavis: Post-setLocalDescription rtcGetTrackDescription returned %d for track %d"), trlen, tr);
              }
            }
          }
      }
      else
      {
        UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcAddTrackEx failed for handler %d on pc %d (rc=%d)"), HandlerCopy.HandlerID, Conn->PeerConnection, trid);
      }
    }
  }

  // Insert into connections map before starting ICE so callbacks can find it
  Connections.Add(PlayerID, Conn);

  // Diagnostic report after creating the connection and tracks
  RTCReport();

  // Finally, start ICE gathering by requesting a local description via C API.
  // Only do this if the streamer is configured to take the first step (offerer).
  TSharedPtr<FSynavisConnection> StoredConn = Connections[PlayerID];
  if (bTakeFirstStep)
  {
    if (StoredConn)
    {
      // If negotiation is held globally, mark this connection as pending instead
      // of creating the offer immediately. StartConnectionNegotiation() will
      // trigger pending negotiations later.
      if (bHoldNegotiation)
      {
        StoredConn->PendingNegotiation = true;
        UE_LOG(LogTemp, Log, TEXT("Synavis: Holding negotiation for player %d until StartConnectionNegotiation()"), PlayerID);
      }
      else
      {
        int localRes = rtcSetLocalDescription(StoredConn->PeerConnection, "offer");
        if (localRes != RTC_ERR_SUCCESS)
        {
          UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcSetLocalDescription returned %d for player %d"), localRes, PlayerID);
        }
        else
        {
          UE_LOG(LogTemp, Log, TEXT("Synavis: rtcSetLocalDescription succeeded for player %d (pc=%d)"), PlayerID, StoredConn->PeerConnection);
          // Dump per-track descriptions after creating local offer
          for (const auto& kv : StoredConn->TracksByHandler)
          {
            int tr = kv.second;
            char trbuf[2048] = {0};
            int trlen = rtcGetTrackDescription(tr, trbuf, static_cast<int>(sizeof(trbuf)));
            if (trlen > 0)
            {
              UE_LOG(LogTemp, Verbose, TEXT("Synavis: After initial setLocal track %d description (len=%d):\n%s"), tr, trlen, ANSI_TO_TCHAR(trbuf));
            }
            else
            {
              UE_LOG(LogTemp, Warning, TEXT("Synavis: After initial setLocal rtcGetTrackDescription returned %d for track %d"), trlen, tr);
            }
          }
        }
      }
    }
  }

  UE_LOG(LogTemp, Log, TEXT("Synavis: Created connection object for player %d (pc=%d dc=%d)"), PlayerID, StoredConn ? StoredConn->PeerConnection : 0, StoredConn ? StoredConn->DataChannel : 0);
}

void USynavisStreamer::RegisterRemoteCandidateForConnection(const FJsonObject& Content, FSynavisConnection& Conn)
{
  if (!Conn.PeerConnection)
    return;

  FString candStr;
  FString sdpMid; int32 sdpMLineIndex = -1;
  const TSharedPtr<FJsonValue>* val = nullptr;
  if (Content.HasField(TEXT("candidate")))
  {
    const TSharedPtr<FJsonValue> CandidateVal = Content.TryGetField(TEXT("candidate"));
    if (CandidateVal.IsValid() && CandidateVal->Type == EJson::Object)
    {
      TSharedPtr<FJsonObject> Inner = CandidateVal->AsObject();
      if (Inner.IsValid() && Inner->HasField(TEXT("candidate")))
        candStr = Inner->GetStringField(TEXT("candidate"));
      if (Inner.IsValid() && Inner->HasField(TEXT("sdpMid")))
        sdpMid = Inner->GetStringField(TEXT("sdpMid"));
      if (Inner.IsValid() && Inner->HasField(TEXT("sdpMLineIndex")))
        sdpMLineIndex = static_cast<int32>(Inner->GetNumberField(TEXT("sdpMLineIndex")));
    }
    else if (CandidateVal.IsValid() && CandidateVal->Type == EJson::String)
    {
      candStr = CandidateVal->AsString();
    }
  }

  if (Content.HasField(TEXT("sdpMid")) && sdpMid.IsEmpty())
  {
    sdpMid = Content.GetStringField(TEXT("sdpMid"));
  }
  if (Content.HasField(TEXT("sdpMLineIndex")) && sdpMLineIndex == -1)
  {
    sdpMLineIndex = static_cast<int32>(Content.GetNumberField(TEXT("sdpMLineIndex")));
  }

  if (candStr.IsEmpty())
  {
    UE_LOG(LogTemp, Warning, TEXT("Synavis: Received iceCandidate message with no candidate field (conn %d)"), Conn.ConnectionID);
    return;
  }

  auto CandAnsi = StringCast<ANSICHAR>(*candStr);
  LogHex(CandAnsi.Get(), static_cast<size_t>(CandAnsi.Length()), TEXT("StringCast(candStr) bytes"));
  std::string scand(CandAnsi.Get(), CandAnsi.Length());
  LogHex(scand.c_str(), scand.size(), TEXT("std::string(scand) bytes"));
  auto SmidAnsi = StringCast<ANSICHAR>(*sdpMid);
  LogHex(SmidAnsi.Get(), static_cast<size_t>(SmidAnsi.Length()), TEXT("StringCast(sdpMid) bytes"));
  std::string smid(SmidAnsi.Get(), SmidAnsi.Length());
  LogHex(smid.c_str(), smid.size(), TEXT("std::string(smid) bytes"));
  // Use C API to add remote candidate
  int addRes = rtcAddRemoteCandidate(Conn.PeerConnection, scand.c_str(), smid.c_str());
  if (addRes != RTC_ERR_SUCCESS)
  {
    UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcAddRemoteCandidate returned %d for conn %d"), addRes, Conn.ConnectionID);
  }
  else
  {
    UE_LOG(LogTemp, Log, TEXT("Synavis: Registered remote candidate for conn %d"), Conn.ConnectionID);
  }
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
      CreateConnectionForPlayer(PlayerID);
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
    if (Type.Equals(TEXT("answer"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("offer"), ESearchCase::IgnoreCase))
    {
      if (TargetPlayer == -1)
      {
        UE_LOG(LogTemp, Warning, TEXT("Synavis: SDP message missing playerId; ignoring"));
        return;
      }
      FSynavisConnection* Conn = FindConnectionByPlayerID(TargetPlayer);
      if (!Conn || !Conn->PeerConnection)
      {
        UE_LOG(LogTemp, Warning, TEXT("Synavis: Received SDP for unknown or invalid player %d"), TargetPlayer);
        return;
      }

      if (!Parsed.HasField(TEXT("sdp")))
      {
        UE_LOG(LogTemp, Warning, TEXT("Synavis: SDP message missing sdp field"));
        return;
      }
      FString sdpf = Parsed.GetStringField(TEXT("sdp"));
      if (Type.Equals(TEXT("answer"), ESearchCase::IgnoreCase))
      {
        FString munged = MungSDPForLibdatachannel(sdpf);
        UE_LOG(LogTemp, Verbose, TEXT("Synavis: Munge incoming answer SDP for player %d orig_len=%d munged_len=%d"), TargetPlayer, sdpf.Len(), munged.Len());
        sdpf = munged;
      }
      std::string sdp = TCHAR_TO_UTF8(*sdpf);
      // Use C API to set remote description
      int setRes = rtcSetRemoteDescription(Conn->PeerConnection, sdp.c_str(), TCHAR_TO_UTF8(*Type));
      if (setRes != RTC_ERR_SUCCESS)
      {
        UE_LOG(LogTemp, Error, TEXT("Synavis: rtcSetRemoteDescription failed (rc=%d) for player %d. SDP type=%s length=%d"), setRes, TargetPlayer, *Type, sdpf.Len());
        UE_LOG(LogTemp, Error, TEXT("Synavis: Failed remote SDP: %s"), *sdpf);
        // Attempt to dump local description for additional context
        char localBuf[8192] = {0};
        int gotLocal = rtcGetLocalDescription(Conn->PeerConnection, localBuf, static_cast<int>(sizeof(localBuf)));
        if (gotLocal > 0)
        {
          UE_LOG(LogTemp, Error, TEXT("Synavis: Current local SDP for pc %d (len=%d):\n%s"), Conn->PeerConnection, gotLocal, ANSI_TO_TCHAR(localBuf));
        }
        else
        {
          UE_LOG(LogTemp, Error, TEXT("Synavis: No local SDP available for pc %d (rtcGetLocalDescription rc=%d)"), Conn->PeerConnection, gotLocal);
        }
      }
      else
      {
        UE_LOG(LogTemp, Log, TEXT("Synavis: Set remote description for player %d"), TargetPlayer);
        UE_LOG(LogTemp, Verbose, TEXT("Synavis: Remote SDP for player %d:\n%s"), TargetPlayer, *sdpf);
        // Dump per-track descriptions after remote description set
        for (const auto& kv : Conn->TracksByHandler)
        {
          int tr = kv.second;
          char trbuf[2048] = {0};
          int trlen = rtcGetTrackDescription(tr, trbuf, static_cast<int>(sizeof(trbuf)));
          if (trlen > 0)
          {
            UE_LOG(LogTemp, Verbose, TEXT("Synavis: Post-setRemote track %d description (len=%d):\n%s"), tr, trlen, ANSI_TO_TCHAR(trbuf));
          }
          else
          {
            UE_LOG(LogTemp, Warning, TEXT("Synavis: Post-setRemote rtcGetTrackDescription returned %d for track %d"), trlen, tr);
          }
        }
      }
      // If remote sent an offer, explicitly create an answer via the C API.
      // Passing NULL lets libdatachannel pick a role which can lead to actpass/actpass
      // if the far end also used NULL. Use explicit "answer" to avoid DTLS role ambiguity.
      if (Type.Equals(TEXT("offer"), ESearchCase::IgnoreCase))
      {
        int localRes = rtcSetLocalDescription(Conn->PeerConnection, "answer");
        if (localRes != RTC_ERR_SUCCESS)
        {
          UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcSetLocalDescription returned %d for player %d"), localRes, TargetPlayer);
        }
        else
        {
          UE_LOG(LogTemp, Log, TEXT("Synavis: rtcSetLocalDescription (answer) succeeded for player %d (pc=%d)"), TargetPlayer, Conn->PeerConnection);
          for (const auto& kv : Conn->TracksByHandler)
          {
            int tr = kv.second;
            char trbuf[2048] = {0};
            int trlen = rtcGetTrackDescription(tr, trbuf, static_cast<int>(sizeof(trbuf)));
            if (trlen > 0)
            {
              UE_LOG(LogTemp, Verbose, TEXT("Synavis: After setLocal(answer) track %d description (len=%d):\n%s"), tr, trlen, ANSI_TO_TCHAR(trbuf));
            }
            else
            {
              UE_LOG(LogTemp, Warning, TEXT("Synavis: After setLocal(answer) rtcGetTrackDescription returned %d for track %d"), trlen, tr);
            }
          }
        }
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
      RegisterRemoteCandidateForConnection(Parsed, *Conn);
      return;
    }
  }
  else
  {
    // Binary signalling frames not expected in this use-case
    UE_LOG(LogTemp, Verbose, TEXT("Synavis: Received binary signalling frame (ignored)"));
  }
}

void USynavisStreamer::RegisterRemoteCandidate(const FJsonObject& Content)
{
  // Route to the correct per-player connection if playerId is present; otherwise broadcast to all
  int32 TargetPlayer = -1;
  if (Content.HasField(TEXT("playerId"))) TargetPlayer = static_cast<int32>(Content.GetNumberField(TEXT("playerId")));
  else if (Content.HasField(TEXT("PlayerID"))) TargetPlayer = static_cast<int32>(Content.GetNumberField(TEXT("PlayerID")));

  if (TargetPlayer != -1)
  {
    FSynavisConnection* Conn = FindConnectionByPlayerID(TargetPlayer);
    if (Conn != nullptr)
    {
      RegisterRemoteCandidateForConnection(Content, *Conn);
      return;
    }

    UE_LOG(LogTemp, Warning, TEXT("Synavis: Received remote candidate for unknown player %d"), TargetPlayer);
    return;
  }

  // No player specified: attempt to add to all connections
  for (auto& Pair : Connections)
  {
    TSharedPtr<FSynavisConnection> C = Pair.Value;
    if (C) RegisterRemoteCandidateForConnection(Content, *C);
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

  RegisteredDataHandlers.Add(Handler);
  // If there are already active PeerConnections, create send-only tracks for
  // this handler now so it is immediately available to connected peers.
  if (Handler.Video)
  {
    for (auto& Pair : Connections)
    {
      TSharedPtr<FSynavisConnection> Conn = Pair.Value;
      if (!Conn) continue;
      if (Conn->PeerConnection == 0) continue;

      rtcTrackInit tinit{};
      tinit.direction = RTC_DIRECTION_SENDONLY;
      tinit.codec = RTC_CODEC_VP9;
      tinit.payloadType = 0;
      // Generate SSRC via member-safe generator
      tinit.ssrc = GetNextSSRC();
      // Build identifiers using FString then convert to UTF-8 and persist
      FString msidF = FString::Printf(TEXT("synavis-%u"), Handler.HandlerID);
      FString trackidF = FString::Printf(TEXT("track-%u-%d"), Handler.HandlerID, Conn->ConnectionID);
      FString tnameF = trackidF;
      FTCHARToUTF8 msidUtf8(*msidF);
      FTCHARToUTF8 trackUtf8(*trackidF);
      FTCHARToUTF8 nameUtf8(*tnameF);
      int32 baseIdx2 = Conn->PersistentTrackUtf8.Num();
      // msid
      {
        int32 len = msidUtf8.Length();
        TArray<ANSICHAR> buf; buf.SetNum(len + 1);
        FMemory::Memcpy(buf.GetData(), msidUtf8.Get(), len);
        buf[len] = '\0';
        Conn->PersistentTrackUtf8.Add(MoveTemp(buf));
      }
      // track id
      {
        int32 len = trackUtf8.Length();
        TArray<ANSICHAR> buf; buf.SetNum(len + 1);
        FMemory::Memcpy(buf.GetData(), trackUtf8.Get(), len);
        buf[len] = '\0';
        Conn->PersistentTrackUtf8.Add(MoveTemp(buf));
      }
      // name
      {
        int32 len = nameUtf8.Length();
        TArray<ANSICHAR> buf; buf.SetNum(len + 1);
        FMemory::Memcpy(buf.GetData(), nameUtf8.Get(), len);
        buf[len] = '\0';
        Conn->PersistentTrackUtf8.Add(MoveTemp(buf));
      }
      tinit.mid = nullptr;
      tinit.name = Conn->PersistentTrackUtf8[baseIdx2 + 2].GetData();
      tinit.msid = Conn->PersistentTrackUtf8[baseIdx2 + 0].GetData();
      tinit.trackId = Conn->PersistentTrackUtf8[baseIdx2 + 1].GetData();
      tinit.profile = nullptr;

      UE_LOG(LogTemp, Log, TEXT("Synavis: About to add send-only track (post-register) for handler %d on pc %d (conn=%d)"), Handler.HandlerID, Conn->PeerConnection, Conn->ConnectionID);
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: rtcTrackInit fields before rtcAddTrackEx (post-register): ssrc=%u msid=%s trackId=%s name=%s"), tinit.ssrc, ANSI_TO_TCHAR(tinit.msid ? tinit.msid : ""), ANSI_TO_TCHAR(tinit.trackId ? tinit.trackId : ""), ANSI_TO_TCHAR(tinit.name ? tinit.name : ""));
      int trid = rtcAddTrackEx(Conn->PeerConnection, &tinit);
      if (trid > 0)
      {
        Conn->TracksByHandler.emplace(Handler.HandlerID, trid);
        // Update handler-side mapping for the stored handler instance
        FSynavisHandler* stored = GetHandlerById(Handler.HandlerID);
        if (stored)
        {
          stored->VideoTracksByConnection.Add(Conn->ConnectionID, trid);
        }
        UE_LOG(LogTemp, Log, TEXT("Synavis: Created send-only track %d for handler %d on pc %d (post-register)"), trid, Handler.HandlerID, Conn->PeerConnection);
        // Diagnostic: ask libdatachannel for this track's description immediately after creation
        {
          char trDescBuf[2048] = {0};
          int trDescLen = rtcGetTrackDescription(trid, trDescBuf, static_cast<int>(sizeof(trDescBuf)));
          if (trDescLen > 0)
          {
            UE_LOG(LogTemp, Verbose, TEXT("Synavis: rtcGetTrackDescription returned %d for track %d immediately after creation (post-register):\n%s"), trDescLen, trid, ANSI_TO_TCHAR(trDescBuf));
          }
          else
          {
            UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcGetTrackDescription returned %d for track %d immediately after creation (post-register)"), trDescLen, trid);
          }
        }
        // Trigger renegotiation for this connection if policy allows
        TriggerRenegotiationForConnection(Conn.Get());
        // Also attempt an explicit local-offer here for diagnosis and to force m-line inclusion
        int setResPost = rtcSetLocalDescription(Conn->PeerConnection, "offer");
        if (setResPost != RTC_ERR_SUCCESS)
        {
          UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcSetLocalDescription returned %d after post-register add track %d on pc %d"), setResPost, trid, Conn->PeerConnection);
        }
        else
        {
          UE_LOG(LogTemp, Log, TEXT("Synavis: rtcSetLocalDescription succeeded after post-register add track %d on pc %d"), trid, Conn->PeerConnection);
          char localBufPost[8192] = {0};
          int gotLocalPost = rtcGetLocalDescription(Conn->PeerConnection, localBufPost, static_cast<int>(sizeof(localBufPost)));
          if (gotLocalPost > 0)
          {
            UE_LOG(LogTemp, Verbose, TEXT("Synavis: New local SDP for pc %d (len=%d):\n%s"), Conn->PeerConnection, gotLocalPost, ANSI_TO_TCHAR(localBufPost));
          }
          else
          {
            UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcGetLocalDescription returned %d after post-register renegotiation for pc %d"), gotLocalPost, Conn->PeerConnection);
          }
          // Dump per-track descriptions after post-register local description
          for (const auto& kv2 : Conn->TracksByHandler)
          {
            int tr2 = kv2.second;
            char trbuf2[2048] = {0};
            int trlen2 = rtcGetTrackDescription(tr2, trbuf2, static_cast<int>(sizeof(trbuf2)));
            if (trlen2 > 0)
            {
              UE_LOG(LogTemp, Verbose, TEXT("Synavis: Post-register setLocal track %d description (len=%d):\n%s"), tr2, trlen2, ANSI_TO_TCHAR(trbuf2));
            }
            else
            {
              UE_LOG(LogTemp, Warning, TEXT("Synavis: Post-register rtcGetTrackDescription returned %d for track %d"), trlen2, tr2);
            }
          }
        }
      }
      else
      {
        UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcAddTrackEx failed for handler %d on pc %d during post-registration (rc=%d)"), Handler.HandlerID, Conn->PeerConnection, trid);
      }
    }
  }
  // Report RTC state after registering handler (and any post-registration track creation)
  RTCReport();
  return Handler.HandlerID;
}

void USynavisStreamer::CaptureFrame()
{
  // Capture frames for registered handlers using only the zero-copy NV12 path.
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
      auto it = Conn->TracksByHandler.find(Handler.HandlerID);
      if (it != Conn->TracksByHandler.end() && it->second != 0 && rtcIsOpen(it->second))
      {
        TracksToSend.Add(it->second);
      }
    }

    if (TracksToSend.Num() == 0)
    {
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: No open tracks for handler %d, skipping readback"), Handler.HandlerID);
      RTCReport();
      // this is what we need to fix so there is no point going on after this
      // exit game
      GetWorld()->GetFirstPlayerController()->ConsoleCommand(TEXT("exit"));
    }
    else
    {
      FRHIGPUTextureReadback* ReadbackY = nullptr;
      FRHIGPUTextureReadback* ReadbackUV = nullptr;
      if (EnqueueNV12ReadbackFromRenderTarget(HandlerRT, ReadbackY, ReadbackUV))
      {
        FPendingNV12Readback rec;
        rec.ReadbackY = ReadbackY;
        rec.ReadbackUV = ReadbackUV;
        rec.EnqueuedAt = FPlatformTime::Seconds();
        rec.TargetTracks = TracksToSend;
        rec.Width = Width;
        rec.Height = Height;
        PendingReadbacks.Add(rec);
        UE_LOG(LogTemp, Verbose, TEXT("Synavis: Enqueued NV12 readback for handler %d (W=%d H=%d) to %d tracks"), Handler.HandlerID, Width, Height, TracksToSend.Num());
      }
      else
      {
        UE_LOG(LogTemp, Warning, TEXT("Synavis: Failed to enqueue NV12 readback for handler %d"), Handler.HandlerID);
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
  UE_LOG(LogTemp, Warning, TEXT("[%0.6f] Synavis(member): PC %d created datachannel %d userPtr=%p"), FPlatformTime::Seconds(), pc, dc, rtcGetUserPointer(dc));

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
            UE_LOG(LogTemp, Warning, TEXT("[%0.6f] HandlePcDataChannelCreated: Found central ctx %p for dc=%d prior conn=%d prior handler=%u"), FPlatformTime::Seconds(), ctx, dc, ctx->ConnectionID, ctx->HandlerID);
            ctx->ConnectionID = C->ConnectionID;
            ctx->HandlerID = 0;
            ctx->ConnRaw = C.Get();
          } else {
            // final fallback: try user pointer if it actually contains a ctx
            void* uptr = rtcGetUserPointer(dc);
            if (uptr) {
              DataChannelCtx* ctx = reinterpret_cast<DataChannelCtx*>(uptr);
              UE_LOG(LogTemp, Warning, TEXT("[%0.6f] HandlePcDataChannelCreated: Fallback userPtr=%p for dc=%d"), FPlatformTime::Seconds(), uptr, dc);
              if (ctx && ctx->Streamer == this) { ctx->ConnectionID = C->ConnectionID; ctx->HandlerID = 0; ctx->ConnRaw = C.Get(); UE_LOG(LogTemp, Warning, TEXT("[%0.6f] HandlePcDataChannelCreated: Assigned fallback ctx %p -> conn=%d"), FPlatformTime::Seconds(), ctx, C->ConnectionID); }
            }
          }
        }
        UE_LOG(LogTemp, Warning, TEXT("[%0.6f] Synavis(member): Adopted incoming datachannel %d as system channel for conn %d"), FPlatformTime::Seconds(), dc, C->ConnectionID);
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
          if (hb.second == H.HandlerID) { found = true; break; }
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
          UE_LOG(LogTemp, Warning, TEXT("[%0.6f] HandlePcDataChannelCreated: Created central ctx %p for dc=%d assigning handler=%u"), FPlatformTime::Seconds(), ctxptr.Get(), dc, HandlerToAssign);
        } else {
          DataChannelCtx* ctx = ctxptr.Get();
          UE_LOG(LogTemp, Warning, TEXT("[%0.6f] HandlePcDataChannelCreated: Found central ctx %p for dc=%d assigning handler=%u"), FPlatformTime::Seconds(), ctx, dc, HandlerToAssign);
          ctx->ConnectionID = C->ConnectionID;
          ctx->HandlerID = HandlerToAssign;
          ctx->ConnRaw = C.Get();
          ctx->PeerPC = pc;
          // ensure user pointer references the ctx for easier diagnostics
          rtcSetUserPointer(dc, ctx);
        }
        UE_LOG(LogTemp, Warning, TEXT("[%0.6f] Synavis(member): Associated incoming datachannel %d label='%s' -> Handler %u (conn %d) by dedicated-channel heuristic"), FPlatformTime::Seconds(), dc, *GetDataChannelLabelSafe(dc), HandlerToAssign, C->ConnectionID);
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

// Helper: trigger renegotiation for a single connection according to SourcePolicy
void USynavisStreamer::TriggerRenegotiationForConnection(FSynavisConnection* Conn)
{
  if (!Conn || Conn->PeerConnection == 0) return;

  // If negotiation is held, mark pending and return
  if (bHoldNegotiation)
  {
    Conn->PendingNegotiation = true;
    UE_LOG(LogTemp, Log, TEXT("Synavis: Marked connection %d for pending negotiation"), Conn->ConnectionID);
    return;
  }

  // Only attempt renegotiation when the source policy allows dynamic updates
  if (SourcePolicy == ESynavisSourcePolicy::RemainStatic)
    return;

  // Ask the C API whether negotiation is required
  if (!rtcIsNegotiationNeeded(Conn->PeerConnection))
    return;

  int localRes = rtcSetLocalDescription(Conn->PeerConnection, "offer");
  if (localRes != RTC_ERR_SUCCESS)
  {
    if (SourcePolicy == ESynavisSourcePolicy::DynamicMandatory)
    {
      UE_LOG(LogTemp, Error, TEXT("Synavis: Forced renegotiation failed for conn %d (rc=%d)"), Conn->ConnectionID, localRes);
    }
    else
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: Renegotiation attempt failed for conn %d (rc=%d)"), Conn->ConnectionID, localRes);
    }
  }
    else
  {
    UE_LOG(LogTemp, Log, TEXT("Synavis: Triggered renegotiation (offer) for conn %d"), Conn->ConnectionID);
    // Dump per-track descriptions after triggering local offer
    for (const auto& kv : Conn->TracksByHandler)
    {
      int tr = kv.second;
      char trbuf[2048] = {0};
      int trlen = rtcGetTrackDescription(tr, trbuf, static_cast<int>(sizeof(trbuf)));
      if (trlen > 0)
      {
        UE_LOG(LogTemp, Verbose, TEXT("Synavis: TriggerRenegotiation post-offer track %d description (len=%d):\n%s"), tr, trlen, ANSI_TO_TCHAR(trbuf));
      }
      else
      {
        UE_LOG(LogTemp, Warning, TEXT("Synavis: TriggerRenegotiation rtcGetTrackDescription returned %d for track %d"), trlen, tr);
      }
    }
  }
}

void USynavisStreamer::StartConnectionNegotiation()
{
  UE_LOG(LogTemp, Log, TEXT("Synavis: StartConnectionNegotiation called - releasing hold and triggering pending negotiations"));
  // Clear the global hold first
  bHoldNegotiation = false;

  for (auto& Pair : Connections)
  {
    TSharedPtr<FSynavisConnection> C = Pair.Value;
    if (!C || C->PeerConnection == 0) continue;

    if (C->PendingNegotiation || rtcIsNegotiationNeeded(C->PeerConnection))
    {
      int localRes = rtcSetLocalDescription(C->PeerConnection, "offer");
      if (localRes != RTC_ERR_SUCCESS)
      {
        UE_LOG(LogTemp, Warning, TEXT("Synavis: StartConnectionNegotiation: rtcSetLocalDescription failed for conn %d (rc=%d)"), C->ConnectionID, localRes);
      }
      else
      {
        UE_LOG(LogTemp, Log, TEXT("Synavis: StartConnectionNegotiation: Sent offer for conn %d"), C->ConnectionID);
        C->PendingNegotiation = false;
        // Dump per-track descriptions after sending offer
        for (const auto& kv : C->TracksByHandler)
        {
          int tr = kv.second;
          char trbuf[2048] = {0};
          int trlen = rtcGetTrackDescription(tr, trbuf, static_cast<int>(sizeof(trbuf)));
          if (trlen > 0)
          {
            UE_LOG(LogTemp, Verbose, TEXT("Synavis: StartConnectionNegotiation post-offer track %d description (len=%d):\n%s"), tr, trlen, ANSI_TO_TCHAR(trbuf));
          }
          else
          {
            UE_LOG(LogTemp, Warning, TEXT("Synavis: StartConnectionNegotiation rtcGetTrackDescription returned %d for track %d"), trlen, tr);
          }
        }
      }
    }
  }
}

// Zero-copy: accept two FRHIGPUTextureReadback objects, wrap their locked pointers into
// AVBufferRefs so FFmpeg manages lifetime and calls our free-callback to unlock/delete readbacks.
void USynavisStreamer::EncodeNV12ReadbackAndSend(FRHIGPUTextureReadback* ReadbackY, FRHIGPUTextureReadback* ReadbackUV, int Width, int Height, const TArray<int32>& TargetTracks)
{
  if (!ReadbackY || !ReadbackUV) return;

  // Poll briefly
  const double TimeoutSeconds = 0.5;
  double StartTime = FPlatformTime::Seconds();
  while (FPlatformTime::Seconds() - StartTime < TimeoutSeconds)
  {
    if (ReadbackY->IsReady() && ReadbackUV->IsReady()) break;
    FPlatformProcess::Sleep(0.001f);
  }
  if (!ReadbackY->IsReady() || !ReadbackUV->IsReady())
  {
    return;
  }

  int YRowPitchPixels = 0; void* YPtr = ReadbackY->Lock(YRowPitchPixels);
  if (!YPtr) { ReadbackY->Unlock(); ReadbackUV->Unlock(); return; }
  int UVRowPitchPixels = 0; void* UVPtr = ReadbackUV->Lock(UVRowPitchPixels);
  if (!UVPtr) { ReadbackY->Unlock(); ReadbackUV->Unlock(); return; }

  int YStride = YRowPitchPixels;
  int UVStride = UVRowPitchPixels * 2;

  UE_LOG(LogTemp, Verbose, TEXT("Synavis: EncodeNV12ReadbackAndSend lock OK W=%d H=%d Ystride=%d UVstride=%d targets=%d"), Width, Height, YStride, UVStride, TargetTracks.Num());

  struct ReadbackFreeCtx { FRHIGPUTextureReadback* RB; };

  auto freeCb = [](void* opaque)
    {
      ReadbackFreeCtx* ctx = reinterpret_cast<ReadbackFreeCtx*>(opaque);
      if (!ctx) return;
      FRHIGPUTextureReadback* RB = ctx->RB;
      // Unlock and delete must run on render thread; enqueue a render command to do it.
      ENQUEUE_RENDER_COMMAND(Synavis_FreeReadback)([RB](FRHICommandListImmediate& RHICmdList)
        {
          if (RB)
          {
            RB->Unlock();
            delete RB;
          }
        });
      delete ctx;
    };

  ReadbackFreeCtx* ctxY = new ReadbackFreeCtx{ ReadbackY };
  ReadbackFreeCtx* ctxUV = new ReadbackFreeCtx{ ReadbackUV };

  AVBufferRef* bufY = av_buffer_create(static_cast<uint8_t*>(YPtr), Width * Height, AvFreeReadback, ctxY, 0);
  AVBufferRef* bufUV = av_buffer_create(static_cast<uint8_t*>(UVPtr), (Width * Height) / 2, AvFreeReadback, ctxUV, 0);

  if (!bufY || !bufUV)
  {
    if (bufY) av_buffer_unref(&bufY);
    if (bufUV) av_buffer_unref(&bufUV);
    return;
  }

  FScopeLock guard(&LibAVState->Mutex);
  AVFrame* frame = av_frame_alloc();
  frame->format = AV_PIX_FMT_NV12;
  frame->width = Width; frame->height = Height;
  frame->buf[0] = bufY; frame->buf[1] = bufUV;
  frame->data[0] = bufY->data; frame->linesize[0] = YStride;
  frame->data[1] = bufUV->data; frame->linesize[1] = UVStride;

  int ret = avcodec_send_frame(LibAVState->CodecCtx, frame);
  if (ret < 0)
  {
    char errbuf[128]; av_strerror(ret, errbuf, sizeof(errbuf));
    UE_LOG(LogTemp, Error, TEXT("Synavis: avcodec_send_frame (NV12 zero-copy) failed: %s"), ANSI_TO_TCHAR(errbuf));
  }

  while ((ret = avcodec_receive_packet(LibAVState->CodecCtx, LibAVState->Packet)) >= 0)
  {
    // Prepare packet data
    size_t sz = static_cast<size_t>(LibAVState->Packet->size);
    const uint8_t* data = LibAVState->Packet->data;

    if (sz == 0)
    {
      av_packet_unref(LibAVState->Packet);
      continue;
    }

    // Determine dispatch targets. Prefer explicit TargetTracks (populated at capture time),
    // otherwise fall back to all currently-open tracks across connections so we don't silently drop images.
    TArray<int32> DispatchTargets = TargetTracks;
    
    if (DispatchTargets.Num() > 0)
    {
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: Sending encoded packet size=%d to %d handler track(s)"), (int)sz, DispatchTargets.Num());
      for (int tr : DispatchTargets)
      {
        if (tr != 0 && rtcIsOpen(tr))
        {
          // Compute RTP timestamp. Prefer AVPacket PTS if available, otherwise wall clock.
          uint32_t rtpTs = 0;
#if defined(LIBAV_AVAILABLE)
          if (LibAVState->Packet->pts != AV_NOPTS_VALUE)
          {
            AVRational outQ = {1, 90000};
            rtpTs = static_cast<uint32_t>(av_rescale_q(LibAVState->Packet->pts, LibAVState->CodecCtx->time_base, outQ));
          }
          else
          {
            rtpTs = static_cast<uint32_t>(FPlatformTime::Seconds() * 90000.0);
          }
#else
          rtpTs = static_cast<uint32_t>(FPlatformTime::Seconds() * 90000.0);
#endif
          // Call packetizer & sender with RTP timestamp
          UE_LOG(LogTemp, Verbose, TEXT("Synavis: Calling VP9PacketizeAndSend for track %d packetSize=%d rtpTs=%u"), tr, static_cast<int>(sz), rtpTs);
          VP9PacketizeAndSend(tr, data, sz, rtpTs);
        }
      }
    }
    // No open tracks: try the global/system datachannel using the C API
    else if (SystemDataChannel != 0 && rtcIsOpen(SystemDataChannel))
    {
      int sendRes = rtcSendMessage(SystemDataChannel, reinterpret_cast<const char*>(data), static_cast<int>(sz));
      if (sendRes != RTC_ERR_SUCCESS)
      {
        UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcSendMessage returned %d when sending packet to system datachannel"), sendRes);
      }
      else
      {
        UE_LOG(LogTemp, VeryVerbose, TEXT("Synavis: Sent encoded packet to system datachannel=%d size=%d"), SystemDataChannel, static_cast<int>(sz));
      }
    }
    else
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: Encoded packet dropped - no open tracks or datachannel"));
    }
    av_packet_unref(LibAVState->Packet);
  }

  av_frame_free(&frame);
}

