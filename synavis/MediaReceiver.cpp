#include "MediaReceiver.hpp"

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
  // empty on purpose (for libdatachannel config changes)
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
  //if(!FrameRelay)
  //  FrameRelay = std::make_shared<BridgeSocket>();
  //FrameRelay->Outgoing = true;
  //FrameRelay->Address = "127.0.0.1";
  //FrameRelay->Port = 5535;
  MediaDescription.setDirection(rtc::Description::Direction::RecvOnly);
  MediaDescription.setBitrate(bitrate);
  RtcpReceivingSession = std::make_shared<rtc::RtcpReceivingSession>();
  switch (Codec)
  {
  default:
  case ECodec::H264:
    MediaDescription.addH264Codec(96);
    break;
  case ECodec::H265:
    MediaDescription.addVideoCodec(96, "H265", "MAIN");
    break;
  case ECodec::VP8:
    MediaDescription.addVP8Codec(96);
    break;
  case ECodec::VP9:
    MediaDescription.addVP9Codec(96);
    break;
  }
  // amazon h264 codec : "packetization-mode=1;profile-level-id=42e01f"
  // source: https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/producer-reference-nal.html
  PeerConnection->onTrack([this](std::shared_ptr<rtc::Track> Track)
  {
    lmedia(ELogVerbosity::Debug) << "PeerConnection onTrack" << std::endl;
    if (std::find(this->theirTracks.begin(), this->theirTracks.end(), Track) == this->theirTracks.end())
    {
      // check if track is a video track
      auto description = Track->description();
      // if track is a video track, set it as theirTrack
      if (description.type() == "video")
      {
        lmedia(ELogVerbosity::Debug) << "Track is a video track" << std::endl;
        this->theirTracks.push_back(Track);
        Track->setMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());
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
        Track->onMessage(std::bind(&MediaReceiver::MediaHandler, this, std::placeholders::_1));
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
  if (auto ch = this->GetDataChannel()) ch->send(rtc::binary({ 5_b }));
}

void Synavis::MediaReceiver::MediaHandler(rtc::message_variant DataOrMessage)
{
  lmedia(ELogVerbosity::Verbose) << "MediaHandler called" << std::endl;
  if (std::holds_alternative<rtc::binary>(DataOrMessage))
  {
    lmedia(ELogVerbosity::Verbose) << "MediaHandler: binary message received: size=" << std::get<rtc::binary>(DataOrMessage).size() << std::endl;
#ifdef SYNAVIS_UPDATE_TIMECODE
    auto Frame = std::get<rtc::binary>(DataOrMessage);
    auto* RTP = reinterpret_cast<rtc::RtpHeader*>(Frame.data());
    // set timestamp to unix time
    RTP->setTimestamp(static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()));
#endif // SYNAVIS_UPDATE_TIMECODE

    //Track->requestKeyframe();
    if (FrameReceptionCallback.has_value())
    {
      try
      {
        auto &bin = std::get<rtc::binary>(DataOrMessage);
        lmedia(ELogVerbosity::Debug) << "MediaHandler: binary message received size=" << bin.size() << std::endl;
        FrameReceptionCallback.value()(bin);
      }
      catch (const std::bad_variant_access&)
      {
        lmedia(ELogVerbosity::Warning) << "MediaHandler: expected binary but variant access failed" << std::endl;
      }
      catch (const std::exception &e)
      {
        lmedia(ELogVerbosity::Error) << "MediaHandler: FrameReceptionCallback threw: " << e.what() << std::endl;
      }
    }
    if(FrameRelay)
      FrameRelay->Send(std::get<rtc::binary>(DataOrMessage));
}
  else if (std::holds_alternative<std::string>(DataOrMessage))
  {
    auto Message = std::get<std::string>(DataOrMessage);
    rtc::binary MessageBinary((std::byte*)Message.data(), (std::byte*)(Message.data() + Message.size()));
    //FrameRelay->Send(MessageBinary);
  }
}
