#include "UnrealConnector.hpp"

void WebRTCBridge::UnrealConnector::OnGatheringStateChange(rtc::PeerConnection::GatheringState inState)
{
}

void WebRTCBridge::UnrealConnector::OnTrack(std::shared_ptr<rtc::Track> inTrack)
{
  
}

void WebRTCBridge::UnrealConnector::OnLocalDescription(rtc::Description inDescription)
{

}

void WebRTCBridge::UnrealConnector::OnLocalCandidate(rtc::Candidate inCandidate)
{

}

void WebRTCBridge::UnrealConnector::OnDataChannel(std::shared_ptr<rtc::DataChannel> inChannel)
{
  UnrealData = inChannel;
}

void WebRTCBridge::UnrealConnector::OnRemoteInformation(json message)
{
  if(message["type"] == "offer")
  {
    std::string sdp = message["sdp"];
    rtc::Description desc(sdp);

    for (unsigned i = 0; i < desc.mediaCount(); ++i)
    {
      auto medium = desc.media(i);
      
      if (std::holds_alternative<rtc::Description::Media*>(medium))
      {
        auto* metadata = std::get<rtc::Description::Media*>(medium);
         
      }
      else
      {
        auto app = std::get<rtc::Description::Application*>(medium);

      }
    }
  }
  else if(message["type"] == "answer")
  {
    // this would be a response to the unreal offer
  }
}

void WebRTCBridge::UnrealConnector::OnChannelPackage(rtc::binary inPackage)
{

}

void WebRTCBridge::UnrealConnector::OnChannelMessage(std::string inMessage)
{

}

void WebRTCBridge::UnrealConnector::StartFrameReception()
{
  if(ConnectionType == EBridgeConnectionType::DirectMode)
  {
    // this might try to find the FFMPEG installation if accessible through the path variable
  }
}

void WebRTCBridge::UnrealConnector::OnDecoderStreamData(std::vector<uint32_t> ImageData)
{
}

void WebRTCBridge::UnrealConnector::SignalKey()
{
}

std::string WebRTCBridge::UnrealConnector::GetConnectionString()
{
  return this->generated_offer_.dump();
}
