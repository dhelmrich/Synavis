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
#include <nlohmann/json.hpp>
#include "Async/Async.h"
#include "RenderCore/Public/RHIGPUReadback.h"

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

	// poll for completion on the game thread
	if (bReadbackPending && TextureReadback.IsValid())
	{
		if (TextureReadback->IsReady())
		{
			FRHIGPUTextureReadback::FReadbackParameters Params;
			FRHIGPUTextureReadback::FReadbackData ReadbackData;
			if (TextureReadback->Map(ReadbackData))
			{
				const uint8_t* PixelData = reinterpret_cast<const uint8_t*>(ReadbackData.Data);
				int RowPitch = ReadbackData.RowPitch;

				// Copy rows into a contiguous buffer (RGBA8)
				std::vector<uint8_t> RawBuf;
				RawBuf.resize(Width * Height * 4);
				for (int y = 0; y < Height; ++y)
				{
					const uint8_t* Row = PixelData + size_t(y) * RowPitch;
					memcpy(RawBuf.data() + size_t(y) * Width * 4, Row, Width * 4);
				}

				TextureReadback->Unmap();
				bReadbackPending = false;

				// Offload encoding to a background thread to avoid blocking the game thread
				Async(EAsyncExecution::ThreadPool, [this, Raw = MoveTemp(RawBuf), Width, Height]() mutable {
					// Convert and encode
#if defined(LIBVPX_AVAILABLE)
					// Convert RGBA -> I420
					vpx_image_t img;
					if (!vpx_img_alloc(&img, VPX_IMG_FMT_I420, Width, Height, 1))
					{
						// conversion failed
					}
					// naive conversion; better use libyuv for performance
					const uint8_t* Src = Raw.data();
					for (int y = 0; y < Height; ++y)
					{
						for (int x = 0; x < Width; ++x)
						{
							int si = (y * Width + x) * 4;
							uint8_t r = Src[si + 0];
							uint8_t g = Src[si + 1];
							uint8_t b = Src[si + 2];
							// RGB -> YUV conversion (BT.601) integer approximation
							int Y = ( 66 * r + 129 * g +  25 * b + 128) >> 8; Y += 16;
							int U = (-38 * r -  74 * g + 112 * b + 128) >> 8; U += 128;
							int V = (112 * r -  94 * g -  18 * b + 128) >> 8; V += 128;
							// clamp
							Y = FMath::Clamp(Y, 0, 255);
							U = FMath::Clamp(U, 0, 255);
							V = FMath::Clamp(V, 0, 255);
							// fill planes
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

					// Prepare encoder
					vpx_codec_enc_cfg_t cfg;
					vpx_codec_enc_config_default(vpx_codec_vp9_cx(), &cfg, 0);
					cfg.g_w = Width;
					cfg.g_h = Height;
					cfg.g_timebase.num = 1;
					cfg.g_timebase.den = 30;

					vpx_codec_ctx_t encoder;
					if (vpx_codec_enc_init(&encoder, vpx_codec_vp9_cx(), &cfg, 0))
					{
						// encoder init failed
					}

					vpx_codec_encode(&encoder, &img, 0, 1, 0, VPX_DL_REALTIME);
					vpx_codec_iter_t iter = nullptr;
					const vpx_codec_cx_pkt_t* pkt = nullptr;
					// Send encoded packets immediately to avoid allocating a large output buffer
					while ((pkt = vpx_codec_get_cx_data(&encoder, &iter)) != nullptr)
					{
						if (pkt->kind == VPX_CODEC_CX_FRAME_PKT)
						{
							const uint8_t* data = reinterpret_cast<const uint8_t*>(pkt->data.frame.buf);
							size_t sz = static_cast<size_t>(pkt->data.frame.sz);
							if (WebRTCInternal)
							{
								// Prefer sending on an outgoing media track if available
								if (WebRTCInternal->VideoTrack && WebRTCInternal->VideoTrack->isOpen())
								{
									rtc::binary pktbuf(sz);
									memcpy(pktbuf.data(), data, sz);
									WebRTCInternal->VideoTrack->send(pktbuf);
								}
								else if (WebRTCInternal->DataChannel && WebRTCInternal->DataChannel->isOpen())
								{
									// reuse a thread-local buffer to avoid frequent allocations
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
					// libvpx not available: fallback to previous RGB packaging
					if (WebRTCInternal && WebRTCInternal->DataChannel && WebRTCInternal->DataChannel->isOpen())
					{
						// reuse thread-local buffer to avoid allocations
						static thread_local rtc::binary dcbuf;
						dcbuf.resize(Raw.size());
						memcpy(dcbuf.data(), Raw.data(), Raw.size());
						WebRTCInternal->DataChannel->sendBuffer(dcbuf);
					}
#endif
#endif
				});
			}
		}
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
		WebRTCInternal = std::make_unique<FWebRTCInternal>();
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
				nlohmann::json message = { {"type", description->typeString()}, {"sdp", std::string(description.value())} };
				FString Offer = FString(message.dump().c_str());
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
	std::string Name = "frame";
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
	// allocate contiguous buffer
	std::vector<uint8_t> RawBuffer;
	RawBuffer.resize(TotalSize);
	for (int i = 0; i < Width * Height; ++i)
	{
		const FColor& C = Bitmap[i];
		RawBuffer[i * 4 + 0] = C.R;
		RawBuffer[i * 4 + 1] = C.G;
		RawBuffer[i * 4 + 2] = C.B;
		RawBuffer[i * 4 + 3] = C.A;
	}

	SendFrameBytes(RawBuffer.data(), RawBuffer.size(), Name, "raw_rgba");
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

void USynavisStreamer::SendFrameBytes(const uint8_t* Bytes, size_t Size, const std::string& Name, const std::string& Format)
{
	if (!Connector)
		return;

	// DataConnector expects std::span<const uint8_t>
	try
	{
		std::span<const uint8_t> span(Bytes, Size);
		Connector->SendBuffer(span, Name, Format);
	}
	catch (...) {
		UE_LOG(LogActor, Error, TEXT("Error sending frame bytes via DataConnector"));
	}
}

