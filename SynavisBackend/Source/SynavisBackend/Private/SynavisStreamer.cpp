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

THIRD_PARTY_INCLUDES_START
#include <rtc/rtc.hpp>
#include <rtc/websocket.hpp>
#include <rtc/rtppacketizer.hpp>
#include <rtc/rtppacketizationconfig.hpp>
#include <rtc/frameinfo.hpp>
#include <rtc/datachannel.hpp>
#include <rtc/configuration.hpp>
#if defined(LIBAV_AVAILABLE)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>
#include <libavutil/buffer.h>
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
#endif


THIRD_PARTY_INCLUDES_END

using rtc::binary;

static const TMap<FString, TArray<TTuple<FString,FString,uint32>>> DataConnectionHeaderMap
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

	// RenderTarget initialization
  this->RenderTarget = CreateDefaultSubobject<UTextureRenderTarget2D>(TEXT("SynavisStreamerRenderTarget"));

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

	if (!bStreaming)
		return;

	// Process any pending NV12 GPU readbacks (non-blocking)
	const double Now = FPlatformTime::Seconds();
	const double ReadbackTimeout = 0.5; // seconds
	for (int32 i = PendingReadbacks.Num() - 1; i >= 0; --i)
	{
		FPendingNV12Readback& rec = PendingReadbacks[i];
		if (!rec.ReadbackY || !rec.ReadbackUV)
		{
			PendingReadbacks.RemoveAtSwap(i);
			continue;
		}

		// If readbacks ready, encode and remove from queue
		if (rec.ReadbackY->IsReady() && rec.ReadbackUV->IsReady())
		{
			UE_LOG(LogTemp, Verbose, TEXT("Synavis: Pending readback ready, starting zero-copy encode (Width=%d Height=%d)"), rec.Width, rec.Height);
			EncodeNV12ReadbackAndSend(rec.ReadbackY, rec.ReadbackUV, rec.Width, rec.Height, rec.TargetTrack);
			// EncodeNV12ReadbackAndSend takes ownership of the readbacks (via AVBuffer free callback),
			// so do not delete them here.
			PendingReadbacks.RemoveAtSwap(i);
			continue;
		}

		// Timeout expired: clean up and fall back (unlock & delete on render thread)
		if (Now - rec.EnqueuedAt > ReadbackTimeout)
		{
			UE_LOG(LogTemp, Warning, TEXT("Synavis: Pending readback timed out after %.3fs, cleaning up"), Now - rec.EnqueuedAt);
			FRHIGPUTextureReadback* Yrb = rec.ReadbackY;
			FRHIGPUTextureReadback* UVrb = rec.ReadbackUV;
			ENQUEUE_RENDER_COMMAND(Synavis_CleanupReadback)([Yrb, UVrb](FRHICommandListImmediate& RHICmdList)
			{
				if (Yrb) { Yrb->Unlock(); delete Yrb; }
				if (UVrb) { UVrb->Unlock(); delete UVrb; }
			});
			PendingReadbacks.RemoveAtSwap(i);
		}
	}
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
	if (SystemDataChannel)
		SystemDataChannel->close();
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


