// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"



THIRD_PARTY_INCLUDES_START
#include <variant>
#include <unordered_map>
#include <memory>
#include <functional>

#include "RHIGPUReadback.h"
#if 0
// Do NOT include the C++ libdatachannel headers from this public header to avoid
// exposing C++ API types across DLL boundaries. All interaction with libdatachannel
// in this module uses the C API (rtc.h) and integer ids. If you need the C++ API
// in a .cpp file, include <rtc/rtc.hpp> there.
#else
namespace rtc { class PeerConnection; class DataChannel; class WebSocket; class Track; }
#endif
#if defined(LIBAV_AVAILABLE)
extern "C" {
  struct AVCodecContext;
  struct AVFrame;
  struct AVPacket;
  struct AVCodec;
}
#else
// forward-declare to allow pointer members without bringing libav into every compile unit
struct AVCodecContext; struct AVFrame; struct AVPacket; struct AVCodec;
#endif

THIRD_PARTY_INCLUDES_END


#include "SynavisStreamer.generated.h"

class UTextureRenderTarget2D;
class USceneCaptureComponent2D;

DECLARE_DYNAMIC_DELEGATE_OneParam(FSynavisMessage, FString, Message);
DECLARE_DYNAMIC_DELEGATE_OneParam(FSynavisData, const TArray<uint8>&, Data);


UENUM(BlueprintType)
enum class ESynavisSourcePolicy : uint8
{
  RemainStatic      UMETA(DisplayName = "Static: Do not accept connection updates"),
  DynamicOptional   UMETA(DisplayName = "Dynamic Optional: Attempt renegotiation but ignore failures"),
  DynamicMandatory  UMETA(DisplayName = "Dynamic Mandatory: Force renegotiation on updates"),
};

UENUM(BlueprintType)
enum class ESynavisState : uint8
{
  Offline     UMETA(DisplayName = "Offline"),
  SignallingUp   UMETA(DisplayName = "Signalling Up"),
  Negotiating   UMETA(DisplayName = "Negotiating"),
  Connected    UMETA(DisplayName = "Connected"),
  Failure      UMETA(DisplayName = "Failure"),
};

UENUM(BlueprintType)
enum class EPeerState : uint8
{
  NoConnection      UMETA(DisplayName = "No Connection"),
  SynavisConnecting      UMETA(DisplayName = "Synavis Connecting"), // received playerConnected from Signalling
  ReceivedOffer      UMETA(DisplayName = "Received Offer"),
  ReceivedAnswer      UMETA(DisplayName = "Received Answer"),
  ICE      UMETA(DisplayName = "ICE"),
  ChannelOpen      UMETA(DisplayName = "Channel Open"),
  AllOpen      UMETA(DisplayName = "All Open"),
};

// Synavis Handler:
// Represents a registered data source/sink that provides synthetic data streams.
// - Contains handlers for text and binary messages and optional native C++ callbacks.
// - May reference a `USceneCaptureComponent2D` as a video source (validated via its TextureTarget).
// - Can request a dedicated per-handler data channel when a separate channel is required.
// - Maintains per-connection track IDs to efficiently prepare and send encoded video frames.
// - `HandlerID` is a stable identifier used to register/unregister handlers.
struct FSynavisHandler
{
  // Video: Source -> Destination
    // Store the scene capture component pointer so we can validate it (ensure it has a TextureTarget)
  USceneCaptureComponent2D* Video = nullptr;
  // will set FSynavisConnection::DataChannel to not equal to System when opened.
  // Use a name without the `b` prefix to match usage in implementation files.
  bool WantsDedicatedChannel = false;

  // If false, this handler is a source-only registration: it will provide
  // outgoing media (video tracks) but will not receive inbound messages and
  // the streamer will avoid allocating/dispatching callback handlers or
  // datachannel bookkeeping for incoming data. Default = true for backward compatibility.
  bool AcceptsInboundMessages = true;

  // Per-connection map of video track ids: connection id -> track id.
  // Used to look up the specific media track to send encoded frames for a given connection.
  TMap<int32 /*connection id*/, int32 /*track id*/> VideoTracksByConnection;

  int MediaDesc = 0;

  FSynavisData DataHandler;
  FSynavisMessage MsgHandler;
  // Optional C++ callbacks (for registration from native code)
  // These may be left empty for source-only handlers (AcceptsInboundMessages == false)
  std::function<void(int32, const TArray<uint8>&)> DataCbCpp;
  std::function<void(int32, const FString&)> MsgCbCpp;
  uint32 HandlerID = 0;

