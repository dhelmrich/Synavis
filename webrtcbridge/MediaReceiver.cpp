#include "MediaReceiver.hpp"

#include <iostream>
#include <rtc/rtc.hpp>

WebRTCBridge::MediaReceiver::MediaReceiver()
  : WebRTCBridge::DataConnector()
{
  std::cout << "MediaReceiver created" << std::endl;
  const unsigned int bitrate = 1000000;
  FrameRelay = std::make_shared<BridgeSocket>();
  FrameRelay->SetAddress("localhost");
  FrameRelay->SetSocketPort(5535);
  MediaDescription.setDirection(rtc::Description::Direction::RecvOnly);
  MediaDescription.setBitrate(bitrate);
  // amazon h264 codec : "packetization-mode=1;profile-level-id=42e01f"
  // source: https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/producer-reference-nal.html
  // MediaDescription.addH264Codec(96, "packetization-mode=1;profile-level-id=42e01f");
  MediaDescription.addH264Codec(96);
  Track = PeerConnection->addTrack(MediaDescription);
  RtcpReceivingSession = std::make_shared<rtc::RtcpReceivingSession>();
  Track->setMediaHandler(RtcpReceivingSession);
  Track->onAvailable([]() {});
  Track->onOpen([this]()
    {
      std::cout << "Track opened" << std::endl;
      Track->requestKeyframe();
      FrameRelay->Connect();
    });
  Track->onMessage([this](const rtc::message_variant& Message)
    {
      std::cout << "Track message" << std::endl;
      if (std::holds_alternative<rtc::binary>(Message))
      {
        std::cout << "Track message binary" << std::endl;
        FrameRelay->Send(std::get<rtc::binary>(Message));
      }
    });
}

WebRTCBridge::MediaReceiver::~MediaReceiver()
{
}

void WebRTCBridge::MediaReceiver::PrintCommunicationData()
{
  DataConnector::PrintCommunicationData();
  std::cout << "FrameRelay: " << FrameRelay->GetAddress() << ":" << FrameRelay->GetSocketPort() << std::endl;
  std::cout << "Track (" << Track->mid() << ") has a maximum Message size of " << Track->maxMessageSize() << std::endl;
}

std::vector<uint8_t> WebRTCBridge::MediaReceiver::DecodeFrame(rtc::binary Frame)
{
  // Decode a H264 frame into a vector of bytes
  std::vector<uint8_t> DecodedFrame(2000 * 1000 );

  // We need to add the NALU start code to the frame
  // https://stackoverflow.com/questions/26451094/what-is-the-nalu-start-code

  return DecodedFrame;
}
