#include "MediaReceiver.hpp"

#include <iostream>
#include <rtc/rtc.hpp>

Synavis::MediaReceiver::MediaReceiver()
  : Synavis::DataConnector()
{
  std::cout << "MediaReceiver created" << std::endl;
  const unsigned int bitrate = 3000;
  FrameRelay = std::make_shared<BridgeSocket>();
  FrameRelay->Outgoing = true;

  FrameRelay->Address = "127.0.0.1";
  FrameRelay->Port = 5535;
  MediaDescription.setDirection(rtc::Description::Direction::RecvOnly);
  MediaDescription.setBitrate(bitrate);
  // amazon h264 codec : "packetization-mode=1;profile-level-id=42e01f"
  // source: https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/producer-reference-nal.html
  // MediaDescription.addH264Codec(96, "packetization-mode=1;profile-level-id=42e01f");
  MediaDescription.addH264Codec(96);
  PeerConnection->onTrack([this](std::shared_ptr<rtc::Track> Track)
  {
    std::cout << "PeerConnection onTrack" << std::endl;
  Track->onOpen([this,NewTrack = Track]()
    {
      std::cout << "THEIR Track opened" << std::endl;
      NewTrack->requestKeyframe();
      FrameRelay->Connect();
    });
    Track->onMessage(std::bind(&MediaReceiver::MediaHandler, this, std::placeholders::_1));
    Track->onClosed([this]()
      {
        std::cout << "Track ended" << std::endl;
      });
  });
  Track = PeerConnection->addTrack(MediaDescription);
  RtcpReceivingSession = std::make_shared<rtc::RtcpReceivingSession>();
  Track->setMediaHandler(RtcpReceivingSession);
  Track->onAvailable([]() {});
  Track->onOpen([this]()
    {
      std::cout << "OUR Track opened" << std::endl;
      Track->requestKeyframe();
      FrameRelay->Connect();
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

Synavis::MediaReceiver::~MediaReceiver()
{
  FrameRelay->Disconnect();
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

std::vector<uint8_t> Synavis::MediaReceiver::DecodeFrame(rtc::binary Frame)
{
  // Decode a H264 frame into a vector of bytes
  std::vector<uint8_t> DecodedFrame(2000 * 1000);

  // We need to add the NALU start code to the frame
  // https://stackoverflow.com/questions/26451094/what-is-the-nalu-start-code

  return DecodedFrame;
}

void Synavis::MediaReceiver::RequestKeyFrame()
{
   Track->requestKeyframe();
}

void Synavis::MediaReceiver::MediaHandler(rtc::message_variant DataOrMessage)
{
  if (std::holds_alternative<rtc::binary>(DataOrMessage))
  {
    if (FrameReceptionCallback.has_value())
    {
      FrameReceptionCallback.value()(std::get<rtc::binary>(DataOrMessage));
    }
    FrameRelay->Send(std::get<rtc::binary>(DataOrMessage));
  }
}
