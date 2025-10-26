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

void USynavisStreamer::TeardownConnection(int32 PlayerID)
{
	FSynavisConnection* Conn = FindConnectionByPlayerID(PlayerID);
	if (!Conn)
	{
		UE_LOG(LogTemp, Warning, TEXT("Synavis: Teardown requested for unknown connection %d"), PlayerID);
		return;
	}

	// Close peerconnection
	try {
		if (Conn->PeerConnection)
		{
			Conn->PeerConnection->close();
		}
	}
	catch (...) {}

	// Close control/data channels
	try {
		if (Conn->DataChannel) Conn->DataChannel->close();
	} catch (...) {}
	for (auto &kv : Conn->DataChannelsByHandler)
	{
		try { if (kv.second) kv.second->close(); } catch (...) {}
	}

	Conn->DataChannelsByHandler.clear();

	// Finally remove from map
	Connections.Remove(PlayerID);
	UE_LOG(LogTemp, Log, TEXT("Synavis: Teardown complete for connection %d"), PlayerID);
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

// Forward declarations for helper functions defined later in this file but used earlier.
static uint32 CreatePawnHandle();
static uint32 CreateConnectionHandle();
static FString LogSetup(uint32 ID, USceneComponent* Child);

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
	if (Signalling)
	{
		try { Signalling->close(); } catch (...) {}
		Signalling = nullptr;
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
	
}


void USynavisStreamer::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// If no connection currently requests streaming, early-out. Streaming is
	// managed per-connection via FSynavisConnection::bStreaming.
	bool anyStreaming = false;
	for (const auto& Pair : Connections) { if (Pair.Value.bStreaming) { anyStreaming = true; break; } }
	if (!anyStreaming)
		return;

	// Process any pending NV12 GPU readbacks (non-blocking).
	// Iterate backwards so we can RemoveAtSwap safely while iterating.
	const double Now = FPlatformTime::Seconds();
	const double LocalReadbackTimeout = 0.5; // seconds - local fallback if no global provided
	if (LibAVState)
	{
		for (int32 i = PendingReadbacks.Num() - 1; i >= 0; --i)
		{
			FPendingNV12Readback& rec = PendingReadbacks[i];

			// Validate readbacks
			if (!rec.ReadbackY || !rec.ReadbackUV)
			{
				// Nothing to do, remove malformed entry
				PendingReadbacks.RemoveAtSwap(i);
				continue;
			}

			// If readbacks ready, perform zero-copy encode and send to all target tracks
			if (rec.ReadbackY->IsReady() && rec.ReadbackUV->IsReady())
			{
				UE_LOG(LogTemp, Verbose, TEXT("Synavis: Pending readback ready, starting zero-copy encode (Width=%d Height=%d)"), rec.Width, rec.Height);
				EncodeNV12ReadbackAndSend(rec.ReadbackY, rec.ReadbackUV, rec.Width, rec.Height, rec.TargetTracks);
				// EncodeNV12ReadbackAndSend takes ownership of the readbacks via AVBuffer free callbacks,
				// so do not unlock/delete them here - remove entry from queue.
				PendingReadbacks.RemoveAtSwap(i);
				continue;
			}

			// If timeout expired: clean up and drop the readback (unlock & delete on render thread)
			if (Now - rec.EnqueuedAt > LocalReadbackTimeout)
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
}

// (Struct declarations live in the header to satisfy UHT; destructor definitions follow)

// NOTE: The previous internal WebRTC container was removed in the refactor; cleanup
// of signalling is handled by the USynavisStreamer destructor and the `Signalling` member.

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

bool USynavisStreamer::AnyConnectionStreaming() const
{
	for (const auto& Pair : Connections)
	{
		if (Pair.Value.bStreaming) return true;
	}
		return false;
}

void USynavisStreamer::StartStreaming()
{
	// Initialize persistent libav encoder state lazily and enable streaming on
	// existing connections. New connections default to bStreaming=false.
	if (!LibAVState)
	{
		LibAVState = new FLibAVEncoderState();
		LibAVState->Codec = avcodec_find_encoder(AV_CODEC_ID_VP9);
		// actual codec context will be created when first frame with size arrives
	}

	for (auto& Pair : Connections)
	{
		Pair.Value.bStreaming = true;
	}
}


void USynavisStreamer::StopStreaming()
{
	// Disable streaming on all connections and tear down encoder state.
	for (auto& Pair : Connections)
	{
		Pair.Value.bStreaming = false;
	}

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

void USynavisStreamer::StopStreaming(int32 ConnectionID)
{
	FSynavisConnection* Conn = FindConnectionByPlayerID(ConnectionID);
	if (!Conn)
	{
		UE_LOG(LogTemp, Warning, TEXT("StopStreaming: connection %d not found"), ConnectionID);
		return;
	}

	Conn->bStreaming = false;

	// If no connections are streaming anymore, free libav state
	if (!AnyConnectionStreaming())
	{
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
}

void USynavisStreamer::StartSignalling()
{
  // Signalling server
  //open/connect/autodiscover
  // Add handler for trickle ICE
  // Add handler for initial setup of connection
  // Synavis might attempt to setup first for simulation coupling.

	// Initialize libdatachannel components and create a datachannel
		// Initialize signalling-only state. PeerConnections are created per-player when the signalling
		// server notifies us of a playerConnected event. Keep the logger init.
		static bool bInitDone = false;
		if (!bInitDone) { rtc::InitLogger(rtc::LogLevel::Info); bInitDone = true; }

		// Wire signalling websocket callbacks to member handlers (keep onOpen/onError as small lambdas)
		if (!Signalling)
			Signalling = std::make_shared<rtc::WebSocket>();

		Signalling->onOpen([this]() { this->HandleSignallingOpen(); });
		Signalling->onClosed([this]() { this->HandleSignallingClose(); });
		Signalling->onError([this](std::string err) { this->HandleSignallingError(err); });
		Signalling->onMessage([this](auto messageOrData) { this->HandleSignallingMessage(messageOrData); });

		// Create signalling websocket and hook handlers (mirror DataConnector behavior)
		if (!Signalling)
			Signalling = std::make_shared<rtc::WebSocket>();
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
	// Communicate SDP for each active connection via signalling
	if (!Signalling || !Signalling->isOpen())
	{
		UE_LOG(LogTemp, Warning, TEXT("Synavis: Signalling websocket not open - cannot send local SDP"));
		return;
	}

	for (const auto& Pair : Connections)
	{
		CommunicateSDPForConnection(Pair.Value);
	}
}

void USynavisStreamer::CommunicateSDPForConnection(const FSynavisConnection& Conn)
{
	if (!Signalling)
		return;

	auto descriptionOpt = Conn.PeerConnection->localDescription();
	if (!descriptionOpt)
		return;

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	std::string typestr = descriptionOpt->typeString();
	std::string sdpStr = static_cast<std::string>(*descriptionOpt);
	Obj->SetStringField(TEXT("type"), FString(UTF8_TO_TCHAR(typestr.c_str())));
	Obj->SetStringField(TEXT("sdp"), FString(UTF8_TO_TCHAR(sdpStr.c_str())));
	Obj->SetNumberField(TEXT("playerId"), Conn.ConnectionID);

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	if (FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer))
	{
		std::string outcpp = TCHAR_TO_UTF8(*Out);
		try {
			Signalling->send(outcpp);
			UE_LOG(LogTemp, Log, TEXT("Synavis: Sent local SDP for connection %d via signalling"), Conn.ConnectionID);
		}
		catch (...) { UE_LOG(LogTemp, Warning, TEXT("Synavis: Failed to send SDP over signalling")); }
	}
}

FSynavisConnection* USynavisStreamer::FindConnectionByPlayerID(int32 PlayerID)
{
	return Connections.Find(PlayerID);
}

const FSynavisConnection* USynavisStreamer::FindConnectionByPlayerID(int32 PlayerID) const
{
	const FSynavisConnection* Found = nullptr;
	auto It = Connections.Find(PlayerID);
	if (It)
	{
		Found = It;
	}
	return Found;
}

void USynavisStreamer::CreateConnectionForPlayer(int32 PlayerID)
{
	// Skip if a connection already exists
	if (Connections.Contains(PlayerID))
	{
		UE_LOG(LogTemp, Log, TEXT("Synavis: Connection for player %d already exists"), PlayerID);
		return;
	}

	FSynavisConnection Conn;
	Conn.ConnectionID = PlayerID;
	// Inherit the current global streaming-request state (if any connections are
	// currently requesting streaming, enable for this new connection as well).
	Conn.bStreaming = AnyConnectionStreaming();
	try {
		Conn.PeerConnection = std::make_shared<rtc::PeerConnection>();
	}
	catch (const std::exception& e)
	{
		UE_LOG(LogTemp, Error, TEXT("Synavis: Exception creating PeerConnection for player %d: %s"), PlayerID, ANSI_TO_TCHAR(e.what()));
		return;
	}

	// Create tracks for each registered handler that has a SceneCapture (do not mutate the handler)
	TArray<FSynavisHandlers> HandlersCopy = RegisteredDataHandlers.Array();
	for (const FSynavisHandlers& HandlerCopy : HandlersCopy)
	{
		if (!HandlerCopy.Video.IsSet())
			continue;
		USceneCaptureComponent2D* SceneCap = HandlerCopy.Video.GetValue().Value;
		if (!SceneCap) continue;
		UTextureRenderTarget2D* VideoSource = SceneCap->TextureTarget;
		if (!VideoSource) continue;

		// Use the pre-created media descriptor from handler registration, if present
		std::shared_ptr<rtc::Description::Video> mediaDesc = HandlerCopy.MediaDesc;
		std::shared_ptr<rtc::Track> Track;
		if (mediaDesc)
		{
			Track = Conn.PeerConnection->addTrack(*mediaDesc);
		}
		else
		{
			rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
			media.addVP9Codec(96);
			media.setBitrate(90000);
			Track = Conn.PeerConnection->addTrack(media);
		}
		if (!Track)
		{
			UE_LOG(LogTemp, Warning, TEXT("Synavis: Failed to create video track for handler %d (player %d)"), HandlerCopy.HandlerID, PlayerID);
			continue;
		}

		// Attach callbacks and packetizer
		FString LogPrefix = LogSetup(HandlerCopy.HandlerID, SceneCap);
		Track->onOpen([LogPrefix]() { UE_LOG(LogTemp, Warning, TEXT("%s: Video track opened for connection"), *LogPrefix); });
		rtc::SSRC ssrc = static_cast<rtc::SSRC>(std::rand());
		auto rtpCfg = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, std::string("synavis"), 96, rtc::RtpPacketizer::VideoClockRate);
		auto packetizer = std::make_shared<rtc::RtpPacketizer>(rtpCfg);
		Track->setMediaHandler(packetizer);

		Conn.TracksByHandler.emplace(HandlerCopy.HandlerID, Track);
	}

	// Create a per-connection data channel for control/messages
	try {
		std::string channelName = std::string("synavis-data-") + std::to_string(PlayerID);
		Conn.DataChannel = Conn.PeerConnection->createDataChannel(channelName);
		if (Conn.DataChannel)
		{
			Conn.DataChannel->onMessage([this](const rtc::message_variant& msg) { this->OnDataChannelMessage(msg); });
			Conn.DataChannel->onOpen([this, PlayerID]() {
				FSynavisConnection* C = FindConnectionByPlayerID(PlayerID);
				if (C) C->State = EPeerState::ChannelOpen;
				UE_LOG(LogTemp, Log, TEXT("Synavis: DataChannel opened for player %d"), PlayerID);
			});
			Conn.DataChannel->onClosed([this, PlayerID]() {
				FSynavisConnection* C = FindConnectionByPlayerID(PlayerID);
				if (C) C->State = EPeerState::NoConnection;
				UE_LOG(LogTemp, Log, TEXT("Synavis: DataChannel closed for player %d"), PlayerID);
			});
		}
	}
	catch (const std::exception& e)
	{
		UE_LOG(LogTemp, Warning, TEXT("Synavis: Failed to create datachannel for player %d: %s"), PlayerID, ANSI_TO_TCHAR(e.what()));
	}

	// Create dedicated datachannels for handlers that requested them and map per-connection
	for (const FSynavisHandlers& HandlerCopy : HandlersCopy)
	{
		if (!HandlerCopy.WantsDedicatedChannel)
			continue;
		try {
			std::string channelName = std::string("synavis-datahandler-") + std::to_string(HandlerCopy.HandlerID) + std::string("-p") + std::to_string(PlayerID);
			auto dc = Conn.PeerConnection->createDataChannel(channelName);
				if (dc)
				{
					uint32 HId = HandlerCopy.HandlerID;
					dc->onMessage([this](const rtc::message_variant& msg) { this->OnDataChannelMessage(msg); });
					dc->onOpen([this, PlayerID, HId]() {
						FSynavisConnection* C = FindConnectionByPlayerID(PlayerID);
						if (C) C->State = EPeerState::ChannelOpen;
						UE_LOG(LogTemp, Log, TEXT("Synavis: Dedicated DataChannel opened for handler %d player %d"), HId, PlayerID);
					});
					dc->onClosed([]() {});
					Conn.DataChannelsByHandler.emplace(HandlerCopy.HandlerID, dc);
				}
		}
		catch (const std::exception& e)
		{
			UE_LOG(LogTemp, Warning, TEXT("Synavis: Failed to create dedicated datachannel for handler %d player %d: %s"), HandlerCopy.HandlerID, PlayerID, ANSI_TO_TCHAR(e.what()));
		}
	}

	// Insert into connections map before starting ICE so callbacks can find it
	Connections.Add(PlayerID, MoveTemp(Conn));

	// When gathering completes, communicate local SDP back to the signalling server
	auto& StoredConn = Connections[PlayerID];
	StoredConn.PeerConnection->onGatheringStateChange([this, PlayerID](rtc::PeerConnection::GatheringState state) {
		if (state == rtc::PeerConnection::GatheringState::Complete) {
			FSynavisConnection* Found = FindConnectionByPlayerID(PlayerID);
			if (Found)
			{
				CommunicateSDPForConnection(*Found);
			}
		}
	});

	// Finalize: set local description to start ICE gathering
	try {
		StoredConn.PeerConnection->setLocalDescription();
	}
	catch (const std::exception& e)
	{
		UE_LOG(LogTemp, Error, TEXT("Synavis: Exception while setting local description for player %d: %s"), PlayerID, ANSI_TO_TCHAR(e.what()));
	}

	UE_LOG(LogTemp, Log, TEXT("Synavis: Created connection object for player %d"), PlayerID);
}

void USynavisStreamer::RegisterRemoteCandidateForConnection(const FJsonObject& Content, FSynavisConnection& Conn)
{
	if (!Conn.PeerConnection)
		return;

	FString candStr;
  FString sdpMid; int32 sdpMLineIndex = -1;
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
				sdpMLineIndex = static_cast<int32>(Inner->GetNumberField(TEXT("sdpMLineIndex")));
		}
		else if (CandidateVal.IsValid() && CandidateVal->Type == EJson::String)
		{
			candStr = CandidateVal->AsString();
		}
	}

	if (Content.HasField(TEXT("sdpMid")) && sdpMid.IsEmpty())
  {
		sdpMid = Content.GetStringField(TEXT("sdpMid"));
  }
	if (Content.HasField(TEXT("sdpMLineIndex")) && sdpMLineIndex == -1)
  {
		sdpMLineIndex = static_cast<int32>(Content.GetNumberField(TEXT("sdpMLineIndex")));
  }

	if (candStr.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Synavis: Received iceCandidate message with no candidate field (conn %d)"), Conn.ConnectionID);
		return;
	}

	std::string scand = TCHAR_TO_UTF8(*candStr);
	std::string smid = TCHAR_TO_UTF8(*sdpMid);
	try
  {
		rtc::Candidate ice(scand, smid);
		Conn.PeerConnection->addRemoteCandidate(ice);
		UE_LOG(LogTemp, Log, TEXT("Synavis: Registered remote candidate for conn %d"), Conn.ConnectionID);
	}
	catch (const std::exception& e)
	{
		UE_LOG(LogTemp, Error, TEXT("Synavis: Exception registering candidate for conn %d: %s"), Conn.ConnectionID, ANSI_TO_TCHAR(e.what()));
	}
}

