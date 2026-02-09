#include "MediaReceiver.hpp"
#include <utility>

#include <iostream>
#include <algorithm>
#include <rtc/rtc.hpp>

// global private logger initialization
static std::string Prefix = "MediaReceiver: ";
static const Synavis::Logger::LoggerInstance lmedia = Synavis::Logger::Get()->LogStarter("MediaReceiver");


// literal for converting to byte
constexpr std::byte operator"" _b(unsigned long long int Value)
{
  return static_cast<std::byte>(Value);
}




Synavis::MediaReceiver::MediaReceiver()
{
}

Synavis::MediaReceiver::~MediaReceiver()
{
  if(FrameRelay)
    FrameRelay->Disconnect();
}

void Synavis::MediaReceiver::Initialize()
{
  DataConnector::Initialize();
  lmedia(ELogVerbosity::Warning) << "Initializing MediaReceiver" << std::endl;
  const unsigned int bitrate = 90000;

  // amazon h264 codec : "packetization-mode=1;profile-level-id=42e01f"
  // source: https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/producer-reference-nal.html
  PeerConnection->onTrack([this](std::shared_ptr<rtc::Track> Track)
  {
    lmedia(ELogVerbosity::Debug) << "PeerConnection onTrack" << std::endl;
    if (std::find(this->theirTracks.begin(), this->theirTracks.end(), Track) == this->theirTracks.end())
    {
      // check if track is a video track
      auto description = Track->description();
      // ensure that we have the callback installed as precaution
        Track->onMessage(std::bind(&MediaReceiver::MessageHandler, this, std::placeholders::_1));
        // if track is a video track, set it as theirTrack
      if (description.type() == "video")
      {
        lmedia(ELogVerbosity::Debug) << "Track is a video track" << std::endl;
        this->theirTracks.push_back(Track);
        // Create a dedicated RTCP receiving session for this track and set it
        
        auto session = std::make_shared<rtc::RtcpReceivingSession>();
        Track->setMediaHandler(session);
        this->theirRtcpSessions.push_back(session);

        // Diagnostics: verify handler assignment and exercise RTCP helper calls
        try
        {
          auto current = Track->getMediaHandler();
          lmedia(ELogVerbosity::Info) << "Created RtcpReceivingSession for track mid " << Track->mid()
                                      << "; handler_ptr=" << static_cast<const void*>(current.get())
                                      << " expected_ptr=" << static_cast<const void*>(session.get()) << std::endl;

          // Prepare a lightweight send callback that logs outgoing RTCP messages
          rtc::message_callback send_cb = [mid = Track->mid()](rtc::message_ptr msg)
          {
            try
            {
              Synavis::Logger::Get()->LogStarter("MediaReceiver")(ELogVerbosity::Info)
                << "RTCP send callback for track mid " << mid << " msg size=" << (msg ? msg->size() : 0)
                << " type=" << (msg ? msg->type : rtc::Message::Binary) << std::endl;
            }
            catch (...) { /* best-effort logging */ }
          };

          // Try requesting a keyframe and bitrate to see whether session will attempt to send RTCP
          bool rk = session->requestKeyframe(send_cb);
          lmedia(ELogVerbosity::Debug) << "RtcpReceivingSession::requestKeyframe returned " << rk << std::endl;

          bool rb = session->requestBitrate(90000, send_cb);
          lmedia(ELogVerbosity::Debug) << "RtcpReceivingSession::requestBitrate returned " << rb << std::endl;
        }
        catch (const std::exception &e)
        {
          lmedia(ELogVerbosity::Error) << "Diagnostics on RtcpReceivingSession failed: " << e.what() << std::endl;
        }
        // Also register an onFrame handler so depacketized frames (if produced by a packetizer)
        // are forwarded to the FrameReceptionCallback (e.g. FrameDecode acceptor).
        Track->onFrame(std::bind(&MediaReceiver::FrameHandler, this, std::placeholders::_1, std::placeholders::_2));
        Track->onOpen([this, NewTrack = Track]()
          {
            lmedia(ELogVerbosity::Info) << "THEIR Track labeled opened with mid " << NewTrack->mid() << std::endl;
            // inform any external callback that the track opened
            if (OnTrackOpenCallback.has_value())
            {
              try { OnTrackOpenCallback.value()(); }
              catch (const std::exception &e) { lmedia(ELogVerbosity::Error) << "OnTrackOpenCallback threw: " << e.what() << std::endl; }
              catch (...) { lmedia(ELogVerbosity::Error) << "OnTrackOpenCallback threw unknown exception" << std::endl; }
            }

            //StartStreaming();
            //NewTrack->send(rtc::binary({ (std::byte)(EClientMessageType::QualityControlOwnership) }));
            if(FrameRelay)
            {
              FrameRelay->Connect();
            }
          });
        Track->onClosed([this, NewTrack = Track]()
          {
            lmedia(ELogVerbosity::Debug) << "THEIR Track with mid " << NewTrack->mid() << " closed" << std::endl;
            // remove closed track from collection
            try
            {
              auto it = std::find(this->theirTracks.begin(), this->theirTracks.end(), NewTrack);
              if (it != this->theirTracks.end()) this->theirTracks.erase(it);
            }
            catch (...) {}
            if (OnTrackCloseCallback.has_value())
            {
              try { OnTrackCloseCallback.value()(); }
              catch (const std::exception &e) { lmedia(ELogVerbosity::Error) << "OnTrackCloseCallback threw: " << e.what() << std::endl; }
              catch (...) { lmedia(ELogVerbosity::Error) << "OnTrackCloseCallback threw unknown exception" << std::endl; }
            }
          });
      }
      else
      {
        lmedia(ELogVerbosity::Debug) << "Track is not a video track but instead " << description.type() << std::endl;
        return;
      }
    }
  });


  // Only create a local offer if this connector is configured to take the first step.
  // If we are not the offerer, do not set a local description here — wait for a
  // remote offer and create an answer in response. Calling setLocalDescription(Answer)
  // proactively can cause signaling-state mismatches in some runtimes.
  if (TakeFirstStep)
  {
    PeerConnection->setLocalDescription(rtc::Description::Type::Offer);
  }
  if (!PeerConnection->hasMedia())
  {
    std::cout << "Media Constructor: PeerConnection has no media" << std::endl;
  }
  else
  {
    std::cout << "Media Constructor: PeerConnection has media" << std::endl;
  }

  // we overwrite the signalling server on open to communicate to the signalling server that we NEED media
  SignallingServer->onOpen([this]()
  {
    Synavis::MediaReceiver::json msg = { {"type","needMedia"} };
    SignallingServer->send(msg.dump());
  });
}