ESynavisState USynavisStreamer::GetConnectionState() const
{
  // return connection state --> we need to do callback-based updates here
  return this->ConnectionState;
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
  // Signalling server
  //open/connect/autodiscover
  // Add handler for trickle ICE
  // Add handler for initial setup of connection
  // Synavis might attempt to setup first for simulation coupling.

	// Initialize libdatachannel components and create a datachannel
	if (!WebRTCInternal)
		WebRTCInternal = new FWebRTCInternal();
	static bool bInitDone = false;
	if (!bInitDone) { rtc::InitLogger(rtc::LogLevel::Info); bInitDone = true; }
	WebRTCInternal->PeerConnection = std::make_shared<rtc::PeerConnection>();

	// create datachannel
	WebRTCInternal->SystemDataChannel = WebRTCInternal->PeerConnection->createDataChannel("synavis-stream");
	WebRTCInternal->SystemDataChannel->onOpen([]() { 
    UE_LOG(LogTemp, Warning, TEXT("Opening Data Channel"))
   });
	WebRTCInternal->SystemDataChannel->onClosed([]() { /* no-op */ });
	WebRTCInternal->SystemDataChannel->onError([](std::string err) { /* no-op */ });
  WebRTCInternal->SystemDataChannel->onMessage(
	  [this](const rtc::message_variant& msg)
	  {
	    if (std::holds_alternative<rtc::binary>(msg))
	    {
	      TArray<uint8> container;
        //try to std::move into container

	    }
      else if (std::holds_alternative<std::string>(msg))
			{
        const std::string& str = std::get<std::string>(msg);
        FString fmsg = FString(UTF8_TO_TCHAR(str.c_str()));
        // MsgBroadcast.Broadcast(fmsg, nullptr);
			}
	  }
	);
  {
		TArray<FSynavisHandlers> HandlersCopy = RegisteredDataHandlers.Array();
		for (FSynavisHandlers HandlerCopy : HandlersCopy)
		{
			if (!HandlerCopy.Video.IsSet())
				continue; // handler did not register a video source
			TPair<std::shared_ptr<rtc::Track>, USceneCaptureComponent2D*> Pair = HandlerCopy.Video.GetValue();
			// If a track already exists, skip
			if (Pair.Key)
				continue;
			USceneCaptureComponent2D* SceneCap = Pair.Value;
			if (!SceneCap)
				continue;
			UTextureRenderTarget2D* VideoSource = SceneCap->TextureTarget;
			if (!VideoSource)
			{
				UE_LOG(LogTemp, Warning, TEXT("Synavis: Handler %d has no valid TextureTarget - skipping track creation"), HandlerCopy.HandlerID);
				continue;
			}

			// Build media description and add a track for this handler
			rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
			media.addVP9Codec(96);
			media.setBitrate(90000);
			auto Track = WebRTCInternal->PeerConnection->addTrack(media);
			if (!Track)
			{
				UE_LOG(LogTemp, Warning, TEXT("Synavis: Failed to create video track for handler %d"), HandlerCopy.HandlerID);
				continue;
			}

			// Attach callbacks and packetizer
			FString LogPrefix = LogSetup(HandlerCopy.HandlerID, SceneCap);
			Track->onOpen([LogPrefix]() { UE_LOG(LogTemp, Warning, TEXT("%s: Video track opened"), *LogPrefix); });
			// Create RTP packetization config and attach a packetizer specific to this track
			rtc::SSRC ssrc = static_cast<rtc::SSRC>(std::rand());
			auto rtpCfg = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, std::string("synavis"), 96, rtc::RtpPacketizer::VideoClockRate);
			auto packetizer = std::make_shared<rtc::RtpPacketizer>(rtpCfg);
			Track->setMediaHandler(packetizer);

			// Update the handler stored in RegisteredDataHandlers: remove and re-add with track filled in
			RegisteredDataHandlers.Remove(HandlerCopy);
			HandlerCopy.Video.Emplace(Track, SceneCap);
			RegisteredDataHandlers.Add(HandlerCopy);
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

	// Create signalling websocket and hook handlers (mirror DataConnector behavior)
	if (!WebRTCInternal->Signalling)
		WebRTCInternal->Signalling = std::make_shared<rtc::WebSocket>();

	// onOpen: communicate local SDP(s)
	WebRTCInternal->Signalling->onOpen([this]() {
		UE_LOG(LogTemp, Log, TEXT("Synavis: Signalling websocket opened"));
    this->ConnectionState = ESynavisState::SignallingUp;
		CommunicateSDPs();
	});

	// onMessage: receive signalling JSON messages (offer/answer/iceCandidate and simple control messages)
	WebRTCInternal->Signalling->onMessage([this](auto messageOrData) {
		try {
			if (std::holds_alternative<std::string>(messageOrData))
			{
				const std::string& msg = std::get<std::string>(messageOrData);
				FJsonObject Parsed;
				if (!TryParseJSON(msg, Parsed))
				{
					UE_LOG(LogTemp, Warning, TEXT("Synavis: Received non-JSON signalling message"));
					return;
				}

				// extract type field
				FString Type;
				if (Parsed.HasField(TEXT("type")))
					Type = Parsed.GetStringField(TEXT("type"));

				if (Type.Equals(TEXT("answer"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("offer"), ESearchCase::IgnoreCase))
				{
					// set remote description
					if (Parsed.HasField(TEXT("sdp")))
					{
						std::string sdp = TCHAR_TO_UTF8(*Parsed.GetStringField(TEXT("sdp")));
						std::string typestr = TCHAR_TO_UTF8(*Type);
						rtc::Description remote(sdp, typestr);
						if (WebRTCInternal && WebRTCInternal->PeerConnection)
						{
							WebRTCInternal->PeerConnection->setRemoteDescription(remote);
							UE_LOG(LogTemp, Log, TEXT("Synavis: Set remote description (type=%s)"), *Type);
						}
					}
				}
				else if (Type.Equals(TEXT("iceCandidate"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("candidate"), ESearchCase::IgnoreCase))
				{
					RegisterRemoteCandidate(Parsed);
				}
				else
				{
					// forward other signalling messages to log
					UE_LOG(LogTemp, Verbose, TEXT("Synavis: Signalling message type=%s"), *Type);
				}
			}
			else
			{
				// binary signalling messages are unexpected; log and ignore
				UE_LOG(LogTemp, Warning, TEXT("Synavis: Received binary signalling message (ignored)"));
			}
		}
		catch (const std::exception& e)
		{
			UE_LOG(LogTemp, Error, TEXT("Synavis: Exception in signalling onMessage: %s"), ANSI_TO_TCHAR(e.what()));
		}
	});
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
	if (!WebRTCInternal || !WebRTCInternal->Signalling || !WebRTCInternal->PeerConnection)
		return;

	if (!WebRTCInternal->Signalling->isOpen())
		return;

	auto descriptionOpt = WebRTCInternal->PeerConnection->localDescription();
	if (!descriptionOpt)
		return;

	// Build a JSON object with type and sdp and send via signalling websocket
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	std::string typestr = descriptionOpt->typeString();
	std::string sdpStr = static_cast<std::string>(*descriptionOpt);
	Obj->SetStringField(TEXT("type"), FString(UTF8_TO_TCHAR(typestr.c_str())));
	Obj->SetStringField(TEXT("sdp"), FString(UTF8_TO_TCHAR(sdpStr.c_str())));

	FString Out; 
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	if (FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer))
	{
		std::string outcpp = TCHAR_TO_UTF8(*Out);
		try {
			WebRTCInternal->Signalling->send(outcpp);
			UE_LOG(LogTemp, Log, TEXT("Synavis: Sent local SDP via signalling"));
		}
		catch (...) { UE_LOG(LogTemp, Warning, TEXT("Synavis: Failed to send SDP over signalling")); }
	}
}

void USynavisStreamer::RegisterRemoteCandidate(const FJsonObject& Content)
{
	if (!WebRTCInternal || !WebRTCInternal->PeerConnection)
		return;

	FString candStr; FString sdpMid; int32 sdpMLineIndex = -1;

	// candidate may be an object or a string
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
				sdpMLineIndex = Inner->GetNumberField(TEXT("sdpMLineIndex"));
		}
		else if (CandidateVal.IsValid() && CandidateVal->Type == EJson::String)
		{
			candStr = CandidateVal->AsString();
		}
	}

	if (Content.HasField(TEXT("sdpMid")) && sdpMid.IsEmpty())
		sdpMid = Content.GetStringField(TEXT("sdpMid"));
	if (Content.HasField(TEXT("sdpMLineIndex")) && sdpMLineIndex == -1)
		sdpMLineIndex = Content.GetNumberField(TEXT("sdpMLineIndex"));

	if (candStr.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Synavis: Received iceCandidate message with no candidate field"));
		return;
	}

	// Convert to std::string and add to peer connection
	std::string scand = TCHAR_TO_UTF8(*candStr);
	std::string smid = TCHAR_TO_UTF8(*sdpMid);
	try {
		rtc::Candidate ice(scand, smid);
		WebRTCInternal->PeerConnection->addRemoteCandidate(ice);
		UE_LOG(LogTemp, Log, TEXT("Synavis: Registered remote candidate"));
	}
	catch (const std::exception& e)
	{
		UE_LOG(LogTemp, Error, TEXT("Synavis: Exception registering candidate: %s"), ANSI_TO_TCHAR(e.what()));
	}
}

