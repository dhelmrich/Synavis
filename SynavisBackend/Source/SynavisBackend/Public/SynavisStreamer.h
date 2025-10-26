// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"



THIRD_PARTY_INCLUDES_START
#include <variant>
#include <unordered_map>
#include <memory>

#include "RHIGPUReadback.h"
#if __has_include(<rtc/rtc.hpp>)
#include <rtc/rtc.hpp>
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

struct FSynavisHandlers
{
  // Video: Source -> Destination
	// a TOptional<TPair<std::shared_ptr<rtc::Track>, USceneCaptureComponent2D*>>
	// Store the scene capture component so we can validate it (ensure it has a TextureTarget)
	TOptional<TPair<std::shared_ptr<rtc::Track>, USceneCaptureComponent2D*>> Video;

	std::optional<std::shared_ptr<rtc::DataChannel>> DataChannel;
	// Media description created at registration time; actual rtc::Track is created per-connection
	std::shared_ptr<rtc::Description::Video> MediaDesc;
	// If true, the handler requested a dedicated datachannel; this will be created per-connection
	bool WantsDedicatedChannel = false;

  FSynavisData DataHandler;
  FSynavisMessage MsgHandler;
  uint32 HandlerID = 0;
  
	// Provide hashing and equality so FSynavisHandlers can be used in UE containers (TSet/TMap)
	friend FORCEINLINE uint32 GetTypeHash(const FSynavisHandlers& H)
	{
		// Use the HandlerID as the stable unique key for hashing
		return H.HandlerID;
	}

	friend FORCEINLINE bool operator==(const FSynavisHandlers& A, const FSynavisHandlers& B)
	{
		return A.HandlerID == B.HandlerID;
	}
};

struct FSynavisConnection
{

  /**********************************
   * Connection Objects             *
   * ********************************/
  std::shared_ptr<rtc::PeerConnection> PeerConnection;
  std::shared_ptr<rtc::RtpPacketizer> Packetizer;
  std::shared_ptr<rtc::DataChannel> DataChannel;

  /**********************************
   * Media Objects                  *
   * ********************************/
		// Per-connection mapping from registered handler ID to an outbound Track
		std::unordered_map<uint32, std::shared_ptr<rtc::Track>> TracksByHandler;

	// Per-connection mapping from handler ID to a dedicated DataChannel (if requested)
	std::unordered_map<uint32, std::shared_ptr<rtc::DataChannel>> DataChannelsByHandler;

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


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
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
	/** Blueprint-accessible API to configure and control the streamer */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming")
	class UTextureRenderTarget2D* RenderTarget;

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

	UFUNCTION(BlueprintCallable, Category = "Streaming|Signalling")
	void StartSignalling();

  UFUNCTION(BlueprintCallable, Category = "Streaming|Connection")
  ESynavisState GetConnectionState() const;

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
		bool DedicatedChannel = false);

    
    // Send raw encoded frame bytes to a target RTC track or datachannel
    void SendFrameBytes(const TArray<uint8>& Bytes, const FString& Name, const FString& Format, std::shared_ptr<rtc::Track> TargetTrack);

protected:
	// timer callback to capture frames
	void CaptureFrame();

  UPROPERTY()
  ESynavisState ConnectionState = ESynavisState::Offline;



	// Pending GPU readback record for non-blocking zero-copy path
	struct FPendingNV12Readback
	{
		FRHIGPUTextureReadback* ReadbackY = nullptr;
		FRHIGPUTextureReadback* ReadbackUV = nullptr;
		double EnqueuedAt = 0.0;
		// optional track to send encoded data to
		TArray<std::shared_ptr<rtc::Track>> TargetTracks;
		int Width = 0;
		int Height = 0;
	};

	// Pending readbacks queue; processed in TickComponent
	TArray<FPendingNV12Readback> PendingReadbacks;

  // TSet of registered data handlers
  TSet<FSynavisHandlers> RegisteredDataHandlers;

  void TakeSignallingMessage(const FString& Message);

	// Zero-copy variant: accept FRHIGPUTextureReadback readbacks for Y and UV (NV12). The helper will wrap
	// the readback pointers into AVBufferRefs that free/unlock the readbacks when FFmpeg is done.
	// TargetTracks contains one or more tracks that should receive the encoded packets.
	void EncodeNV12ReadbackAndSend(class FRHIGPUTextureReadback* ReadbackY, class FRHIGPUTextureReadback* ReadbackUV, int Width, int Height, const TArray<std::shared_ptr<rtc::Track>>& TargetTracks);

  void OnDataChannelMessage(const rtc::message_variant& message);

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
  
  std::shared_ptr<rtc::WebSocket> Signalling;

	TMap<int32, FSynavisConnection> Connections;

	// Global/system datachannel used as fallback when per-handler tracks are not available
	std::shared_ptr<rtc::DataChannel> SystemDataChannel;

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
	void HandleSignallingMessage(const std::variant<rtc::binary, std::string>& Message);

	// Create a new peerconnection for a remote player identified by PlayerID
	void CreateConnectionForPlayer(int32 PlayerID);
	// Send local SDP for a specific connection via the signalling websocket
	void CommunicateSDPForConnection(const FSynavisConnection& Conn);
	// Register remote ICE candidate for a given connection (content contains candidate obj)
	void RegisterRemoteCandidateForConnection(const FJsonObject& Content, FSynavisConnection& Conn);
	// Find connection by ConnectionID (PlayerID). Returns nullptr if not found.
	FSynavisConnection* FindConnectionByPlayerID(int32 PlayerID);
	const FSynavisConnection* FindConnectionByPlayerID(int32 PlayerID) const;
    
	// Stop streaming for a specific connection (marks connection not to receive video).
	UFUNCTION(BlueprintCallable, Category = "Streaming")
	void StopStreaming(int32 ConnectionID);
};