void USynavisStreamer::HandleSignallingOpen()
{
	UE_LOG(LogTemp, Log, TEXT("Synavis: Signalling websocket opened (member handler)"));
	this->ConnectionState = ESynavisState::SignallingUp;
}

void USynavisStreamer::HandleSignallingClose()
{
	UE_LOG(LogTemp, Log, TEXT("Synavis: Signalling websocket closed (member handler)"));
	this->ConnectionState = ESynavisState::Offline;
}

void USynavisStreamer::HandleSignallingError(const std::string& Err)
{
	UE_LOG(LogTemp, Error, TEXT("Synavis: Signalling websocket error: %s"), ANSI_TO_TCHAR(Err.c_str()));
	this->ConnectionState = ESynavisState::Failure;
}

void USynavisStreamer::HandleSignallingMessage(const std::variant<rtc::binary, std::string>& messageOrData)
{
	try 
  {
		if (std::holds_alternative<std::string>(messageOrData))
		{
			const std::string& s = std::get<std::string>(messageOrData);
			FJsonObject Parsed;
			if (!TryParseJSON(s, Parsed))
			{
				UE_LOG(LogTemp, Warning, TEXT("Synavis: Received non-JSON signalling text"));
				return;
			}

			FString Type;
			if (Parsed.HasField(TEXT("type")))
      {
				Type = Parsed.GetStringField(TEXT("type"));
      }

			if (Type.Equals(TEXT("playerConnected"), ESearchCase::IgnoreCase))
			{
				int32 PlayerID = -1;
				if (Parsed.HasField(TEXT("playerId"))) PlayerID = static_cast<int32>(Parsed.GetNumberField(TEXT("playerId")));
				if (PlayerID == -1 && Parsed.HasField(TEXT("PlayerID"))) PlayerID = static_cast<int32>(Parsed.GetNumberField(TEXT("PlayerID")));
				if (PlayerID == -1)
				{
					// Assign an internal connection id starting at 101 when signalling didn't provide one
					PlayerID = static_cast<int32>(CreateConnectionHandle());
					UE_LOG(LogTemp, Warning, TEXT("Synavis: playerConnected message missing playerId - generated id %d"), PlayerID);
				}
				CreateConnectionForPlayer(PlayerID);
				return;
			}

			// Routing of SDP / ICE messages to correct connection
			int32 TargetPlayer = -1;
			if (Parsed.HasField(TEXT("playerId")))
      {
        TargetPlayer = static_cast<int32>(Parsed.GetNumberField(TEXT("playerId")));
      }
			else if (Parsed.HasField(TEXT("PlayerID")))
      {
        TargetPlayer = static_cast<int32>(Parsed.GetNumberField(TEXT("PlayerID")));
      }
			if (Type.Equals(TEXT("answer"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("offer"), ESearchCase::IgnoreCase))
			{
				if (TargetPlayer == -1)
				{
					UE_LOG(LogTemp, Warning, TEXT("Synavis: SDP message missing playerId; ignoring"));
					return;
				}
				FSynavisConnection* Conn = FindConnectionByPlayerID(TargetPlayer);
				if (!Conn || !Conn->PeerConnection)
				{
					UE_LOG(LogTemp, Warning, TEXT("Synavis: Received SDP for unknown or invalid player %d"), TargetPlayer);
					return;
				}

				if (!Parsed.HasField(TEXT("sdp")))
				{
					UE_LOG(LogTemp, Warning, TEXT("Synavis: SDP message missing sdp field"));
					return;
				}
				FString sdpf = Parsed.GetStringField(TEXT("sdp"));
				std::string sdp = TCHAR_TO_UTF8(*sdpf);
				try {
					Conn->PeerConnection->setRemoteDescription(sdp);
					UE_LOG(LogTemp, Log, TEXT("Synavis: Set remote description for player %d"), TargetPlayer);
					// If remote sent an offer, we should create an answer and set local
					if (Type.Equals(TEXT("offer"), ESearchCase::IgnoreCase))
					{
						Conn->PeerConnection->setLocalDescription();
					}
				}
				catch (const std::exception& e)
				{
					UE_LOG(LogTemp, Error, TEXT("Synavis: Exception setting remote description for player %d: %s"), TargetPlayer, ANSI_TO_TCHAR(e.what()));
				}
				return;
			}

			if (Type.Equals(TEXT("iceCandidate"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("candidate"), ESearchCase::IgnoreCase))
			{
				if (TargetPlayer == -1)
				{
					UE_LOG(LogTemp, Warning, TEXT("Synavis: iceCandidate message missing playerId; ignoring"));
					return;
				}
				FSynavisConnection* Conn = FindConnectionByPlayerID(TargetPlayer);
				if (!Conn)
				{
					UE_LOG(LogTemp, Warning, TEXT("Synavis: Received ICE for unknown player %d"), TargetPlayer);
					return;
				}
				RegisterRemoteCandidateForConnection(Parsed, *Conn);
				return;
			}
		}
		else
		{
			// Binary signalling frames not expected in this use-case
			UE_LOG(LogTemp, Verbose, TEXT("Synavis: Received binary signalling frame (ignored)"));
		}
	}
	catch (const std::exception& e)
	{
		UE_LOG(LogTemp, Error, TEXT("Synavis: Exception in signalling message handler: %s"), ANSI_TO_TCHAR(e.what()));
	}
}

void USynavisStreamer::RegisterRemoteCandidate(const FJsonObject& Content)
{
	// Route to the correct per-player connection if playerId is present; otherwise broadcast to all
	int32 TargetPlayer = -1;
	if (Content.HasField(TEXT("playerId"))) TargetPlayer = static_cast<int32>(Content.GetNumberField(TEXT("playerId")));
	else if (Content.HasField(TEXT("PlayerID"))) TargetPlayer = static_cast<int32>(Content.GetNumberField(TEXT("PlayerID")));

	if (TargetPlayer != -1)
	{
		FSynavisConnection* Conn = FindConnectionByPlayerID(TargetPlayer);
		if (Conn != nullptr)
		{
			RegisterRemoteCandidateForConnection(Content, *Conn);
			return;
		}

		UE_LOG(LogTemp, Warning, TEXT("Synavis: Received remote candidate for unknown player %d"), TargetPlayer);
		return;
	}

	// No player specified: attempt to add to all connections
	for (auto& Pair : Connections)
	{
		RegisterRemoteCandidateForConnection(Content, Pair.Value);
	}
}

static uint32 CreatePawnHandle()
{
	static uint32 NextPawnHandle = 1;
	return NextPawnHandle++;
}

static uint32 CreateConnectionHandle()
{
	static uint32 NextConnHandle = 101;
	return NextConnHandle++;
}

FORCEINLINE FString LogSetup(uint32 ID, USceneComponent* Child)
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
			// Always prepare a media description for this handler; actual rtc::Track will be created per-connection
			auto Media = std::make_shared<rtc::Description::Video>(std::string("video"), rtc::Description::Direction::SendOnly);
			Media->addVP9Codec(96);
			Media->setBitrate(90000);
			Handler.MediaDesc = Media;

			// Register the video source but do not create a PeerConnection-local track here. Tracks are created when a connection is established.
			Handler.Video.Emplace(nullptr, SceneCapture);
    }
  }

	// If a dedicated channel was requested, mark it for creation per-connection; otherwise use the system channel
	if (DedicatedChannel)
	{
		Handler.WantsDedicatedChannel = true;
		Handler.DataChannel = std::nullopt;
	}
	else
	{
		Handler.WantsDedicatedChannel = false;
		Handler.DataChannel = SystemDataChannel;
	}

  RegisteredDataHandlers.Add(Handler);
  return Handler.HandlerID;
}