static uint32 CreatePawnHandle()
{
	static uint32 NextPawnHandle = 1;
	return NextPawnHandle++;
}

FORCEINLINE FString LogSetup(auto ID, USceneComponent* Child)
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
      // If no PeerConnection yet, treat as offline: record video source but no rtc::Track (will be created later)
      if (this->ConnectionState == ESynavisState::Offline)
      {
        UE_LOG(LogTemp, Verbose, TEXT("%s: Pre-Streaming initialization of track - registering video source without track"), *LogPrefix);
        Handler.Video.Emplace(nullptr, SceneCapture);
      }
      else if(this->SourcePolicy == ESynavisSourcePolicy::RemainStatic)
      {
        UE_LOG(LogTemp, Verbose, TEXT("%s: Source policy is remain static - will refuse new source."), *LogPrefix);
      }
      else
      {
        // Online: create a send-only VP9 track now (ignore failure-policy distinctions for now)
        rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
        media.addVP9Codec(96);
        media.setBitrate(90000);

        auto Track = WebRTCInternal->PeerConnection->addTrack(media);
        if (!Track)
        {
          UE_LOG(LogTemp, Warning, TEXT("%s: Failed to create video track for handler - registering without track"), *LogPrefix);
          Handler.Video.Emplace(nullptr, SceneCapture);
        }
        else
        {
          // attach basic callbacks and packetizer (same behavior as StartSignalling)
          Handler.Video.Emplace(Track, SceneCapture);
          Track->onOpen([LogPrefix]() { UE_LOG(LogTemp, Warning, TEXT("%s: Video track opened"), *LogPrefix); });

          rtc::SSRC ssrc = static_cast<rtc::SSRC>(std::rand());
          auto rtpCfg = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, std::string("synavis"), 96, rtc::RtpPacketizer::VideoClockRate);
          auto packetizer = std::make_shared<rtc::RtpPacketizer>(rtpCfg);
          Track->setMediaHandler(packetizer);

          UE_LOG(LogTemp, Log, TEXT("%s: Created video track for handler (SSRC=%u)"), *LogPrefix, (uint32)ssrc);
        }
      }
    }
  }

  if (DedicatedChannel)
  {
    // create dedicated datachannel for this handler
    std::string channelName = "synavis-datahandler-" + std::to_string(Handler.HandlerID);
			Handler.DataChannel = WebRTCInternal->PeerConnection->createDataChannel(channelName);
			// Capture the log prefix by value for use inside the callback
			FString DedicatedLog = LogPrefix;
			if (Handler.DataChannel.has_value() && Handler.DataChannel.value())
			{
				auto DedicatedDC = Handler.DataChannel.value();
				DedicatedDC->onOpen([DedicatedLog]() {
					UE_LOG(LogTemp, Warning, TEXT("%s: Opening dedicated data channel for handler"), *DedicatedLog);
				});
				DedicatedDC->onClosed([]() {  });
				DedicatedDC->onError([](std::string err) {  });
				DedicatedDC->onMessage(
					[this, Handler](const rtc::message_variant& msg)
					{
						if (std::holds_alternative<rtc::binary>(msg))
						{
							const rtc::binary& data = std::get<rtc::binary>(msg);
							TArray<uint8> container;
							//try to std::move into container

						}
						else if (std::holds_alternative<std::string>(msg))
						{
							const std::string& str = std::get<std::string>(msg);
							FString fmsg = FString(UTF8_TO_TCHAR(str.c_str()));
							// Handler.MsgHandler.Broadcast(fmsg);
						}
					}
				);
			}
  }
  else
  {
    Handler.DataChannel = WebRTCInternal->SystemDataChannel;
  }

  RegisteredDataHandlers.Add(Handler);
  return Handler.HandlerID;
}

