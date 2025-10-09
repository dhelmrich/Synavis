// Fill out your copyright notice in the Description page of Project Settings.


#include "SynavisStreamer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "RHICommandList.h"
#include "RendererInterface.h"
#include "RenderUtils.h"
#include "Engine/Texture2D.h"
#include "Misc/ScopeLock.h"
#include <memory>
#include <rtc/rtc.hpp>
#include <rtc/websocket.hpp>
#include <string>
#include <span>
#include "Async/Async.h"
#include "SynavisStreamerRendering.h"

// libvpx guard: define LIBVPX_AVAILABLE in your build if libvpx is linked
#if defined(LIBVPX_AVAILABLE)
#include <vpx/vpx_encoder.h>
#include <vpx/vp9cx.h>
#include <vpx/vpx_codec.h>
#include <vpx/vpx_image.h>
#endif

using rtc::binary;

// Sets default values for this component's properties
USynavisStreamer::USynavisStreamer()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}


// Called when the game starts
void USynavisStreamer::BeginPlay()
{
	Super::BeginPlay();

	// ...
	
}


// Called every frame
void USynavisStreamer::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bStreaming || !RenderTarget)
		return;

	// Attempt GPU conversion via render-queue transfer into CPU buffers
	TArray<uint8> Ybuf, Ubuf, Vbuf;
	bool bUsedGPU = Synavis::ConvertRenderTargetToI420_GPU(RenderTarget, Ybuf, Ubuf, Vbuf);
	if (bUsedGPU)
	{
		int YWidth = RenderTarget->SizeX;
		int YHeight = RenderTarget->SizeY;
		// Offload libvpx encoding to worker thread using the plane buffers
		Async(EAsyncExecution::ThreadPool, [this, Ybuf = MoveTemp(Ybuf), Ubuf = MoveTemp(Ubuf), Vbuf = MoveTemp(Vbuf), YWidth, YHeight]() mutable {
#if defined(LIBVPX_AVAILABLE)
			vpx_image_t img;
			if (!vpx_img_alloc(&img, VPX_IMG_FMT_I420, YWidth, YHeight, 1))
			{
				return;
			}
			for (int y = 0; y < YHeight; ++y)
			{
				memcpy(img.planes[0] + y * img.stride[0], Ybuf.GetData() + y * YWidth, YWidth);
			}
			int ChW = (YWidth + 1) / 2;
			int ChH = (YHeight + 1) / 2;
			for (int y = 0; y < ChH; ++y)
			{
				memcpy(img.planes[1] + y * img.stride[1], Ubuf.GetData() + y * ChW, ChW);
				memcpy(img.planes[2] + y * img.stride[2], Vbuf.GetData() + y * ChW, ChW);
			}

			vpx_codec_enc_cfg_t cfg;
			vpx_codec_enc_config_default(vpx_codec_vp9_cx(), &cfg, 0);
			cfg.g_w = YWidth;
			cfg.g_h = YHeight;
			cfg.g_timebase.num = 1;
			cfg.g_timebase.den = 30;

			vpx_codec_ctx_t encoder;
			if (vpx_codec_enc_init(&encoder, vpx_codec_vp9_cx(), &cfg, 0))
			{
				vpx_img_free(&img);
				return;
			}

			if (vpx_codec_encode(&encoder, &img, 0, 1, 0, VPX_DL_REALTIME))
			{
				vpx_codec_destroy(&encoder);
				vpx_img_free(&img);
				return;
			}

			vpx_codec_iter_t iter = nullptr;
			const vpx_codec_cx_pkt_t* pkt = nullptr;
			while ((pkt = vpx_codec_get_cx_data(&encoder, &iter)) != nullptr)
			{
				if (pkt->kind == VPX_CODEC_CX_FRAME_PKT)
				{
					const uint8_t* data = reinterpret_cast<const uint8_t*>(pkt->data.frame.buf);
					size_t sz = static_cast<size_t>(pkt->data.frame.sz);
					if (WebRTCInternal)
					{
						if (WebRTCInternal->VideoTrack && WebRTCInternal->VideoTrack->isOpen())
						{
							rtc::binary pktbuf(sz);
							memcpy(pktbuf.data(), data, sz);
							WebRTCInternal->VideoTrack->send(pktbuf);
						}
						else if (WebRTCInternal->DataChannel && WebRTCInternal->DataChannel->isOpen())
						{
							static thread_local rtc::binary dcbuf;
							dcbuf.resize(sz);
							memcpy(dcbuf.data(), data, sz);
							WebRTCInternal->DataChannel->sendBuffer(dcbuf);
						}
					}
				}
			}

			vpx_codec_destroy(&encoder);
			vpx_img_free(&img);
#else
			if (WebRTCInternal && WebRTCInternal->DataChannel && WebRTCInternal->DataChannel->isOpen())
			{
				// not a great fallback; we could repackage RGB into DataChannel
			}
#endif
		});
		return;
	}

	// If GPU path not used or not ready fall back to the earlier CPU readback path
	// Use FRHIGPUTextureReadback (UE5.1+) for async readback
	static TUniquePtr<FRHIGPUTextureReadback> TextureReadback;
	static bool bReadbackPending = false;

	if (!TextureReadback)
	{
		TextureReadback.Reset(RHICreateTextureReadback(TEXT("SynavisReadback")));
	}

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
		return;

	int Width = RenderTarget->SizeX;
	int Height = RenderTarget->SizeY;

	// issue a readback if none outstanding
	if (!bReadbackPending && TextureReadback.IsValid())
	{
		ENQUEUE_RENDER_COMMAND(CopyToReadbackCmd)(
			[RTTexture = RTResource->GetRenderTargetTexture(), Readback = TextureReadback.Get()](FRHICommandListImmediate& RHICmdList)
			{
				if (RTTexture && Readback)
				{
					RHICmdList.CopyTextureToReadback(RTTexture, Readback->GetReadback());
				}
			}
		);
		bReadbackPending = true;
	}
}