void USynavisStreamer::CaptureFrame()
{
	// Capture frames for registered handlers using only the zero-copy NV12 path.
	// The registration step is expected to have created a valid video track for each handler.
	// If there is no connection that requests streaming, skip capture.
	bool anyStreaming = false;
	for (const auto& Pair : Connections)
	{
		if (Pair.Value.bStreaming)
		{
			anyStreaming = true;
			break;
		}
	}
	if (!anyStreaming)
		return;

	for (const FSynavisHandlers& Handler : RegisteredDataHandlers)
	{
		if (!Handler.Video.IsSet())
			continue;
		USceneCaptureComponent2D* SceneCapture = Handler.Video.GetValue().Value;
		if (!SceneCapture)
		{
			UE_LOG(LogTemp, Warning, TEXT("Synavis: Handler missing required SceneCapture - skipping"));
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
			// Gather target tracks across all connections for this handler (only connections
			// that currently request streaming will receive frames).
			TArray<std::shared_ptr<rtc::Track>> TracksToSend;
			for (const auto& Pair : Connections)
			{
				const FSynavisConnection& Conn = Pair.Value;
				if (!Conn.bStreaming) continue;
				auto it = Conn.TracksByHandler.find(Handler.HandlerID);
				if (it != Conn.TracksByHandler.end() && it->second && it->second->isOpen())
				{
					TracksToSend.Add(it->second);
				}
			}

			if (TracksToSend.Num() == 0)
			{
				UE_LOG(LogTemp, Verbose, TEXT("Synavis: No open tracks for handler %d, skipping readback"), Handler.HandlerID);
				// cleanup readbacks immediately on render thread
				FRHIGPUTextureReadback* Yrb = ReadbackY;
				FRHIGPUTextureReadback* UVrb = ReadbackUV;
				ENQUEUE_RENDER_COMMAND(Synavis_CleanupReadbackImmediate)([Yrb, UVrb](FRHICommandListImmediate& RHICmdList)
				{
					if (Yrb) { Yrb->Unlock(); delete Yrb; }
					if (UVrb) { UVrb->Unlock(); delete UVrb; }
				});
			}
			else
			{
				FPendingNV12Readback rec;
				rec.ReadbackY = ReadbackY;
				rec.ReadbackUV = ReadbackUV;
				rec.EnqueuedAt = FPlatformTime::Seconds();
				rec.TargetTracks = TracksToSend;
				rec.Width = Width;
				rec.Height = Height;
				PendingReadbacks.Add(rec);
				UE_LOG(LogTemp, Verbose, TEXT("Synavis: Enqueued NV12 readback for handler %d (W=%d H=%d) to %d tracks"), Handler.HandlerID, Width, Height, TracksToSend.Num());
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Synavis: Failed to enqueue NV12 readback for handler"));
		}
	}
}

void USynavisStreamer::SendFrameBytes(const TArray<uint8>& Bytes, const FString& Name, const FString& Format, std::shared_ptr<rtc::Track> TargetTrack)
{
	// Send outbound bytes via the best available path: prefer the provided TargetTrack (if open),
	// otherwise fall back to the system data channel. Previously this depended on a global
	// container; now we simply check the relevant targets directly.
	size_t sz = static_cast<size_t>(Bytes.Num());
	if (sz == 0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("Synavis: SendFrameBytes called with empty payload (Name=%s, Format=%s)"), *Name, *Format);
		return;
	}

	// If a handler-specific RTC track is available and open, use it (video path)
	if (TargetTrack && TargetTrack->isOpen())
	{
		rtc::binary buf(sz);
		memcpy(buf.data(), Bytes.GetData(), sz);
		TargetTrack->send(std::move(buf));
		return;
	}

	// Otherwise try the global/system data channel
	if (SystemDataChannel && SystemDataChannel->isOpen())
	{
		rtc::binary buf(sz);
		memcpy(buf.data(), Bytes.GetData(), sz);
		SystemDataChannel->sendBuffer(buf);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("No open DataChannel or Track to send frame bytes (Name=%s, Format=%s)"), *Name, *Format);
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
void USynavisStreamer::EncodeNV12ReadbackAndSend(FRHIGPUTextureReadback* ReadbackY, FRHIGPUTextureReadback* ReadbackUV, int Width, int Height, const TArray<std::shared_ptr<rtc::Track>>& TargetTracks)
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
		// Prepare packet data
		size_t sz = static_cast<size_t>(LibAVState->Packet->size);
		const uint8_t* data = LibAVState->Packet->data;

		if (sz == 0)
		{
			av_packet_unref(LibAVState->Packet);
			continue;
		}

		// Determine dispatch targets. Prefer explicit TargetTracks (populated at capture time),
		// otherwise fall back to all currently-open tracks across connections so we don't silently drop images.
		TArray<std::shared_ptr<rtc::Track>> DispatchTargets = TargetTracks;
		if (DispatchTargets.Num() == 0)
		{
			// Gather all open tracks from all connections
			for (const auto& Pair : Connections)
			{
				const FSynavisConnection& Conn = Pair.Value;
				// if TracksByHandler exists for the connection, iterate it
				for (const auto& kv : Conn.TracksByHandler)
				{
					const std::shared_ptr<rtc::Track>& T = kv.second;
					if (T && T->isOpen())
						DispatchTargets.Add(T);
				}
			}
		}

		if (DispatchTargets.Num() > 0)
		{
			UE_LOG(LogTemp, Verbose, TEXT("Synavis: Sending encoded packet size=%d to %d handler track(s)"), (int)sz, DispatchTargets.Num());
			uint32_t ts = 0;
			if (LibAVState->Packet && LibAVState->Packet->pts != AV_NOPTS_VALUE)
				ts = static_cast<uint32_t>(LibAVState->Packet->pts);
			rtc::FrameInfo fi(ts);
			fi.payloadType = 96;
			for (const auto& T : DispatchTargets)
			{
				if (T && T->isOpen())
				{
					rtc::binary copybuf(sz);
					if (sz) memcpy(copybuf.data(), data, sz);
					T->sendFrame(std::move(copybuf), fi);
				}
			}
		}
		// No open tracks: try the system data channel
		else if (SystemDataChannel && SystemDataChannel->isOpen())
		{
			static thread_local rtc::binary dcbuf;
			dcbuf.resize(sz);
			if (sz) memcpy(dcbuf.data(), data, sz);
			SystemDataChannel->sendBuffer(dcbuf);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Synavis: Encoded packet dropped - no open tracks or datachannel"));
		}
		av_packet_unref(LibAVState->Packet);
	}

	av_frame_free(&frame);
}

