#include "MediaReceiver.hpp"

#include <iostream>
#include <rtc/rtc.hpp>


#define LVERBOSE if(LogVerbosity >= ELogVerbosity::Verbose) std::cout << Prefix 
#define LDEBUG if(LogVerbosity >= ELogVerbosity::Debug) std::cout << Prefix 
#define LINFO if(LogVerbosity >= ELogVerbosity::Info) std::cout << Prefix 
#define LWARNING if(LogVerbosity >= ELogVerbosity::Warning) std::cout << Prefix 
#define LERROR if(LogVerbosity >= ELogVerbosity::Error) std::cerr << Prefix

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
  FrameRelay->Disconnect();
}

void Synavis::MediaReceiver::Initialize()
{
  DataConnector::Initialize();
  LINFO << "MediaReceiver created" << std::endl;
  const unsigned int bitrate = 90000;
  FrameRelay = std::make_shared<BridgeSocket>();
  FrameRelay->Outgoing = true;
  FrameRelay->Address = "127.0.0.1";
  FrameRelay->Port = 5535;
  MediaDescription.setDirection(rtc::Description::Direction::RecvOnly);
  MediaDescription.setBitrate(bitrate);
  RtcpReceivingSession = std::make_shared<rtc::RtcpReceivingSession>();
  switch (Codec)
  {
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
      LDEBUG << "PeerConnection onTrack" << std::endl;
      if (Track != this->Track)
      {
        // check if track is a video track
        auto description = Track->description();
        // if track is a video track, set it as theirTrack
        if (description.description().contains("video"))
        {
          LDEBUG << "Track is a video track" << std::endl;
          this->theirTrack = Track;
        }
        else
        {
          LDEBUG << "Track is not a video track but instead " << std::endl;
          return;
        }
      }
      Track->setMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());
      this->theirTrack->onOpen([this, NewTrack = Track]()
        {
          LDEBUG << "THEIR Track opened" << std::endl;

          NewTrack->requestKeyframe();
          //NewTrack->send(rtc::binary({ (std::byte)(EClientMessageType::QualityControlOwnership) }));
          FrameRelay->Connect();
        });
      this->theirTrack->onMessage(std::bind(&MediaReceiver::MediaHandler, this, std::placeholders::_1));
      this->theirTrack->onClosed([this]()
        {
          LDEBUG << "THEIR Track ended" << std::endl;
        });
    });
  Track = PeerConnection->addTrack(MediaDescription);
  Track->onAvailable([]() {});
  Track->onOpen([this]()
    {
      LDEBUG << "OUR Track opened" << std::endl;
      FrameRelay->Connect();
      if (this->DataChannel->isOpen())
      {
        DataChannel->send(rtc::binary({ (std::byte)1, (std::byte)(EClientMessageType::InitialSettings) }));
        DataChannel->send(rtc::binary({ (std::byte)1, (std::byte)(EClientMessageType::QualityControlOwnership) }));
      }
    });
  Track->onMessage(std::bind(&MediaReceiver::MediaHandler, this, std::placeholders::_1));


  PeerConnection->setLocalDescription();
  if (!PeerConnection->hasMedia())
  {
    std::cout << "Media Constructor: PeerConnection has no media" << std::endl;
  }
  else
  {
    std::cout << "Media Constructor: PeerConnection has media" << std::endl;
  }
}

void Synavis::MediaReceiver::ConfigureRelay(std::string IP, int Port)
{
  FrameRelay->SetAddress(IP);
  FrameRelay->SetSocketPort(Port);
}

void Synavis::MediaReceiver::PrintCommunicationData()
{
  DataConnector::PrintCommunicationData();
  std::cout << "FrameRelay: " << FrameRelay->GetAddress() << ":" << FrameRelay->GetSocketPort() << std::endl;
  std::cout << "Track (" << Track->mid() << ") has a maximum Message size of " << Track->maxMessageSize() << std::endl;
}

void Synavis::MediaReceiver::RequestKeyFrame()
{
  Track->requestKeyframe();
}

void Synavis::MediaReceiver::SendMouseClick()
{

  // mouse down: length 5 button uint8 x uint16 y uint16
  rtc::binary m_down = {  72_b, 0_b, 0_b, 0_b, 0_b, 0_b };
  rtc::binary m_up = {  73_b, 0_b, 0_b, 0_b, 0_b, 0_b };

  DataChannel->send(m_down);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  DataChannel->send(m_up);
}

void Synavis::MediaReceiver::MediaHandler(rtc::message_variant DataOrMessage)
{
  if (std::holds_alternative<rtc::binary>(DataOrMessage))
  {

#ifdef SYNAVIS_UPDATE_TIMECODE
    auto Frame = std::get<rtc::binary>(DataOrMessage);
    auto* RTP = reinterpret_cast<rtc::RtpHeader*>(Frame.data());
    // set timestamp to unix time
    RTP->setTimestamp(static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()));
#endif // SYNAVIS_UPDATE_TIMECODE

    //Track->requestKeyframe();
    if (FrameReceptionCallback.has_value())
    {
      FrameReceptionCallback.value()(std::get<rtc::binary>(DataOrMessage));
    }
    FrameRelay->Send(std::get<rtc::binary>(DataOrMessage));
  }
  else if (std::holds_alternative<std::string>(DataOrMessage))
  {
    auto Message = std::get<std::string>(DataOrMessage);
    rtc::binary MessageBinary((std::byte*)Message.data(), (std::byte*)(Message.data() + Message.size()));
    FrameRelay->Send(MessageBinary);
  }
}