void USynavisStreamer::SetRenderTarget(UTextureRenderTarget2D* InTarget)
{
	RenderTarget = InTarget;
}

void USynavisStreamer::SetCaptureFPS(float FPS)
{
	CaptureFPS = FMath::Max(1.0f, FPS);
	if (bStreaming && GetWorld())
	{
		StopStreaming();
		StartStreaming();
	}
}

// WebRTC internal implementation
struct USynavisStreamer::FWebRTCInternal
{
	std::shared_ptr<rtc::PeerConnection> PeerConnection;
	std::shared_ptr<rtc::DataChannel> DataChannel;
	std::shared_ptr<rtc::WebSocket> Signalling;
    // optional outgoing video track (send-only)
    std::shared_ptr<rtc::Track> VideoTrack;

	FWebRTCInternal() {}
	~FWebRTCInternal()
	{
		if (DataChannel)
			DataChannel->close();
		if (PeerConnection)
			PeerConnection->close();
		if (Signalling)
			Signalling->close();
	}
};

void USynavisStreamer::StartStreaming()
{
	if (bStreaming)
		return;
	bStreaming = true;
}

void USynavisStreamer::StopStreaming()
{
	if (!bStreaming)
		return;
	bStreaming = false;
}

void USynavisStreamer::StartSignalling()
{
	// Initialize libdatachannel components and create a datachannel
	if (!WebRTCInternal)
		WebRTCInternal = MakeUnique<FWebRTCInternal>();
	static bool bInitDone = false;
	if (!bInitDone) { rtc::InitLogger(rtc::LogLevel::Info); bInitDone = true; }
	WebRTCInternal->PeerConnection = std::make_shared<rtc::PeerConnection>();

	// create datachannel
	WebRTCInternal->DataChannel = WebRTCInternal->PeerConnection->createDataChannel("synavis-stream");
	WebRTCInternal->DataChannel->onOpen([]() { /* no-op */ });
	WebRTCInternal->DataChannel->onClosed([]() { /* no-op */ });
	WebRTCInternal->DataChannel->onError([](std::string err) { /* no-op */ });

	// Create a send-only VP9 track if possible (mirrors libdatachannel examples)
	try {
		rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
		media.addVP9Codec(96);
		media.setBitrate(90000);
		WebRTCInternal->VideoTrack = WebRTCInternal->PeerConnection->addTrack(media);
		if (WebRTCInternal->VideoTrack)
		{
			WebRTCInternal->VideoTrack->onOpen([]() { /* no-op */ });
		}
	} catch (...) {
		// ignore failures to create media track
	}

	// handle incoming signalling messages if you want to accept answers via websocket
	// We will just create the local offer and log it; the user should copy/paste the JSON or wire a websocket
	WebRTCInternal->PeerConnection->onGatheringStateChange([this](rtc::PeerConnection::GatheringState state) {
		if (state == rtc::PeerConnection::GatheringState::Complete) {
			auto description = WebRTCInternal->PeerConnection->localDescription();
			if (description)
			{
				// Build a minimal JSON offer string using FString to avoid external JSON dependency
				std::string sdpStr = description.value();
				FString Offer = FString::Printf(TEXT("{\"type\":\"%s\",\"sdp\":\"%s\"}"),
					*FString(description->typeString().c_str()), *FString(sdpStr.c_str()));
				UE_LOG(LogTemp, Log, TEXT("WebRTC Offer: %s"), *Offer);
			}
		}
	});

	WebRTCInternal->PeerConnection->setLocalDescription();
}

