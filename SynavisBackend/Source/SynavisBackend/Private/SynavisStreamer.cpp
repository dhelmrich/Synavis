// Fill out your copyright notice in the Description page of Project Settings.


#include "SynavisStreamer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "RendererInterface.h"
#include "RenderUtils.h"
#include "Engine/Texture2D.h"
#include "Misc/ScopeLock.h"
#include <memory>
#include <string>
#include <span>
#include "Async/Async.h"
#include "SynavisStreamerRendering.h"

THIRD_PARTY_INCLUDES_START
#include <rtc/rtc.hpp>
#include <rtc/websocket.hpp>
#include <rtc/rtppacketizer.hpp>
#include <rtc/rtppacketizationconfig.hpp>
#include <rtc/frameinfo.hpp>
#if defined(LIBAV_AVAILABLE)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>
}
#endif
THIRD_PARTY_INCLUDES_END

using rtc::binary;

// Sets default values for this component's properties
USynavisStreamer::USynavisStreamer()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}

USynavisStreamer::~USynavisStreamer()
{
	if (WebRTCInternal)
	{
		delete WebRTCInternal;
		WebRTCInternal = nullptr;
	}
	if (LibAVState)
	{
		if (LibAVState->Packet) { av_packet_free(&LibAVState->Packet); LibAVState->Packet = nullptr; }
		if (LibAVState->Frame) { av_frame_free(&LibAVState->Frame); LibAVState->Frame = nullptr; }
		if (LibAVState->CodecCtx) { avcodec_free_context(&LibAVState->CodecCtx); LibAVState->CodecCtx = nullptr; }
		delete LibAVState; LibAVState = nullptr;
	}
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

	// CPU-only path: we do not attempt GPU conversion here.

	// If GPU path not used or not ready fall back to the earlier CPU readback path
	// Use FRHIGPUTextureReadback (UE5.1+) for async readback
	static TUniquePtr<FRHIGPUTextureReadback> TextureReadback;
	static bool bReadbackPending = false;

	if (!TextureReadback)
	{
		// FRHIGPUTextureReadback has a constructor taking FName; create via MakeUnique
		TextureReadback = MakeUnique<FRHIGPUTextureReadback>(TEXT("SynavisReadback"));
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
					// Use EnqueueCopy to copy texture into the readback
					Readback->EnqueueCopy(RHICmdList, RTTexture);
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

// (Struct declarations live in the header to satisfy UHT; destructor definitions follow)

USynavisStreamer::FWebRTCInternal::~FWebRTCInternal()
{
	if (DataChannel)
		DataChannel->close();
	if (PeerConnection)
		PeerConnection->close();
	if (Signalling)
		Signalling->close();
}

USynavisStreamer::FLibAVEncoderState::~FLibAVEncoderState()
{
	if (Packet) { av_packet_free(&Packet); Packet = nullptr; }
	if (Frame) { av_frame_free(&Frame); Frame = nullptr; }
	if (CodecCtx) { avcodec_free_context(&CodecCtx); CodecCtx = nullptr; }
}

void USynavisStreamer::StartStreaming()
{
	if (bStreaming)
		return;
	bStreaming = true;

	// Initialize persistent libav encoder state lazily
	if (!LibAVState)
	{
		LibAVState = new FLibAVEncoderState();
		LibAVState->Codec = avcodec_find_encoder(AV_CODEC_ID_VP9);
		// actual codec context will be created when first frame with size arrives
	}
}

void USynavisStreamer::StopStreaming()
{
	if (!bStreaming)
		return;
	bStreaming = false;

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

void USynavisStreamer::StartSignalling()
{
	// Initialize libdatachannel components and create a datachannel
	if (!WebRTCInternal)
		WebRTCInternal = new FWebRTCInternal();
	static bool bInitDone = false;
	if (!bInitDone) { rtc::InitLogger(rtc::LogLevel::Info); bInitDone = true; }
	WebRTCInternal->PeerConnection = std::make_shared<rtc::PeerConnection>();

	// create datachannel
	WebRTCInternal->DataChannel = WebRTCInternal->PeerConnection->createDataChannel("synavis-stream");
	WebRTCInternal->DataChannel->onOpen([]() { /* no-op */ });
	WebRTCInternal->DataChannel->onClosed([]() { /* no-op */ });
	WebRTCInternal->DataChannel->onError([](std::string err) { /* no-op */ });

	// Create a send-only VP9 track if possible (mirrors libdatachannel examples)
	// Try to create a send-only VP9 track; ignore failures without using exceptions
	{
		rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
		media.addVP9Codec(96);
		media.setBitrate(90000);
		WebRTCInternal->VideoTrack = WebRTCInternal->PeerConnection->addTrack(media);
		if (WebRTCInternal->VideoTrack)
		{
			WebRTCInternal->VideoTrack->onOpen([]() { /* no-op */ });

			// Attach an RTP packetizer/media handler to the track so libdatachannel handles RTP packetization
      using namespace rtc;
      // Create RTP packetization config: choose random SSRC and default payload type 96 (VP9)
      SSRC ssrc = static_cast<SSRC>(std::rand());
      auto rtpCfg = std::make_shared<RtpPacketizationConfig>(ssrc, std::string("synavis"), 96, RtpPacketizer::VideoClockRate);
      auto packetizer = std::make_shared<RtpPacketizer>(rtpCfg);
      // Attach packetizer to the track (media handler chain)
      WebRTCInternal->VideoTrack->setMediaHandler(packetizer);
      // Keep a reference so we can reuse it for frame metadata if needed
      WebRTCInternal->Packetizer = packetizer;
		}
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
	if (!RenderTarget)
		return;

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
		return;

	int Width = RenderTarget->SizeX;
	int Height = RenderTarget->SizeY;
	// First try GPU path: convert render target to I420 via compute shader (NV12 on GPU, deinterleaved to I420 on CPU)
	TArray<uint8> Ygpu, Ugpu, Vgpu;
	if (ConvertRenderTargetToI420_GPU(RenderTarget, Ygpu, Ugpu, Vgpu))
	{
		EncodeI420AndSend(Ygpu, Ugpu, Vgpu, Width, Height);
		return;
	}

	// GPU path not available or failed: fall back to CPU readback + conversion
	TArray<FColor> Bitmap;
	Bitmap.AddUninitialized(Width * Height);

	// read pixels from render target (game thread safe call)
	bool bRead = RTResource->ReadPixels(Bitmap);
	if (!bRead)
	{
		return;
	}

	// Convert on CPU using existing helper and then encode
	TArray<uint8> RGBA;
	RGBA.SetNumUninitialized(Width * Height * 4);
	for (int i = 0; i < Width * Height; ++i)
	{
		const FColor& C = Bitmap[i];
		RGBA[i * 4 + 0] = C.R;
		RGBA[i * 4 + 1] = C.G;
		RGBA[i * 4 + 2] = C.B;
		RGBA[i * 4 + 3] = C.A;
	}

	TArray<uint8> Ycpu, Ucpu, Vcpu;
	if (ConvertRGBA8ToI420_CPU(RGBA.GetData(), Width, Height, Ycpu, Ucpu, Vcpu))
	{
		EncodeI420AndSend(Ycpu, Ucpu, Vcpu, Width, Height);
		return;
	}

	// Last-resort: send raw RGBA
	FString Name = TEXT("frame");
	SendFrameBytes(RGBA, Name, TEXT("raw_rgba"));
}

// EncodeFrameToVP9 removed; replaced by EncodeI420AndSend which takes I420 planes (GPU or CPU-produced)

void USynavisStreamer::SendFrameBytes(const TArray<uint8>& Bytes, const FString& Name, const FString& Format)
{
	// Send outbound bytes via libdatachannel (DataChannel) when available.
	if (WebRTCInternal)
	{
		size_t sz = static_cast<size_t>(Bytes.Num());
		rtc::binary buf(sz);
		if (sz)
			memcpy(buf.data(), Bytes.GetData(), sz);

		// Prefer sending via the send-only VideoTrack when available (video packets);
		// fall back to the reliable DataChannel for arbitrary bytes.
		if (WebRTCInternal->VideoTrack && WebRTCInternal->VideoTrack->isOpen())
		{
			WebRTCInternal->VideoTrack->send(buf);
			return;
		}
		else if (WebRTCInternal->DataChannel && WebRTCInternal->DataChannel->isOpen())
		{
			WebRTCInternal->DataChannel->sendBuffer(buf);
			return;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("No open DataChannel to send frame bytes (Name=%s, Format=%s)"), *Name, *Format);
}

// Helper: encode planar I420 buffers using libav (ffmpeg) and send via WebRTC DataChannel/VideoTrack
void USynavisStreamer::EncodeI420AndSend(const TArray<uint8>& Y, const TArray<uint8>& U, const TArray<uint8>& V, int Width, int Height)
{
	if (Y.Num() == 0 || U.Num() == 0 || V.Num() == 0)
		return;

	if (!LibAVState || !LibAVState->Codec)
		return;

	// Initialize or resize codec context/frame if needed
	{
		FScopeLock guard(&LibAVState->Mutex);
		if (!LibAVState->CodecCtx || LibAVState->Width != Width || LibAVState->Height != Height)
		{
			// cleanup existing
			if (LibAVState->Packet) { av_packet_free(&LibAVState->Packet); LibAVState->Packet = nullptr; }
			if (LibAVState->Frame) { av_frame_free(&LibAVState->Frame); LibAVState->Frame = nullptr; }
			if (LibAVState->CodecCtx) { avcodec_free_context(&LibAVState->CodecCtx); LibAVState->CodecCtx = nullptr; }

			LibAVState->CodecCtx = avcodec_alloc_context3(LibAVState->Codec);
			if (!LibAVState->CodecCtx)
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to allocate AVCodecContext"));
				return;
			}
			LibAVState->CodecCtx->width = Width;
			LibAVState->CodecCtx->height = Height;
			LibAVState->CodecCtx->time_base = AVRational{1,30};
			LibAVState->CodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
			LibAVState->CodecCtx->bit_rate = 200000;
			av_opt_set_int(LibAVState->CodecCtx->priv_data, "deadline", 1, 0);

			int ret = avcodec_open2(LibAVState->CodecCtx, LibAVState->Codec, NULL);
			if (ret < 0)
			{
				char errbuf[128]; av_strerror(ret, errbuf, sizeof(errbuf));
				UE_LOG(LogTemp, Error, TEXT("avcodec_open2 failed: %s"), ANSI_TO_TCHAR(errbuf));
				avcodec_free_context(&LibAVState->CodecCtx);
				return;
			}

			LibAVState->Frame = av_frame_alloc();
			if (!LibAVState->Frame)
			{
				UE_LOG(LogTemp, Error, TEXT("av_frame_alloc failed"));
				avcodec_free_context(&LibAVState->CodecCtx);
				return;
			}
			LibAVState->Frame->format = LibAVState->CodecCtx->pix_fmt;
			LibAVState->Frame->width = LibAVState->CodecCtx->width;
			LibAVState->Frame->height = LibAVState->CodecCtx->height;
			if ((ret = av_frame_get_buffer(LibAVState->Frame, 32)) < 0)
			{
				char errbuf[128]; av_strerror(ret, errbuf, sizeof(errbuf));
				UE_LOG(LogTemp, Error, TEXT("av_frame_get_buffer failed: %s"), ANSI_TO_TCHAR(errbuf));
				av_frame_free(&LibAVState->Frame);
				avcodec_free_context(&LibAVState->CodecCtx);
				return;
			}

			LibAVState->Packet = av_packet_alloc();
			LibAVState->Width = Width;
			LibAVState->Height = Height;
		}
	}

	// Encode using persistent state
	{
		FScopeLock guard(&LibAVState->Mutex);
		// Copy Y plane
		for (int y = 0; y < Height; ++y)
		{
			memcpy(LibAVState->Frame->data[0] + y * LibAVState->Frame->linesize[0], Y.GetData() + y * Width, Width);
		}
		int ChW = (Width + 1) / 2;
		int ChH = (Height + 1) / 2;
		for (int y = 0; y < ChH; ++y)
		{
			memcpy(LibAVState->Frame->data[1] + y * LibAVState->Frame->linesize[1], U.GetData() + y * ChW, ChW);
			memcpy(LibAVState->Frame->data[2] + y * LibAVState->Frame->linesize[2], V.GetData() + y * ChW, ChW);
		}

		int ret = avcodec_send_frame(LibAVState->CodecCtx, LibAVState->Frame);
		if (ret < 0)
		{
			char errbuf[128]; av_strerror(ret, errbuf, sizeof(errbuf));
			UE_LOG(LogTemp, Error, TEXT("avcodec_send_frame failed: %s"), ANSI_TO_TCHAR(errbuf));
			return;
		}

		while ((ret = avcodec_receive_packet(LibAVState->CodecCtx, LibAVState->Packet)) >= 0)
		{
			if (WebRTCInternal)
			{
				size_t sz = static_cast<size_t>(LibAVState->Packet->size);
				const uint8_t* data = LibAVState->Packet->data;
				if (WebRTCInternal->VideoTrack && WebRTCInternal->VideoTrack->isOpen())
				{
					// Prefer sendFrame so the attached RtpPacketizer/media handler will packetize RTP
					rtc::binary pktbuf(sz);
					if (sz) memcpy(pktbuf.data(), data, sz);
					// Build FrameInfo: prefer packet PTS if available
					uint32_t ts = 0;
					if (LibAVState->Packet && LibAVState->Packet->pts != AV_NOPTS_VALUE)
						ts = static_cast<uint32_t>(LibAVState->Packet->pts);
					rtc::FrameInfo fi(ts);
					fi.payloadType = 96; // VP9 payload type as configured earlier
					WebRTCInternal->VideoTrack->sendFrame(std::move(pktbuf), fi);
				}
				else if (WebRTCInternal->DataChannel && WebRTCInternal->DataChannel->isOpen())
				{
					static thread_local rtc::binary dcbuf;
					dcbuf.resize(sz);
					if (sz) memcpy(dcbuf.data(), data, sz);
					WebRTCInternal->DataChannel->sendBuffer(dcbuf);
				}
			}
			av_packet_unref(LibAVState->Packet);
		}
	}
}

