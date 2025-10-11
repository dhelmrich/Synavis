// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include <memory>
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


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SYNAVISBACKEND_API USynavisStreamer : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	USynavisStreamer();
	virtual ~USynavisStreamer();

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
	void SetRenderTarget(class UTextureRenderTarget2D* InTarget);

	UFUNCTION(BlueprintCallable, Category = "Streaming")
	void SetCaptureFPS(float FPS);

	// no setter: encoding uses VP9 by default

	// Signalling server configuration for WebRTC (ws://host:port)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Signalling")
	FString SignallingIP;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Signalling")
	int32 SignallingPort = 9000;

	UFUNCTION(BlueprintCallable, Category = "Streaming|Signalling")
	void StartSignalling();

protected:
	// timer callback to capture frames
	void CaptureFrame();

	// Pending GPU readback record for non-blocking zero-copy path
	struct FPendingNV12Readback
	{
		FRHIGPUTextureReadback* ReadbackY = nullptr;
		FRHIGPUTextureReadback* ReadbackUV = nullptr;
		double EnqueuedAt = 0.0;
	};

	// Pending readbacks queue; processed in TickComponent
	TArray<FPendingNV12Readback> PendingReadbacks;

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

	// internal state
	bool bStreaming = false;

				// Opaque pimpl for WebRTC internals (defined here so UHT sees a complete type)
				struct FWebRTCInternal
				{
						std::shared_ptr<rtc::PeerConnection> PeerConnection;
						std::shared_ptr<rtc::DataChannel> DataChannel;
						std::shared_ptr<rtc::WebSocket> Signalling;
						std::shared_ptr<rtc::Track> VideoTrack;
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
