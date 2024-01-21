#include "MediaReceiver.hpp"

#include <iostream>
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
  FrameRelay->Disconnect();
}

void Synavis::MediaReceiver::Initialize()
{
  DataConnector::Initialize();
  lmedia(ELogVerbosity::Warning) << "Initializing MediaReceiver" << std::endl;
  const unsigned int bitrate = 90000;
  if(!FrameRelay)
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
    lmedia(ELogVerbosity::Debug) << "PeerConnection onTrack" << std::endl;
    if (Track != this->Track)
    {
      // check if track is a video track
      auto description = Track->description();
      // if track is a video track, set it as theirTrack
      if (description.type() == "video")
      {
        lmedia(ELogVerbosity::Debug) << "Track is a video track" << std::endl;
        this->theirTrack = Track;
        Track->setMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());
        this->theirTrack->onOpen([this, NewTrack = Track]()
          {
            lmedia(ELogVerbosity::Debug) << "THEIR Track opened" << std::endl;

            //StartStreaming();
            //NewTrack->send(rtc::binary({ (std::byte)(EClientMessageType::QualityControlOwnership) }));
            FrameRelay->Connect();
          });
        this->theirTrack->onMessage(std::bind(&MediaReceiver::MediaHandler, this, std::placeholders::_1));
        this->theirTrack->onClosed([this]()
          {
            lmedia(ELogVerbosity::Debug) << "THEIR Track ended" << std::endl;
          });
      }
      else
      {
        lmedia(ELogVerbosity::Debug) << "Track is not a video track but instead " << description.type() << std::endl;
        return;
      }
    }
  });
  Track = PeerConnection->addTrack(MediaDescription);
  Track->onAvailable([]() {});
  Track->onOpen([this]()
  {
    lmedia(ELogVerbosity::Debug) << "OUR Track opened" << std::endl;
    FrameRelay->Connect();
    if (this->DataChannel->isOpen())
    {
      //StartStreaming();
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
  if(!FrameRelay)
    FrameRelay = std::make_shared<BridgeSocket>();
  FrameRelay->SetAddress(IP);
  FrameRelay->SetSocketPort(Port);
}

void Synavis::MediaReceiver::PrintCommunicationData()
{
  DataConnector::PrintCommunicationData();
  lmedia(ELogVerbosity::Info) << "FrameRelay: " << FrameRelay->GetAddress() << ":" << FrameRelay->GetSocketPort() << std::endl;
  lmedia(ELogVerbosity::Info) << "Track (" << Track->mid() << ") has a maximum Message size of " << Track->maxMessageSize() << std::endl;
}

void Synavis::MediaReceiver::RequestKeyFrame()
{
  Track->requestKeyframe();
}

void Synavis::MediaReceiver::SendMouseClick()
{

  // mouse down: length 5 button uint8 x uint16 y uint16
  rtc::binary m_down = { 72_b, 0_b, 0_b, 0_b, 0_b, 0_b };
  rtc::binary m_up = { 73_b, 0_b, 0_b, 0_b, 0_b, 0_b };

  DataChannel->send(m_down);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  DataChannel->send(m_up);
}

void Synavis::MediaReceiver::StartStreaming()
{
  DataChannel->send(rtc::binary({ 4_b,0_b }));
}

void Synavis::MediaReceiver::StopStreaming()
{
  DataChannel->send(rtc::binary({ 5_b,0_b }));
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
