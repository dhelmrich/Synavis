// Fill out your copyright notice in the Description page of Project Settings.


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

void USynavisStreamer::TeardownConnection(int32 PlayerID)
{
  FSynavisConnection* Conn = FindConnectionByPlayerID(PlayerID);
  if (!Conn)
  {
    UE_LOG(LogTemp, Warning, TEXT("Synavis: Teardown requested for unknown connection %d"), PlayerID);
    return;
  }

  // Close peerconnection
  if (Conn->PeerConnection)
  {
    // Close and delete the C API peer connection id
    rtcClosePeerConnection(Conn->PeerConnection);
    rtcDeletePeerConnection(Conn->PeerConnection);
    Conn->PeerConnection = 0;
  }

  // Close control/data channels
  if (Conn->DataChannel)
  {
    rtcClose(Conn->DataChannel);
    rtcDeleteDataChannel(Conn->DataChannel);
    Conn->DataChannel = 0;
  }
  for (auto& kv : Conn->DataChannelsByHandler)
  {
    int dc = kv.second;
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


  Conn->DataChannelsByHandler.clear();

  // Finally remove from map
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

// C callbacks used with the libdatachannel C API (rtc/rtc.h). These callbacks are plain C-style
// functions which receive the websocket id and the user pointer we attached with
// rtcSetUserPointer. We forward the events to the UE member handlers on the game thread.
extern "C" {
  void Synavis_Rtc_OnOpen(int id, void* user_ptr)
  {
    USynavisStreamer* self = reinterpret_cast<USynavisStreamer*>(user_ptr);
    if (!self) return;
    // Forward to game thread
    AsyncTask(ENamedThreads::GameThread, [self]() { self->NotifySignallingOpen(); });
  }

  void Synavis_Rtc_OnClosed(int id, void* user_ptr)
  {
    USynavisStreamer* self = reinterpret_cast<USynavisStreamer*>(user_ptr);
    if (!self) return;
    AsyncTask(ENamedThreads::GameThread, [self]() { self->NotifySignallingClose(); });
  }

  void Synavis_Rtc_OnError(int id, const char* err, void* user_ptr)
  {
    USynavisStreamer* self = reinterpret_cast<USynavisStreamer*>(user_ptr);
    if (!self) return;
    std::string s = err ? std::string(err) : std::string();
    AsyncTask(ENamedThreads::GameThread, [self, s]() { self->NotifySignallingError(s); });
  }

  void Synavis_Rtc_OnMessage(int id, const char* data, int size, void* user_ptr)
  {
    USynavisStreamer* self = reinterpret_cast<USynavisStreamer*>(user_ptr);
    if (!self) return;
    // size < 0 indicates a null-terminated text message according to the C API conventions
    if (size < 0)
    {
      // treat as null-terminated string
      std::string s = data ? std::string(data) : std::string();
      AsyncTask(ENamedThreads::GameThread, [self, s]() {
        self->NotifySignallingMessage(std::variant<TArray<uint8>, std::string>(s));
        });
    }
    else
    {
      // binary payload
      TArray<uint8> b;
      if (size > 0 && data)
      {
        b.AddUninitialized(size);
        memcpy(b.GetData(), data, static_cast<size_t>(size));
      }
      AsyncTask(ENamedThreads::GameThread, [self, b]() mutable {
        self->NotifySignallingMessage(std::variant<TArray<uint8>, std::string>(b));
        });
    }
  }

}

// PeerConnection and DataChannel callbacks
    // Forward-declare DataChannel callbacks so they can be referenced by PC callbacks below
void Synavis_Rtc_DataChannel_OnMessage(int id, const char* data, int size, void* user_ptr);
void Synavis_Rtc_DataChannel_OnOpen(int id, void* user_ptr);
void Synavis_Rtc_DataChannel_OnClosed(int id, void* user_ptr);
void Synavis_Rtc_DataChannel_OnError(int id, const char* error, void* user_ptr);

void Synavis_Rtc_OnPcLocalDescription(int pc, const char* sdp, const char* type, void* user_ptr)
{
  USynavisStreamer* self = reinterpret_cast<USynavisStreamer*>(user_ptr);
  if (!self) return;
  std::string s = sdp ? std::string(sdp) : std::string();
  std::string t = type ? std::string(type) : std::string();
  AsyncTask(ENamedThreads::GameThread, [self, pc, s, t]() {
    self->NotifyPcLocalDescription(pc, s.c_str(), t.c_str());
    });
}

void Synavis_Rtc_OnPcLocalCandidate(int pc, const char* cand, const char* mid, void* user_ptr)
{
  // For now forward as log; can be expanded to dispatch as needed
  USynavisStreamer* self = reinterpret_cast<USynavisStreamer*>(user_ptr);
  if (!self) return;
  AsyncTask(ENamedThreads::GameThread, [self, pc, cand = cand ? std::string(cand) : std::string(), mid = mid ? std::string(mid) : std::string()]() {
    UE_LOG(LogTemp, Verbose, TEXT("Synavis: PC %d local candidate callback (mid=%s)"), pc, ANSI_TO_TCHAR(mid.c_str()));
    });
}

void Synavis_Rtc_OnPcGatheringStateChange(int pc, rtcGatheringState state, void* user_ptr)
{
  USynavisStreamer* self = reinterpret_cast<USynavisStreamer*>(user_ptr);
  if (!self) return;
  int istate = static_cast<int>(state);
  AsyncTask(ENamedThreads::GameThread, [self, pc, istate]() {
    self->NotifyPcGatheringStateChange(pc, istate);
    });
}

void Synavis_Rtc_OnPcDataChannel(int pc, int dc, void* user_ptr)
{
  USynavisStreamer* self = reinterpret_cast<USynavisStreamer*>(user_ptr);
  if (!self) return;
  // Configure per-datachannel callbacks and forward open/message events
  // Set user pointer and callbacks for the newly-created datachannel
  rtcSetUserPointer(dc, user_ptr);
  rtcSetMessageCallback(dc, Synavis_Rtc_DataChannel_OnMessage);
  rtcSetOpenCallback(dc, Synavis_Rtc_DataChannel_OnOpen);
  rtcSetClosedCallback(dc, Synavis_Rtc_DataChannel_OnClosed);
  rtcSetErrorCallback(dc, Synavis_Rtc_DataChannel_OnError);
  AsyncTask(ENamedThreads::GameThread, [self, pc, dc]() {
    UE_LOG(LogTemp, Log, TEXT("Synavis: DataChannel %d created for PC %d"), dc, pc);
    });
}

// DataChannel callbacks used above
void Synavis_Rtc_DataChannel_OnOpen(int id, void* user_ptr)
{
  USynavisStreamer* self = reinterpret_cast<USynavisStreamer*>(user_ptr);
  if (!self) return;
  AsyncTask(ENamedThreads::GameThread, [self, id]() { self->NotifyDataChannelOpen(id); });
}

void Synavis_Rtc_DataChannel_OnClosed(int id, void* user_ptr)
{
  USynavisStreamer* self = reinterpret_cast<USynavisStreamer*>(user_ptr);
  if (!self) return;
  AsyncTask(ENamedThreads::GameThread, [self, id]() { self->NotifyDataChannelClosed(id); });
}

void Synavis_Rtc_DataChannel_OnError(int id, const char* error, void* user_ptr)
{
  USynavisStreamer* self = reinterpret_cast<USynavisStreamer*>(user_ptr);
  if (!self) return;
  std::string s = error ? std::string(error) : std::string();
  AsyncTask(ENamedThreads::GameThread, [self, id, s]() { UE_LOG(LogTemp, Error, TEXT("Synavis: DataChannel %d error: %s"), id, ANSI_TO_TCHAR(s.c_str())); });
}

void Synavis_Rtc_DataChannel_OnMessage(int id, const char* data, int size, void* user_ptr)
{
  USynavisStreamer* self = reinterpret_cast<USynavisStreamer*>(user_ptr);
  if (!self) return;
  if (size < 0)
  {
    std::string s = data ? std::string(data) : std::string();
    AsyncTask(ENamedThreads::GameThread, [self, id, s]() {
      self->NotifyDataChannelMessage(id, std::variant<TArray<uint8>, std::string>(s));
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
    AsyncTask(ENamedThreads::GameThread, [self, id, b]() mutable {
      self->NotifyDataChannelMessage(id, std::variant<TArray<uint8>, std::string>(b));
      });
  }
}

// Additional PeerConnection callbacks: track/state/ice state notifications
void Synavis_Rtc_OnPcTrack(int pc, int tr, void* user_ptr)
{
  USynavisStreamer* self = reinterpret_cast<USynavisStreamer*>(user_ptr);
  if (!self) return;
  AsyncTask(ENamedThreads::GameThread, [self, pc, tr]() {
    UE_LOG(LogTemp, Verbose, TEXT("Synavis: PC %d track event for track %d"), pc, tr);
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
}

USynavisStreamer::~USynavisStreamer()
{
  if (SignallingId != 0)
  {
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
}

// Called when the game starts
void USynavisStreamer::BeginPlay()
{
  Super::BeginPlay();

}


void USynavisStreamer::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
  // If no connection currently requests streaming, early-out. Streaming is
  // managed per-connection via FSynavisConnection::bStreaming.
  bool anyStreaming = false;
  for (const auto& Pair : Connections) { if (Pair.Value.bStreaming) { anyStreaming = true; break; } }
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

// VP9 packetizer stub
// Replace the body of VP9PacketizeAndSend with a real VP9 packetizer that fragments
// encoded VP9 frames into RTP payloads (or create rtcMessage opaque messages +
// media interceptor) as required by your libdatachannel runtime. For now this
// stub simply sends the raw encoded payload via rtcSendMessage to the track id.
static void VP9PacketizeAndSend(int trackId, const uint8_t* data, size_t size)
{
  if (trackId == 0 || data == nullptr || size == 0) return;
  if (!rtcIsOpen(trackId))
  {
    UE_LOG(LogTemp, Warning, TEXT("Synavis: VP9PacketizeAndSend - track %d not open, dropping packet"), trackId);
    return;
  }

  // TODO: Insert VP9 packetization here. Example approaches:
  //  - Implement VP9 RTP packetization locally and call rtcSendMessage(trackId, chunk, chunkSize) for each RTP packet.
  //  - Use libdatachannel's opaque message + media interceptor API (rtcCreateOpaqueMessage + rtcSetMediaInterceptorCallback)
  //    to hand an encoded frame + metadata to libdatachannel and let it perform packetization.

  // Interim behavior: forward raw encoded packet as a single binary message.
  int sendRes = rtcSendMessage(trackId, reinterpret_cast<const char*>(data), static_cast<int>(size));
  if (sendRes != RTC_ERR_SUCCESS)
  {
    UE_LOG(LogTemp, Warning, TEXT("Synavis: VP9PacketizeAndSend rtcSendMessage returned %d for track %d"), sendRes, trackId);
  }
}

USynavisStreamer::FLibAVEncoderState::~FLibAVEncoderState()
{
  if (Packet) { av_packet_free(&Packet); Packet = nullptr; }
  if (Frame) { av_frame_free(&Frame); Frame = nullptr; }
  if (CodecCtx) { avcodec_free_context(&CodecCtx); CodecCtx = nullptr; }
}


ESynavisState USynavisStreamer::GetConnectionState() const
{
  // return connection state --> we need to do callback-based updates here
  return this->ConnectionState;
}

bool USynavisStreamer::AnyConnectionStreaming() const
{
  for (const auto& Pair : Connections)
  {
    if (Pair.Value.bStreaming) return true;
  }
  return false;
}

void USynavisStreamer::StartStreaming()
{
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
    Pair.Value.bStreaming = true;
  }
}


void USynavisStreamer::StopStreaming()
{
  // Disable streaming on all connections and tear down encoder state.
  for (auto& Pair : Connections)
  {
    Pair.Value.bStreaming = false;
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
  // and return an integer id. Attach our `this` pointer with rtcSetUserPointer so callbacks
  // receive it as the last argument.
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



// Public wrappers for C callbacks to call into the protected handlers
void USynavisStreamer::NotifySignallingOpen()
{
  HandleSignallingOpen();
}

void USynavisStreamer::NotifySignallingClose()
{
  HandleSignallingClose();
}

void USynavisStreamer::NotifySignallingError(const std::string& Err)
{
  HandleSignallingError(Err);
}

void USynavisStreamer::NotifySignallingMessage(const std::variant<TArray<uint8>, std::string>& Message)
{
  HandleSignallingMessage(Message);
}

// Public Notify wrappers for PC/DataChannel events (call the protected handlers)
void USynavisStreamer::NotifyPcLocalDescription(int pc, const char* sdp, const char* type)
{
  HandlePcLocalDescriptionCallback(pc, sdp, type);
}

void USynavisStreamer::NotifyPcGatheringStateChange(int pc, int state)
{
  HandlePcGatheringStateChangeCallback(pc, state);
}

void USynavisStreamer::NotifyDataChannelMessage(int dc, const std::variant<TArray<uint8>, std::string>& message)
{
  HandleDataChannelMessageCallback(dc, message);
}

void USynavisStreamer::NotifyDataChannelOpen(int dc)
{
  HandleDataChannelOpenCallback(dc);
}

void USynavisStreamer::NotifyDataChannelClosed(int dc)
{
  HandleDataChannelClosedCallback(dc);
}

void USynavisStreamer::HandlePcLocalDescriptionCallback(int pc, const char* sdp, const char* type)
{
  FSynavisConnection* Conn = nullptr;
  for (auto& Pair : Connections)
  {
    if (Pair.Value.PeerConnection == pc) { Conn = &Pair.Value; break; }
  }
  if (!Conn) return;
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
      if (Pair.Value.PeerConnection == pc) { Conn = &Pair.Value; break; }
    }
    if (Conn) CommunicateSDPForConnection(*Conn);
  }
}

void USynavisStreamer::HandleDataChannelMessageCallback(int dc, const std::variant<TArray<uint8>, std::string>& message)
{
  // Forward to generic handler which will parse and dispatch messages
  OnDataChannelMessage(message);
}

void USynavisStreamer::HandleDataChannelOpenCallback(int dc)
{
  // Find the connection that owns this datachannel and mark state
  for (auto& Pair : Connections)
  {
    FSynavisConnection& C = Pair.Value;
    if (C.DataChannel == dc)
    {
      C.State = EPeerState::ChannelOpen;
      UE_LOG(LogTemp, Log, TEXT("Synavis: DataChannel %d opened for connection %d"), dc, C.ConnectionID);
      return;
    }
    for (auto& kv : C.DataChannelsByHandler)
    {
      if (kv.second == dc)
      {
        C.State = EPeerState::ChannelOpen;
        UE_LOG(LogTemp, Log, TEXT("Synavis: Dedicated DataChannel %d opened for handler %d player %d"), dc, kv.first, C.ConnectionID);
        return;
      }
    }
  }
}

void USynavisStreamer::HandleDataChannelClosedCallback(int dc)
{
  for (auto& Pair : Connections)
  {
    FSynavisConnection& C = Pair.Value;
    if (C.DataChannel == dc)
    {
      C.State = EPeerState::NoConnection;
      UE_LOG(LogTemp, Log, TEXT("Synavis: DataChannel %d closed for connection %d"), dc, C.ConnectionID);
      return;
    }
    for (auto& kv : C.DataChannelsByHandler)
    {
      if (kv.second == dc)
      {
        UE_LOG(LogTemp, Log, TEXT("Synavis: Dedicated DataChannel %d closed for handler %d player %d"), dc, kv.first, C.ConnectionID);
        return;
      }
    }
  }
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
    CommunicateSDPForConnection(Pair.Value);
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
      UE_LOG(LogTemp, Log, TEXT("Synavis: Sent local SDP for connection %d via signalling (id=%d)"), Conn.ConnectionID, SignallingId);
    }

  }
}

FSynavisConnection* USynavisStreamer::FindConnectionByPlayerID(int32 PlayerID)
{
  return Connections.Find(PlayerID);
}

const FSynavisConnection* USynavisStreamer::FindConnectionByPlayerID(int32 PlayerID) const
{
  const FSynavisConnection* Found = nullptr;
  auto It = Connections.Find(PlayerID);
  if (It)
  {
    Found = It;
  }
  return Found;
}

void USynavisStreamer::CreateConnectionForPlayer(int32 PlayerID)
{
  // Skip if a connection already exists
  if (Connections.Contains(PlayerID))
  {
    UE_LOG(LogTemp, Log, TEXT("Synavis: Connection for player %d already exists"), PlayerID);
    return;
  }

  FSynavisConnection Conn;
  Conn.ConnectionID = PlayerID;
  // Inherit the current global streaming-request state (if any connections are
  // currently requesting streaming, enable for this new connection as well).
  Conn.bStreaming = AnyConnectionStreaming();
  // Create a PeerConnection via C API and register C callbacks
  rtcConfiguration cfg{}; // default-initialized configuration
  int pcid = rtcCreatePeerConnection(&cfg);
  if (pcid <= 0)
  {
    UE_LOG(LogTemp, Error, TEXT("Synavis: rtcCreatePeerConnection failed (rc=%d) for player %d"), pcid, PlayerID);
    return;
  }
  Conn.PeerConnection = pcid;

  // Attach user pointer so callbacks can find this USynavisStreamer instance
  rtcSetUserPointer(Conn.PeerConnection, this);
  rtcSetLocalDescriptionCallback(Conn.PeerConnection, Synavis_Rtc_OnPcLocalDescription);
  rtcSetLocalCandidateCallback(Conn.PeerConnection, Synavis_Rtc_OnPcLocalCandidate);
  rtcSetGatheringStateChangeCallback(Conn.PeerConnection, Synavis_Rtc_OnPcGatheringStateChange);
  rtcSetDataChannelCallback(Conn.PeerConnection, Synavis_Rtc_OnPcDataChannel);
  rtcSetTrackCallback(Conn.PeerConnection, Synavis_Rtc_OnPcTrack);
  rtcSetStateChangeCallback(Conn.PeerConnection, Synavis_Rtc_OnPcStateChange);
  rtcSetIceStateChangeCallback(Conn.PeerConnection, Synavis_Rtc_OnPcIceStateChange);

  // Create a per-connection data channel for control/messages using the C API
  std::string channelName = std::string("synavis-data-") + std::to_string(PlayerID);
  int dcid = rtcCreateDataChannel(Conn.PeerConnection, channelName.c_str());
  if (dcid > 0)
  {
    Conn.DataChannel = dcid;
    rtcSetUserPointer(dcid, this);
    rtcSetMessageCallback(dcid, Synavis_Rtc_DataChannel_OnMessage);
    rtcSetOpenCallback(dcid, Synavis_Rtc_DataChannel_OnOpen);
    rtcSetClosedCallback(dcid, Synavis_Rtc_DataChannel_OnClosed);
    rtcSetErrorCallback(dcid, Synavis_Rtc_DataChannel_OnError);
  }

  // Create dedicated datachannels for handlers that requested them and map per-connection
  TArray<FSynavisHandlers> HandlersCopy = RegisteredDataHandlers.Array();
  for (const FSynavisHandlers& HandlerCopy : HandlersCopy)
  {
    if (HandlerCopy.WantsDedicatedChannel)
    {
      std::string handlerChannelName = std::string("synavis-datahandler-") + std::to_string(HandlerCopy.HandlerID) + std::string("-p") + std::to_string(PlayerID);
      int hdc = rtcCreateDataChannel(Conn.PeerConnection, handlerChannelName.c_str());
      if (hdc > 0)
      {
        rtcSetUserPointer(hdc, this);
        rtcSetMessageCallback(hdc, Synavis_Rtc_DataChannel_OnMessage);
        rtcSetOpenCallback(hdc, Synavis_Rtc_DataChannel_OnOpen);
        rtcSetClosedCallback(hdc, Synavis_Rtc_DataChannel_OnClosed);
        rtcSetErrorCallback(hdc, Synavis_Rtc_DataChannel_OnError);
        Conn.DataChannelsByHandler.emplace(HandlerCopy.HandlerID, hdc);
      }
    }
  }

  // Create outgoing send-only tracks for any registered handlers that have video sources.
  // Use the C API rtcAddTrackEx to create a per-connection track id and store it in TracksByHandler.
  for (const FSynavisHandlers& HandlerCopy : HandlersCopy)
  {
    if (HandlerCopy.Video.IsSet())
    {
      // Prepare rtcTrackInit: request a send-only VP9 track when available.
      rtcTrackInit tinit{};
      tinit.direction = RTC_DIRECTION_SENDONLY;
      tinit.codec = RTC_CODEC_VP9; // prefer VP9; libdatachannel may fallback
      tinit.payloadType = 0; // 0 = let libdatachannel choose default
      tinit.ssrc = 0; // 0 = let libdatachannel allocate
      tinit.mid = nullptr;
      tinit.name = nullptr;
      tinit.msid = nullptr;
      tinit.trackId = nullptr;
      tinit.profile = nullptr;

      int trid = rtcAddTrackEx(Conn.PeerConnection, &tinit);
      if (trid > 0)
      {
        // store mapping for this connection
        Conn.TracksByHandler.emplace(HandlerCopy.HandlerID, trid);
        UE_LOG(LogTemp, Log, TEXT("Synavis: Created send-only track %d for handler %d on pc %d"), trid, HandlerCopy.HandlerID, Conn.PeerConnection);

        // Packetizer setup: libdatachannel has packetizers for some codecs (H264/H265/AV1...).
        // VP9 may not have an explicit packetizer function; if needed, configure for other codecs here.
        // Example for AV1/H264 if required:
        // rtcPacketizerInit pinit{}; pinit.ssrc = 0; pinit.maxFragmentSize = RTC_DEFAULT_MAX_FRAGMENT_SIZE; rtcSetAV1Packetizer(trid, &pinit);
      }
      else
      {
        UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcAddTrackEx failed for handler %d on pc %d (rc=%d)"), HandlerCopy.HandlerID, Conn.PeerConnection, trid);
      }
    }
  }

  // Insert into connections map before starting ICE so callbacks can find it
  Connections.Add(PlayerID, MoveTemp(Conn));

  // Finally, start ICE gathering by requesting a local description via C API
  FSynavisConnection& StoredConn = Connections[PlayerID];
  int localRes = rtcSetLocalDescription(StoredConn.PeerConnection, nullptr);
  if (localRes != RTC_ERR_SUCCESS)
  {
    UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcSetLocalDescription returned %d for player %d"), localRes, PlayerID);
  }

  UE_LOG(LogTemp, Log, TEXT("Synavis: Created connection object for player %d (pc=%d dc=%d)"), PlayerID, StoredConn.PeerConnection, StoredConn.DataChannel);
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
      std::string sdp = TCHAR_TO_UTF8(*sdpf);
      // Use C API to set remote description
      int setRes = rtcSetRemoteDescription(Conn->PeerConnection, sdp.c_str(), TCHAR_TO_UTF8(*Type));
      if (setRes != RTC_ERR_SUCCESS)
      {
        UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcSetRemoteDescription returned %d for player %d"), setRes, TargetPlayer);
      }
      else
      {
        UE_LOG(LogTemp, Log, TEXT("Synavis: Set remote description for player %d"), TargetPlayer);
      }
      // If remote sent an offer, request the C API to set local description (type NULL lets libdatachannel choose)
      if (Type.Equals(TEXT("offer"), ESearchCase::IgnoreCase))
      {
        int localRes = rtcSetLocalDescription(Conn->PeerConnection, nullptr);
        if (localRes != RTC_ERR_SUCCESS)
        {
          UE_LOG(LogTemp, Warning, TEXT("Synavis: rtcSetLocalDescription returned %d for player %d"), localRes, TargetPlayer);
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
    RegisterRemoteCandidateForConnection(Content, Pair.Value);
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
  bool DedicatedChannel)
{
  FSynavisHandlers Handler;
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
      Handler.Video.Emplace(0, SceneCapture);
    }
  }

  // If a dedicated channel was requested, mark it for creation per-connection; otherwise use the system channel
  if (DedicatedChannel)
  {
    Handler.WantsDedicatedChannel = true;
    Handler.DataChannel = std::nullopt;
  }
  else
  {
    Handler.WantsDedicatedChannel = false;
    Handler.DataChannel = SystemDataChannel;
  }

  RegisteredDataHandlers.Add(Handler);
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
    if (Pair.Value.bStreaming)
    {
      anyStreaming = true;
      break;
    }
  }
  if (!anyStreaming)
    return;

  for (const FSynavisHandlers& Handler : RegisteredDataHandlers)
  {
    if (!Handler.Video.IsSet())
      continue;
    USceneCaptureComponent2D* SceneCapture = Handler.Video.GetValue().Value;
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
    FRHIGPUTextureReadback* ReadbackY = nullptr;
    FRHIGPUTextureReadback* ReadbackUV = nullptr;
    if (EnqueueNV12ReadbackFromRenderTarget(HandlerRT, ReadbackY, ReadbackUV))
    {
      // Gather target track ids across all connections for this handler (only connections
      // that currently request streaming will receive frames).
      TArray<int32> TracksToSend;
      for (const auto& Pair : Connections)
      {
        const FSynavisConnection& Conn = Pair.Value;
        if (!Conn.bStreaming) continue;
        auto it = Conn.TracksByHandler.find(Handler.HandlerID);
        if (it != Conn.TracksByHandler.end() && it->second != 0 && rtcIsOpen(it->second))
        {
          TracksToSend.Add(it->second);
        }
      }

      if (TracksToSend.Num() == 0)
      {
        UE_LOG(LogTemp, Verbose, TEXT("Synavis: No open tracks for handler %d, skipping readback"), Handler.HandlerID);
        // cleanup readbacks immediately on render thread
        FRHIGPUTextureReadback* Yrb = ReadbackY;
        FRHIGPUTextureReadback* UVrb = ReadbackUV;
        ENQUEUE_RENDER_COMMAND(Synavis_CleanupReadbackImmediate)([Yrb, UVrb](FRHICommandListImmediate& RHICmdList)
          {
            if (Yrb) { Yrb->Unlock(); delete Yrb; }
            if (UVrb) { UVrb->Unlock(); delete UVrb; }
          });
      }
      else
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
    }
    else
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: Failed to enqueue NV12 readback for handler"));
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
    return;
  }

  UE_LOG(LogTemp, Warning, TEXT("No open DataChannel or Track to send frame bytes (Name=%s, Format=%s)"), *Name, *Format);
}

void USynavisStreamer::OnDataChannelMessage(const std::variant<TArray<uint8>, std::string>& message)
{
  if (std::holds_alternative<TArray<uint8>>(message))
  {
    const TArray<uint8>& data = std::get<TArray<uint8>>(message);
    // preprocessing of the data type using the control bytes
    // (user-defined handling)
  }
  else if (std::holds_alternative<std::string>(message))
  {
    const std::string& msgStr = std::get<std::string>(message);
    FString Msg = FString(UTF8_TO_TCHAR(msgStr.c_str()));
    // (user-defined handling of textual messages)
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
    if (DispatchTargets.Num() == 0)
    {
      // Gather all open tracks from all connections
      for (const auto& Pair : Connections)
      {
        const FSynavisConnection& Conn = Pair.Value;
        // if TracksByHandler exists for the connection, iterate it
        for (const auto& kv : Conn.TracksByHandler)
        {
          int tr = kv.second;
          if (tr != 0 && rtcIsOpen(tr))
            DispatchTargets.Add(tr);
        }
      }
    }

    if (DispatchTargets.Num() > 0)
    {
      UE_LOG(LogTemp, Verbose, TEXT("Synavis: Sending encoded packet size=%d to %d handler track(s)"), (int)sz, DispatchTargets.Num());
      for (int tr : DispatchTargets)
      {
        if (tr != 0 && rtcIsOpen(tr))
        {
          // Call the VP9 packetizer/sender stub. Replace implementation inside
          // VP9PacketizeAndSend with real VP9 packetization logic when ready.
          VP9PacketizeAndSend(tr, data, sz);
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
    }
    else
    {
      UE_LOG(LogTemp, Warning, TEXT("Synavis: Encoded packet dropped - no open tracks or datachannel"));
    }
    av_packet_unref(LibAVState->Packet);
  }

  av_frame_free(&frame);
}