void USynavisStreamer::CaptureFrame()
{
	if (!RenderTarget || !Connector)
		return;

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
		return;

	int Width = RenderTarget->SizeX;
	int Height = RenderTarget->SizeY;

	TArray<FColor> Bitmap;
	Bitmap.AddUninitialized(Width * Height);

	// read pixels from render target (game thread safe call)
	bool bRead = RTResource->ReadPixels(Bitmap);
	if (!bRead)
	{
		return;
	}

	// always attempt VP9 when libvpx is available; otherwise fallback to raw RGBA
	FString Name = TEXT("frame");
#if defined(LIBVPX_AVAILABLE)
	{
		if (EncodeFrameToVP9AndSend(Bitmap, Width, Height))
			return;
	}
#endif

	// fallback: send raw RGBA
	// send as bytes (RGBA8)
	size_t BytesPerPixel = 4;
	size_t TotalSize = Width * Height * BytesPerPixel;
	// allocate contiguous buffer using UE types
	TArray<uint8> RawBuffer;
	RawBuffer.SetNumUninitialized(TotalSize);
	for (int i = 0; i < Width * Height; ++i)
	{
		const FColor& C = Bitmap[i];
		RawBuffer[i * 4 + 0] = C.R;
		RawBuffer[i * 4 + 1] = C.G;
		RawBuffer[i * 4 + 2] = C.B;
    if (WebRTCInternal)
    {
        delete WebRTCInternal;
        WebRTCInternal = nullptr;
    }
		RawBuffer[i * 4 + 3] = C.A;
	}

	SendFrameBytes(RawBuffer, Name, TEXT("raw_rgba"));
}

// EncodeFrameToVP9 removed: use EncodeFrameToVP9AndSend to encode and stream without allocating an output buffer.