  // Provide hashing and equality so FSynavisHandlers can be used in UE containers (TSet/TMap)
  friend FORCEINLINE uint32 GetTypeHash(const FSynavisHandler& H)
  {
    // Use the HandlerID as the stable unique key for hashing
    return H.HandlerID;
  }

  friend FORCEINLINE bool operator==(const FSynavisHandler& A, const FSynavisHandler& B)
  {
    return A.HandlerID == B.HandlerID;
  }
};

// Synavis Connection:
// Represents a single PeerConnection and its associated state.
// - Stores C API object ids (PeerConnection, Packetizer, DataChannel) used by the C wrapper layer.
// - `SystemDataChannel` is the global/system data channel used for handlers that do not use
//   a dedicated per-handler channel; handler-domain association is performed via message dispatch.
// - `TracksByHandler` maps handler ids to media track ids to support initialization and teardown.
// - `HandlersByChannel` maps data channel ids to handler ids for efficient message dispatch.
// - Track and channel ids are recorded so the connection can be properly initialized and torn down.
struct FSynavisConnection
{

  /**********************************
   * Connection Objects             *
   * ********************************/
   // C API ids: PeerConnection id, Packetizer id (if used), DataChannel id
  int PeerConnection = 0;
  int Packetizer = 0;
  int DataChannel = 0;

  /**********************************
   * Meta Info on Connection        *
   * ********************************/
  uint32 MaxMessageSize = 0;

  // Map of handler id -> track id for video/audio tracks. Used during media setup/teardown.
  std::unordered_map<uint32, int32> TracksByHandler;
  // Map of data channel id -> handler id for dispatching incoming messages to the correct handler.
  std::unordered_map<int32, uint32> HandlersByChannel;

  int ConnectionID = 0;
  // Per-connection flag indicating whether this connection should receive encoded video
  // frames. This replaces the previous global bStreaming flag which no longer fits
  // the multi-connection model.
  bool bStreaming = false;
  EPeerState State = EPeerState::NoConnection;
  FSynavisConnection() = default;
  FSynavisConnection(const FSynavisConnection&) = delete;
  FSynavisConnection(FSynavisConnection&&) = default;
  FSynavisConnection& operator=(FSynavisConnection&&) = default;
};


UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SYNAVISBACKEND_API USynavisStreamer : public UActorComponent
{
  GENERATED_BODY()

public:
  // Sets default values for this component's properties
  USynavisStreamer();
  virtual ~USynavisStreamer() override;

  UPROPERTY()
  FSynavisMessage MsgBroadcast;

  UPROPERTY()
  FSynavisData DataBroadcast;


protected:
  // Called when the game starts
  virtual void BeginPlay() override;

public:
  // Called every frame
  virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;


  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
  float CaptureFPS = 30.0f;

  // Encoding always uses VP9 when available
  UFUNCTION(BlueprintCallable, Category = "Streaming")
  void StartStreaming();

  // Stop streaming globally for all connections (non-blueprint helper)
  void StopStreaming();

  // Connection Policy for handling additional requests to stream cameras
  // from within Unreal: If set, the streamer will attempt to renegotiate
  // the connection when receiving such requests. By default, we will ignore
  // these requests in the instance that libdatachannel reports that we have
  // an active connection
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Connection")
  ESynavisSourcePolicy SourcePolicy = ESynavisSourcePolicy::RemainStatic;

  // Signalling server configuration for WebRTC (ws://host:port)
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Signalling")
  FString SignallingIP;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Signalling")
  int32 SignallingPort = 9000;

  // If true the streamer will create local SDPs (take the first step / be offerer).
  // Set to false to let remote endpoints offer first and make this component passive.
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Signalling")
  bool bTakeFirstStep = true;

  UFUNCTION(BlueprintCallable, Category = "Streaming|Signalling")
  void StartSignalling();

  // Public notification wrappers used by C API callbacks to safely forward events
  // into this class without violating C++ access control.
  void NotifySignallingOpen();
  void NotifySignallingClose();
  void NotifySignallingError(const std::string& Err);
  void NotifySignallingMessage(const std::variant<TArray<uint8>, std::string>& Message);

  // Public PC/DataChannel notify wrappers so C callbacks can call into this
  // class without touching protected member functions directly.
  void NotifyPcLocalDescription(int pc, const char* sdp, const char* type);
  void NotifyPcGatheringStateChange(int pc, int state);
  void NotifyDataChannelMessage(int dc, const std::variant<TArray<uint8>, std::string>& message);
  void NotifyDataChannelOpen(int dc);
  void NotifyDataChannelClosed(int dc);


  // Called when libdatachannel reports a new datachannel for a PeerConnection.
  // Implemented as a member so it can safely access protected connection maps.
  void HandlePcDataChannelCreated(int pc, int dc);

  UFUNCTION(BlueprintCallable, Category = "Streaming|Connection")
  ESynavisState GetConnectionState() const;

  int SetupDataChannel(const FSynavisHandler& Handler);

  /**
   * Register data source for this streamer instance.
   * @param DataHandler Callback to receive binary data messages
   * @param MsgHandler Callback to receive text messages
   * @param SceneCapture optional reference to a USceneCaptureComponent2D to use as video source. The capture must have a valid TextureTarget.
   * @param DedicatedChannel set to false by default but if true, will trigger the creation of a dedicated DataChannel for this handler
   * @return Handler ID that can be used to unregister later
   */
  UFUNCTION(BlueprintCallable, Category = "Streaming|Data")
  int RegisterDataSource(
    FSynavisData DataHandler,
    FSynavisMessage MsgHandler,
    USceneCaptureComponent2D* SceneCapture = nullptr,
    bool DedicatedChannel = false,
    bool AcceptsInboundMessages = true);

  // C++ registration API: register native callbacks without Blueprint indirection.
  // Returns a stable HandlerID (positive) or 0 on failure.
  int32 RegisterDataSourceCpp(
    const std::function<void(int32, const TArray<uint8>&)>& OnData,
    const std::function<void(int32, const FString&)>& OnMessage,
    USceneCaptureComponent2D* SceneCapture = nullptr,
    bool DedicatedChannel = false,
    bool AcceptsInboundMessages = true);

  // Lightweight registration for video/source-only handlers (no inbound callbacks).
  // Returns a stable HandlerID (positive) or 0 on failure.
  int32 RegisterVideoSourceCpp(USceneCaptureComponent2D* SceneCapture,
    bool DedicatedChannel = false,
    bool AcceptsInboundMessages = false);

  // Blueprint-friendly registration of a source-only video handler (no inbound callbacks).
  UFUNCTION(BlueprintCallable, Category = "Streaming|Data")
  int RegisterVideoSource(USceneCaptureComponent2D* SceneCapture,
    bool DedicatedChannel = false,
    bool AcceptsInboundMessages = false);

  // Unregister a previously registered handler.
  void UnregisterDataSource(int32 HandlerId);

  // Send raw bytes/text to a specific connection via the handler's dedicated channel
  // If ConnectionPlayerID < 0 the message will be broadcast to all connections
  UFUNCTION(BlueprintCallable, Category = "Streaming|Data")
  bool SendTextToConnection(int32 HandlerId, int32 ConnectionPlayerID, const FString& Text);

  // Send a potentially large binary payload by chunking it into DataChannel-friendly pieces.
  bool SendBinaryToConnection(int32 HandlerId, int32 ConnectionPlayerID, const TArray<uint8>& Data);

  bool BroadcastText(int32 HandlerId, const FString& Text);
  bool BroadcastBinary(int32 HandlerId, const TArray<uint8>& Data);

  bool SendTextViaSystemChannel(const FString& Text);
  bool SendBinaryViaSystemChannel(const TArray<uint8>& Data);


  // Send raw encoded frame bytes to a target RTC track or datachannel
  void SendFrameBytes(const TArray<uint8>& Bytes, const FString& Name, const FString& Format, int32 TargetTrackId);

  
  // Find connection by ConnectionID (PlayerID). Returns nullptr if not found.
  FSynavisConnection* FindConnectionByPlayerID(int32 PlayerID);
  const FSynavisConnection* FindConnectionByPlayerID(int32 PlayerID) const;

protected:
  // timer callback to capture frames
  void CaptureFrame();

  UPROPERTY()
  ESynavisState ConnectionState = ESynavisState::Offline;

  // helper for whether we are in game
  FORCEINLINE bool IsInGame()
  {
    UWorld* W = GetWorld();
    return (W != nullptr) && W->IsGameWorld();
  }


  // Pending GPU readback record for non-blocking zero-copy path
  struct FPendingNV12Readback
  {
    FRHIGPUTextureReadback* ReadbackY = nullptr;
    FRHIGPUTextureReadback* ReadbackUV = nullptr;
    double EnqueuedAt = 0.0;
    // optional track to send encoded data to
    TArray<int32> TargetTracks;
    int Width = 0;
    int Height = 0;
  };

  // Pending readbacks queue; processed in TickComponent
  TArray<FPendingNV12Readback> PendingReadbacks;

  // TSet of registered data handlers
  TSet<FSynavisHandler> RegisteredDataHandlers;

  // Next handler id for C++ registrations
  uint32 NextHandlerId = 1;

  void TakeSignallingMessage(const FString& Message);

  // Zero-copy variant: accept FRHIGPUTextureReadback readbacks for Y and UV (NV12). The helper will wrap
  // the readback pointers into AVBufferRefs that free/unlock the readbacks when FFmpeg is done.
  // TargetTracks contains one or more tracks that should receive the encoded packets.
  void EncodeNV12ReadbackAndSend(class FRHIGPUTextureReadback* ReadbackY, class FRHIGPUTextureReadback* ReadbackUV, int Width, int Height, const TArray<int32>& TargetTracks);

  void OnDataChannelMessage(const std::variant<TArray<uint8>, std::string>& message);

  bool TryParseJSON(std::string message, FJsonObject& OutJsonObject);

  // Signalling helpers (ported behavior from DataConnector)
  void CommunicateSDPs();
  void RegisterRemoteCandidate(const FJsonObject& Content);

  // internal state
  // NOTE: streaming is now per-connection (FSynavisConnection::bStreaming). Use
  // StartStreaming/StopStreaming to influence existing connections; new
  // connections default to bStreaming=false and will be enabled by StartStreaming.
  bool AnyConnectionStreaming() const;

  // Remote playerConnected state: only send local SDP after a playerConnected message
  // with dataChannel=true and sfu=false is received from the signalling server.
  bool bPlayerConnected = false;

  std::string WebSocketUri;
  // When using the C API wrapper of libdatachannel we store the created websocket id here.
  // Value 0 indicates no websocket has been created via the C API.
  int SignallingId = 0;

  // Store connections as shared pointers to ensure stable addresses
  // across container rehashes and to allow safe user-pointer references
  // from C API callbacks while enabling shared ownership semantics.
  TMap<int32, TSharedPtr<FSynavisConnection>> Connections;

  // Global/system datachannel used as fallback when per-handler tracks are not available
  int SystemDataChannel = 0;

  // Note: per-connection mapping of datachannel -> handler is stored in
  // FSynavisConnection::HandlersByChannel. No global reverse map is kept.

  // Teardown a connection and free its resources (PeerConnection, DataChannels, Tracks)
  void TeardownConnection(int32 PlayerID);

  // Persistent libav encoder context to avoid allocations per-frame
  struct FLibAVEncoderState
  {
    AVCodecContext* CodecCtx = nullptr;
    AVFrame* Frame = nullptr;
    AVPacket* Packet = nullptr;
    const AVCodec* Codec = nullptr;
    int Width = 0;
    int Height = 0;
    FCriticalSection Mutex;
    FLibAVEncoderState() {}
    ~FLibAVEncoderState(); // defined in cpp
  };
  FLibAVEncoderState* LibAVState = nullptr;

  // Signalling handlers (moved to member functions to reduce lambda use)
  void HandleSignallingOpen();
  void HandleSignallingClose();
  void HandleSignallingError(const std::string& Err);
  void HandleSignallingMessage(const std::variant<TArray<uint8>, std::string>& Message);

  // Callbacks invoked from C API callback wrappers (forwarded to game thread)
  // The integer parameters identify the C API object ids (peer, datachannel, track)
  void HandlePcLocalDescriptionCallback(int pc, const char* sdp, const char* type);
  void HandlePcGatheringStateChangeCallback(int pc, int state);
  void HandleDataChannelMessageCallback(int dc, const std::variant<TArray<uint8>, std::string>& message);
  void HandleDataChannelOpenCallback(int dc);
  void HandleDataChannelClosedCallback(int dc);
  

  // Create a new peerconnection for a remote player identified by PlayerID
  void CreateConnectionForPlayer(int32 PlayerID);
  // Send local SDP for a specific connection via the signalling websocket
  void CommunicateSDPForConnection(const FSynavisConnection& Conn);
  // Register remote ICE candidate for a given connection (content contains candidate obj)
  void RegisterRemoteCandidateForConnection(const FJsonObject& Content, FSynavisConnection& Conn);

  // Stop streaming for a specific connection (marks connection not to receive video).
  UFUNCTION(BlueprintCallable, Category = "Streaming")
  void StopStreaming(int32 ConnectionID);
};
