// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
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

struct FSynavisHandlers
{
  // Video: Source -> Destination
	// a TOptional<TPair<std::shared_ptr<rtc::Track>, USceneCaptureComponent2D*>>
	// Store the scene capture component so we can validate it (ensure it has a TextureTarget)
	TOptional<TPair<std::shared_ptr<rtc::Track>, USceneCaptureComponent2D*>> Video;

  std::optional<std::shared_ptr<rtc::DataChannel>> DataChannel;

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

	UFUNCTION(BlueprintCallable, Category = "Streaming")
	void StopStreaming();

	UFUNCTION(BlueprintCallable, Category = "Streaming")
	void SetCaptureFPS(float FPS);

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
		std::shared_ptr<rtc::Track> TargetTrack = nullptr;
		int Width = 0;
		int Height = 0;
	};

	// Pending readbacks queue; processed in TickComponent
	TArray<FPendingNV12Readback> PendingReadbacks;

  // TSet of registered data handlers
  TSet<FSynavisHandlers> RegisteredDataHandlers;

	// Zero-copy variant: accept FRHIGPUTextureReadback readbacks for Y and UV (NV12). The helper will wrap
	// the readback pointers into AVBufferRefs that free/unlock the readbacks when FFmpeg is done.
	// TargetTrack is required and the encoded packets will be sent to that track.
	void EncodeNV12ReadbackAndSend(class FRHIGPUTextureReadback* ReadbackY, class FRHIGPUTextureReadback* ReadbackUV, int Width, int Height, std::shared_ptr<rtc::Track> TargetTrack);

  void OnDataChannelMessage(const rtc::message_variant& message);

  bool TryParseJSON(std::string message, FJsonObject& OutJsonObject);

	// Signalling helpers (ported behavior from DataConnector)
	void CommunicateSDPs();
	void RegisterRemoteCandidate(const FJsonObject& Content);

	// internal state
	bool bStreaming = false;

	bool bPlayerConnected = false;

	// Opaque pimpl for WebRTC internals (defined here so UHT sees a complete type)
	struct FWebRTCInternal
	{
			std::shared_ptr<rtc::PeerConnection> PeerConnection;
			std::shared_ptr<rtc::DataChannel> SystemDataChannel;
			std::shared_ptr<rtc::WebSocket> Signalling;
			std::shared_ptr<rtc::RtpPacketizer> Packetizer;
			FWebRTCInternal() {}
			~FWebRTCInternal(); // defined in cpp
	};
	FWebRTCInternal* WebRTCInternal = nullptr;

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
};