bool USynavisStreamer::EncodeFrameToVP9AndSend(const TArray<FColor>& Pixels, int Width, int Height)
{
#if defined(LIBVPX_AVAILABLE)
	vpx_image_t img;
	if (!vpx_img_alloc(&img, VPX_IMG_FMT_I420, Width, Height, 1))
	{
		return false;
	}

	// Convert RGBA -> I420 (naive) - consider libyuv for performance
	for (int y = 0; y < Height; ++y)
	{
		for (int x = 0; x < Width; ++x)
		{
			int si = (y * Width + x);
			const FColor& C = Pixels[si];
			uint8_t r = C.R;
			uint8_t g = C.G;
			uint8_t b = C.B;
			int Y = ( 66 * r + 129 * g +  25 * b + 128) >> 8; Y += 16;
			int U = (-38 * r -  74 * g + 112 * b + 128) >> 8; U += 128;
			int V = (112 * r -  94 * g -  18 * b + 128) >> 8; V += 128;
			Y = FMath::Clamp(Y, 0, 255);
			U = FMath::Clamp(U, 0, 255);
			V = FMath::Clamp(V, 0, 255);
			img.planes[0][y * img.stride[0] + x] = static_cast<uint8_t>(Y);
			if ((y % 2 == 0) && (x % 2 == 0))
			{
				int cx = x / 2;
				int cy = y / 2;
				img.planes[1][cy * img.stride[1] + cx] = static_cast<uint8_t>(U);
				img.planes[2][cy * img.stride[2] + cx] = static_cast<uint8_t>(V);
			}
		}
	}

	vpx_codec_enc_cfg_t cfg;
	vpx_codec_enc_config_default(vpx_codec_vp9_cx(), &cfg, 0);
	cfg.g_w = Width;
	cfg.g_h = Height;
	cfg.g_timebase.num = 1;
	cfg.g_timebase.den = 30;

	vpx_codec_ctx_t encoder;
	if (vpx_codec_enc_init(&encoder, vpx_codec_vp9_cx(), &cfg, 0))
	{
		vpx_img_free(&img);
		return false;
	}

	if (vpx_codec_encode(&encoder, &img, 0, 1, 0, VPX_DL_REALTIME))
	{
		vpx_codec_destroy(&encoder);
		vpx_img_free(&img);
		return false;
	}

	vpx_codec_iter_t iter = nullptr;
	const vpx_codec_cx_pkt_t* pkt = nullptr;
	bool sent = false;
	while ((pkt = vpx_codec_get_cx_data(&encoder, &iter)) != nullptr)
	{
		if (pkt->kind == VPX_CODEC_CX_FRAME_PKT)
		{
			const uint8_t* data = reinterpret_cast<const uint8_t*>(pkt->data.frame.buf);
			size_t sz = static_cast<size_t>(pkt->data.frame.sz);
			if (WebRTCInternal)
			{
				if (WebRTCInternal->VideoTrack && WebRTCInternal->VideoTrack->isOpen())
				{
					rtc::binary pktbuf(sz);
					memcpy(pktbuf.data(), data, sz);
					WebRTCInternal->VideoTrack->send(pktbuf);
					sent = true;
				}
				else if (WebRTCInternal->DataChannel && WebRTCInternal->DataChannel->isOpen())
				{
					static thread_local rtc::binary dcbuf;
					dcbuf.resize(sz);
					memcpy(dcbuf.data(), data, sz);
					WebRTCInternal->DataChannel->sendBuffer(dcbuf);
					sent = true;
				}
			}
		}
	}

	vpx_codec_destroy(&encoder);
	vpx_img_free(&img);
	return sent;
#else
	return false;
#endif
}

void USynavisStreamer::SendFrameBytes(const TArray<uint8>& Bytes, const FString& Name, const FString& Format)
{
	if (!Connector)
		return;

	// DataConnector expects std::span<const uint8_t>. Convert TArray to std::span without copying.
	try
	{
		const uint8* DataPtr = Bytes.GetData();
		size_t Size = static_cast<size_t>(Bytes.Num());
		std::span<const uint8_t> span(DataPtr, Size);
		// Connector likely expects UE FString for metadata; attempt to forward as UTF-8 std::string
		std::string nameUtf8(TCHAR_TO_UTF8(*Name));
		std::string formatUtf8(TCHAR_TO_UTF8(*Format));
		Connector->SendBuffer(span, nameUtf8, formatUtf8);
	}
	catch (...) {
		UE_LOG(LogActor, Error, TEXT("Error sending frame bytes via DataConnector"));
	}
}