void USynavisStreamer::CaptureFrame()
{
	// Capture frames for registered handlers using only the zero-copy NV12 path.
	// The registration step is expected to have created a valid video track for each handler.
	if (!bStreaming)
		return;

	for (const FSynavisHandlers& Handler : RegisteredDataHandlers)
	{
			if (!Handler.Video.IsSet())
			continue;
			TPair<std::shared_ptr<rtc::Track>, USceneCaptureComponent2D*> videoPair = Handler.Video.GetValue();
			USceneCaptureComponent2D* SceneCapture = videoPair.Value;
			std::shared_ptr<rtc::Track> targetTrack = videoPair.Key;
			if (!SceneCapture || !targetTrack)
		{
			UE_LOG(LogTemp, Warning, TEXT("Synavis: Handler missing required RenderTarget or Track - skipping"));
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
			FPendingNV12Readback rec;
			rec.ReadbackY = ReadbackY;
			rec.ReadbackUV = ReadbackUV;
			rec.EnqueuedAt = FPlatformTime::Seconds();
			rec.TargetTrack = targetTrack;
			rec.Width = Width;
			rec.Height = Height;
			PendingReadbacks.Add(rec);
			UE_LOG(LogTemp, Verbose, TEXT("Synavis: Enqueued NV12 readback for handler (W=%d H=%d)"), Width, Height);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Synavis: Failed to enqueue NV12 readback for handler"));
		}
	}
}

