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
  // a TOptional<TPair<std::shared_ptr<rtc::Track>, UTextureRenderTarget2D*>>
  TOptional<TPair<std::shared_ptr<rtc::Track>, UTextureRenderTarget2D*>> Video;

  std::optional<std::shared_ptr<rtc::DataChannel>> DataChannel;

  FSynavisData DataHandler;
  FSynavisMessage MsgHandler;
  uint32 HandlerID = 0;
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
   * @param VideoSource optional reference to a UTextureRenderTarget2D to use as video source
   * @param DedicatedChannel set to false by default but if true, will trigger the creation of a dedicated DataChannel for this handler
   * @return Handler ID that can be used to unregister later
   */
	UFUNCTION(BlueprintCallable, Category = "Streaming|Data")
	int RegisterDataSource(
		FSynavisData DataHandler,
		FSynavisMessage MsgHandler,
		UTextureRenderTarget2D* VideoSource = nullptr,
		bool DedicatedChannel = false);


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
	};

	// Pending readbacks queue; processed in TickComponent
	TArray<FPendingNV12Readback> PendingReadbacks;

  // TSet of registered data handlers
  TSet<FSynavisHandlers> RegisteredDataHandlers;

	// Encode planar I420 buffers using libav and send via WebRTC (defined in cpp)
	void EncodeI420AndSend(const TArray<uint8>& Y, const TArray<uint8>& U, const TArray<uint8>& V, int Width, int Height);

	// Encode NV12 buffers using libav and send via WebRTC (Y plane + packed interleaved UV)
	// Y: size Width*Height, UV: size (Width * Height)/2, typical strides: YStride=Width, UVStride=Width
	void EncodeNV12AndSend(const TArray<uint8>& Y, const TArray<uint8>& UV, int YStride, int UVStride, int Width, int Height);

	// Zero-copy variant: accept FRHIGPUTextureReadback readbacks for Y and UV (NV12). The helper will wrap
	// the readback pointers into AVBufferRefs that free/unlock the readbacks when FFmpeg is done.
	void EncodeNV12ReadbackAndSend(class FRHIGPUTextureReadback* ReadbackY, class FRHIGPUTextureReadback* ReadbackUV, int Width, int Height);

	// helper to send bytes via DataConnector (use UE types in public API)
	void SendFrameBytes(const TArray<uint8>& Bytes, const FString& Name, const FString& Format);

  void OnDataChannelMessage(const rtc::message_variant& message);

  bool TryParseJSON(std::string message, FJsonObject& OutJsonObject);

	// internal state
	bool bStreaming = false;

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
