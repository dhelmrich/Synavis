// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"



THIRD_PARTY_INCLUDES_START
#include <variant>
#include <unordered_map>
#include <memory>
#include <functional>
#include <atomic>

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

// Forward-declare the shared encoder state (actual definition in SynavisVp9SendoffHandler.h)
struct FLibAVEncoderState;

THIRD_PARTY_INCLUDES_END


#include "SynavisStreamer.generated.h"

class UTextureRenderTarget2D;
class USceneCaptureComponent2D;
class USynavisStreamer;
class USynavisVp9SendoffHandler;

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
  // If true, a negotiation request is pending for this connection because tracks
  // or channels were added while global negotiation was held.
  bool PendingNegotiation = false;
  EPeerState State = EPeerState::NoConnection;
  // Hold converted UTF-8 bytes for track identifiers so pointers remain valid
  TArray<TArray<ANSICHAR>> PersistentTrackUtf8;
  // Thread-safe SSRC generator for tracks
  FSynavisConnection() = default;
  FSynavisConnection(const FSynavisConnection&) = delete;
  FSynavisConnection(FSynavisConnection&&) = default;
  FSynavisConnection& operator=(FSynavisConnection&&) = default;

  

  // Helper in your Conn class header
  TArray<ANSICHAR>* AddPersistentUtf8(const FString& Str);
};

// Per-datachannel context stored for each datachannel. Instances are owned
// by the USynavisStreamer via `DataChannelContexts` to centralize lifetime
// management and provide a single authoritative place for diagnostics.
struct DataChannelCtx
{
  // Leading magic to detect writes before the structure
  uint64_t CANARY_FRONT = 0xDEADBEEFCAFEBABEULL;

  // Actual context fields
  USynavisStreamer* Streamer = nullptr;
  int32 ConnectionID = 0;
  uint32 HandlerID = 0;
  int PeerPC = 0;
  FSynavisConnection* ConnRaw = nullptr;

  // Trailing magic to detect writes after the structure
  uint64_t CANARY_BACK = 0xDEADBEEFCAFEBABEULL;
};


UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SYNAVISBACKEND_API USynavisStreamer : public UActorComponent
{
  GENERATED_BODY()

public:
  // Sets default values for this component's properties
  USynavisStreamer();
  virtual ~USynavisStreamer() override;

  // Return a monotonic, thread-safe SSRC value for newly-created tracks
  uint32_t GetNextSSRC();

  // Centralized helpers to manage per-datachannel contexts from external C callbacks
  void AddDataChannelContext(int32 DcId, TSharedPtr<struct DataChannelCtx> Ctx);
  void RemoveDataChannelContext(int32 DcId);
  // Thread-safe accessor for callbacks to find the authoritative ctx by datachannel id
  TSharedPtr<struct DataChannelCtx> GetDataChannelContext(int32 DcId) const;

  UPROPERTY()
  FSynavisMessage MsgBroadcast;

  UPROPERTY()
  FSynavisData DataBroadcast;

    // Centralized non-blocking sendoff handler (encodes+packetizes+sends on workers)
    UPROPERTY()
    USynavisVp9SendoffHandler* SendoffHandler = nullptr;

  // Resolve channel -> connection mapping on the game thread and dispatch message
  void ResolveAndHandleDataChannelMessage(int dc, const std::variant<TArray<uint8>, std::string>& message);

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

  // Maximum message size to request from libdatachannel (bytes). Set to 0 for library default.
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Signalling")
  int32 MaxMessageSize = 262144;

  // Optional MTU hint (bytes). Set to 0 to use automatic/default.
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Signalling")
  int32 Mtu = 0;

  // Optional explicit payload type to use for outgoing VP9 RTP packets.
  // Set to 96 by default (dynamic range 96-127). Use -1 for library default.
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Signalling")
  int32 VideoPayloadType = 96;

  // If true the streamer will create local SDPs (take the first step / be offerer).
  // Set to false to let remote endpoints offer first and make this component passive.
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Signalling")
  bool bTakeFirstStep = true;

  // If true, hold any negotiation/offer creation until StartConnectionNegotiation()
  // is called from the editor or Blueprint. Allows registering handlers in construction
  // scripts before PeerConnection offers are created.
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Signalling")
  bool bHoldNegotiation = true;

  UFUNCTION(BlueprintCallable, Category = "Streaming|Signalling")
  void StartSignalling();


  // Called when libdatachannel reports a new datachannel for a PeerConnection.
  // Implemented as a member so it can safely access protected connection maps.
  void HandlePcDataChannelCreated(int pc, int dc);

  UFUNCTION(BlueprintCallable, Category = "Streaming|Connection")
  ESynavisState GetConnectionState() const;

  int SetupDataChannel(const FSynavisHandler& Handler);

  uint64 UniqueIdenfifier()
  {
    return NextUniqueIdentifier++;
  }

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

  // Find connection by PeerConnection id (pc). Returns nullptr if not found.
  FSynavisConnection* GetConnectionFromPC(int pc);
  const FSynavisConnection* GetConnectionFromPC(int pc) const;

  // Find registered handler by HandlerID. Returns nullptr if not found.
  FSynavisHandler* GetHandlerById(uint32 HandlerId);
  const FSynavisHandler* GetHandlerById(uint32 HandlerId) const;

  // Return the first registered handler that has a video source but does not
  // yet have a video track mapping for the provided connection. Returns
  // nullptr if none found.
  FSynavisHandler* FirstWithoutVideoTrack(struct FSynavisConnection* Conn);
  const FSynavisHandler* FirstWithoutVideoTrack(const struct FSynavisConnection* Conn) const;

  // Diagnostic: print RTC-related state (channels, tracks) for all connections
  void RTCReport() const;

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
  struct FPendingI420Readback
  {
    FRHIGPUTextureReadback* ReadbackY = nullptr;
    // Separate U and V half-resolution readbacks (I420 layout)
    FRHIGPUTextureReadback* ReadbackU = nullptr;
    FRHIGPUTextureReadback* ReadbackV = nullptr;
    double EnqueuedAt = 0.0;
    // optional track to send encoded data to
    TArray<int32> TargetTracks;
    int Width = 0;
    int Height = 0;
  };

  // Pending readbacks queue; processed in TickComponent
  TArray<FPendingI420Readback> PendingReadbacks;

  // TSet of registered data handlers
  TSet<FSynavisHandler> RegisteredDataHandlers;

  // Next handler id for C++ registrations
  uint32 NextHandlerId = 1;

  void TakeSignallingMessage(const FString& Message);

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

  // Central container that owns per-datachannel context objects. Keys are
  // datachannel ids returned by the C API (rtcCreateDataChannel / OnPcDataChannel).
  // Stored as a heap-allocated pointer to make the container address stable
  // and to avoid accidental placement on stack-like/embedded storage.
  TUniquePtr<TMap<int32, TSharedPtr<DataChannelCtx>>> DataChannelContexts;

  // Mutex protecting DataChannelContexts for thread-safe access from arbitrary
  // callback threads.
  mutable FCriticalSection DataChannelContextsMutex;

  // Pending remote answers queued per-peer-connection id. Protected by mutex
  // because C API callbacks may arrive on arbitrary threads.
  mutable FCriticalSection PendingAnswersMutex;
  TMap<int32, TArray<FString>> PendingRemoteAnswers;

  // Note: per-connection mapping of datachannel -> handler is stored in
  // FSynavisConnection::HandlersByChannel. No global reverse map is kept.

  // Teardown a connection and free its resources (PeerConnection, DataChannels, Tracks)
  void TeardownConnection(int32 PlayerID);

  // Persistent libav encoder context shared with the sendoff handler.
  // Type defined in SynavisVp9SendoffHandler.h; forward-declared above.
  FLibAVEncoderState* LibAVState = nullptr;

  std::atomic<uint32_t> NextSSRC {1001};
public:

  // Signalling handlers (moved to member functions to reduce lambda use)
  void HandleSignallingOpen();
  void HandleSignallingClose();
  void HandleSignallingError(const std::string& Err);
  void HandleSignallingMessage(const std::variant<TArray<uint8>, std::string>& Message);

  // Callbacks invoked from C API callback wrappers (forwarded to game thread)
  // The integer parameters identify the C API object ids (peer, datachannel, track)
  void HandlePcLocalDescriptionCallback(int pc, const char* sdp, const char* type);
  void HandlePcGatheringStateChangeCallback(int pc, int state);
  void HandlePcSignalingStateChangeCallback(int pc, int state);
  void HandleDataChannelMessageCallback(int dc, const std::variant<TArray<uint8>, std::string>& message);
  void HandleDataChannelOpenCallback(int dc);
  void HandleDataChannelClosedCallback(int dc);
  

  // Create a new peerconnection for a remote player identified by PlayerID
  void CreateConnectionForPlayer(int32 PlayerID);
  // Trigger renegotiation for a specific connection (internal helper)
  void TriggerRenegotiationForConnection(struct FSynavisConnection* Conn);
  // Trigger any pending/required negotiation for all connections. This is intended
  // to be called from editor/blueprint once handler registration is complete.
  UFUNCTION(BlueprintCallable, Category = "Streaming|Signalling")
  void StartConnectionNegotiation();
  // Drain any pending remote answers for a given peer connection id
  void DrainPendingAnswersForPC(int pc);
  // Send local SDP for a specific connection via the signalling websocket
  void CommunicateSDPForConnection(const FSynavisConnection& Conn);
  // Register remote ICE candidate for a given connection (content contains candidate obj)
  void RegisterRemoteCandidateForConnection(const FJsonObject& Content, FSynavisConnection& Conn);

  // Stop streaming for a specific connection (marks connection not to receive video).
  UFUNCTION(BlueprintCallable, Category = "Streaming")
  void StopStreaming(int32 ConnectionID);

  uint64 NextUniqueIdentifier = 1;
};