void USynavisStreamer::SendFrameBytes(const TArray<uint8>& Bytes, const FString& Name, const FString& Format, std::shared_ptr<rtc::Track> TargetTrack)
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
		if (TargetTrack && TargetTrack->isOpen())
		{
			TargetTrack->send(buf);
			return;
		}
		// No global VideoTrack fallback; prefer handler TargetTrack, otherwise system data channel
		else if (WebRTCInternal->SystemDataChannel && WebRTCInternal->SystemDataChannel->isOpen())
		{
			WebRTCInternal->SystemDataChannel->sendBuffer(buf);
			return;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("No open DataChannel to send frame bytes (Name=%s, Format=%s)"), *Name, *Format);
}

void USynavisStreamer::OnDataChannelMessage(const rtc::message_variant& message)
{
	if (std::holds_alternative<rtc::binary>(message))
	{
		const rtc::binary& data = std::get<rtc::binary>(message);
		// preprocessing of the data type using the control bytes

	}
	else if (std::holds_alternative<std::string>(message))
	{
		const std::string& msgStr = std::get<std::string>(message);
		FString Msg = FString(msgStr.c_str());
		
  }
}

// (CPU-based I420/NV12 helpers removed; use EncodeNV12ReadbackAndSend for GPU zero-copy)

// Zero-copy: accept two FRHIGPUTextureReadback objects, wrap their locked pointers into
// AVBufferRefs so FFmpeg manages lifetime and calls our free-callback to unlock/delete readbacks.
void USynavisStreamer::EncodeNV12ReadbackAndSend(FRHIGPUTextureReadback* ReadbackY, FRHIGPUTextureReadback* ReadbackUV, int Width, int Height, std::shared_ptr<rtc::Track> TargetTrack)
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
		if (WebRTCInternal)
		{
			size_t sz = static_cast<size_t>(LibAVState->Packet->size);
			const uint8_t* data = LibAVState->Packet->data;
			rtc::binary pktbuf(sz);
			if (sz) memcpy(pktbuf.data(), data, sz);

			if (TargetTrack && TargetTrack->isOpen())
			{
				UE_LOG(LogTemp, Verbose, TEXT("Synavis: Sending encoded packet size=%d to handler track"), (int)sz);
				uint32_t ts = 0;
				if (LibAVState->Packet && LibAVState->Packet->pts != AV_NOPTS_VALUE)
					ts = static_cast<uint32_t>(LibAVState->Packet->pts);
				rtc::FrameInfo fi(ts);
				fi.payloadType = 96;
				TargetTrack->sendFrame(std::move(pktbuf), fi);
			}
			// No global VideoTrack fallback: prefer handler TargetTrack; otherwise fall back to system DataChannel
			else if (WebRTCInternal->SystemDataChannel && WebRTCInternal->SystemDataChannel->isOpen())
			{
				static thread_local rtc::binary dcbuf;
				dcbuf.resize(sz);
				if (sz) memcpy(dcbuf.data(), data, sz);
				WebRTCInternal->SystemDataChannel->sendBuffer(dcbuf);
			}
		}
		av_packet_unref(LibAVState->Packet);
	}

	av_frame_free(&frame);
}

