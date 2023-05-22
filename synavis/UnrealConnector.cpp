#include "UnrealConnector.hpp"

void Synavis::UnrealConnector::OnGatheringStateChange(rtc::PeerConnection::GatheringState inState)
{
}

void Synavis::UnrealConnector::OnTrack(std::shared_ptr<rtc::Track> inTrack)
{
  
}

void Synavis::UnrealConnector::OnLocalDescription(rtc::Description inDescription)
{

}

void Synavis::UnrealConnector::OnLocalCandidate(rtc::Candidate inCandidate)
{

}

void Synavis::UnrealConnector::OnDataChannel(std::shared_ptr<rtc::DataChannel> inChannel)
{
  UnrealData = inChannel;
}

void Synavis::UnrealConnector::OnRemoteInformation(json message)
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

void Synavis::UnrealConnector::OnChannelPackage(rtc::binary inPackage)
{

}

void Synavis::UnrealConnector::OnChannelMessage(std::string inMessage)
{

}

void Synavis::UnrealConnector::StartFrameReception()
{
  if(ConnectionType == EBridgeConnectionType::DirectMode)
  {
    // this might try to find the FFMPEG installation if accessible through the path variable
  }
}

void Synavis::UnrealConnector::OnDecoderStreamData(std::vector<uint32_t> ImageData)
{
}

void Synavis::UnrealConnector::SignalKey()
{
}

std::string Synavis::UnrealConnector::GetConnectionString()
{
  return this->generated_offer_.dump();
}