void Synavis::MediaReceiver::ConfigureRelay(std::string IP, int Port)
{
  if(!FrameRelay)
    FrameRelay = std::make_shared<BridgeSocket>();
  FrameRelay->Outgoing = true;
  FrameRelay->SetAddress(IP);
  FrameRelay->SetSocketPort(Port);
  if(!FrameRelay->Connect())
  {
    lmedia(ELogVerbosity::Error) << "Could not connect to FrameRelay" << std::endl;
    lmedia(ELogVerbosity::Error) << "FrameRelay: " << FrameRelay->GetAddress() << ":" << FrameRelay->GetSocketPort() << std::endl;
    lmedia(ELogVerbosity::Error) << "What says: " << FrameRelay->What() << std::endl;
    throw std::runtime_error("Could not connect to FrameRelay");
  }
  else
  {
    lmedia(ELogVerbosity::Info) << "Connected to FrameRelay" << std::endl;
  }
}

int Synavis::MediaReceiver::NumRemoteMedia()
{
  if (!PeerConnection) return 0;
  auto rd = PeerConnection->remoteDescription();
  if (!rd) return 0;
  return rd->mediaCount();
}

Synavis::MediaReceiver::json Synavis::MediaReceiver::RemoteMediaDescription(int id)
{
  json out = json::object();
  if (!PeerConnection) return out;
  auto rd = PeerConnection->remoteDescription();
  if (!rd) return out;
  int count = rd->mediaCount();
  if (id < 0 || id >= count) return out;
  auto var = rd->media(id);
  // variant holds either Media* or Application*
  if (std::holds_alternative<rtc::Description::Media*>(var))
  {
    auto m = std::get<rtc::Description::Media*>(var);
    out["mid"] = m->mid();
    out["type"] = m->type();
    out["bitrate"] = m->bitrate();
    // payload types
    std::vector<int> pts = m->payloadTypes();
    out["payloadTypes"] = json::array();
    for (int pt : pts)
    {
      json entry;
      entry["payloadType"] = pt;
      if (auto r = m->rtpMap(pt))
      {
        entry["format"] = r->format;
        entry["clockRate"] = r->clockRate;
        entry["encParams"] = r->encParams;
        entry["fmtps"] = r->fmtps;
      }
      out["payloadTypes"].push_back(entry);
    }
  }
  else if (std::holds_alternative<rtc::Description::Application*>(var))
  {
    auto a = std::get<rtc::Description::Application*>(var);
    out["mid"] = a->mid();
    out["type"] = "application";
    if (a->sctpPort()) out["sctpPort"] = *a->sctpPort();
    if (a->maxMessageSize()) out["maxMessageSize"] = *a->maxMessageSize();
  }
  return out;
}

void Synavis::MediaReceiver::PrintCommunicationData()
{
  DataConnector::PrintCommunicationData();
  if (FrameRelay)
    lmedia(ELogVerbosity::Info) << "FrameRelay: " << FrameRelay->GetAddress() << ":" << FrameRelay->GetSocketPort() << std::endl;
  if (theirTracks.empty())
  {
    lmedia(ELogVerbosity::Info) << "No tracks present" << std::endl;
  }
  else
  {
    for (auto &t : theirTracks)
    {
      if (!t) continue;
      lmedia(ELogVerbosity::Info) << "Track (" << t->mid() << ") has a maximum Message size of " << t->maxMessageSize() << std::endl;
    }
  }
}

void Synavis::MediaReceiver::RequestKeyFrame()
{
  for (auto &t : theirTracks)
  {
    if (t) t->requestKeyframe();
  }
}

void Synavis::MediaReceiver::SendMouseClick()
{

  // mouse down: length 5 button uint8 x uint16 y uint16
  rtc::binary m_down = { 72_b, 0_b, 0_b, 0_b, 0_b, 0_b };
  rtc::binary m_up = { 73_b, 0_b, 0_b, 0_b, 0_b, 0_b };

  if (auto ch = this->GetDataChannel()) ch->send(m_down);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  if (auto ch = this->GetDataChannel()) ch->send(m_up);
}

void Synavis::MediaReceiver::StartStreaming()
{
  if (auto ch = this->GetDataChannel()) ch->send(rtc::binary({ 4_b }));
}

void Synavis::MediaReceiver::StopStreaming()
{
  if (auto ch = this->GetDataChannel())
  {
    ch->send(rtc::binary({ 5_b }));
  }
}

void Synavis::MediaReceiver::MessageHandler(rtc::message_variant DataOrMessage)
{
  // Minimal message handler: forward binary payloads to the FrameReceptionCallback.
  lmedia(ELogVerbosity::Verbose) << "MessageHandler called" << std::endl;
  // If this is a binary RTP message, hand it to the VP9 depacketizer so
  // frames can be assembled and delivered via the frame callback.
  if (std::holds_alternative<rtc::binary>(DataOrMessage))
  {
    auto bin = std::get<rtc::binary>(DataOrMessage);
    lmedia(ELogVerbosity::Verbose) << "MessageHandler: binary message size=" << bin.size() << std::endl;

    // If an external FrameReceptionCallback is installed (e.g. FrameDecode::CreateAcceptor),
    // forward the raw RTP packet and a constructed FrameInfo so the acceptor receives
    // the packet with RTP headers intact (FrameDecode expects RTP packets).
    if (this->FrameReceptionCallback.has_value())
    {
      // parse RTP header fields directly and construct FrameInfo
      const rtc::RtpHeader* Header = reinterpret_cast<const rtc::RtpHeader*>(bin.data());
      uint32_t ts = Header->timestamp();
      rtc::FrameInfo info(ts);
      info.payloadType = Header->payloadType();
      bool handled = this->FrameReceptionCallback.value()(bin, info);
      lmedia(ELogVerbosity::Info) << "MessageHandler: forwarded raw RTP to FrameReceptionCallback handled=" << (handled ? 1 : 0) << std::endl;
      if (handled)
      {
        // acceptor will handle packet assembly/decoding; do not double-depacketize
        return;
      }
      // otherwise fall through and let local depacketizer attempt assembly
    }

 
  }

  // forward to MessageHandler if set
  if(this->MessageCallback.has_value())
  {
    this->MessageCallback.value()(DataOrMessage);
  }
}

void Synavis::MediaReceiver::FrameHandler(rtc::binary FrameData, rtc::FrameInfo Info)
{
  lmedia(ELogVerbosity::Verbose) << "FrameHandler called, size=" << FrameData.size() << std::endl;
  // log an explicit decode/dispatch attempt so avcodec activity is observable
  lmedia(ELogVerbosity::Info) << "FrameHandler: attempting decode/dispatch size=" << FrameData.size()
                               << " ts=" << Info.timestamp << " payloadType=" << int(Info.payloadType) << std::endl;

    if (FrameReceptionCallback.has_value())
    {
      bool handled = false;
      handled = FrameReceptionCallback.value()(FrameData, Info);

      // Log whether the frame was handled by the receiver's acceptor
      lmedia(ELogVerbosity::Info) << "FrameHandler: FrameReceptionCallback handled=" << (handled ? 1 : 0)
                                   << " size=" << FrameData.size() << " ts=" << Info.timestamp << std::endl;

      // If callback didn't handle the frame, forward to MessageHandler for fallback processing.
      if (!handled)
      {
        this->MessageHandler(FrameData);
      }
    }
  // Forward raw frame bytes to relay if configured
  if (FrameRelay)
  {
    FrameRelay->Send(FrameData);
  }
}
