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
}

void WebRTCBridge::UnrealConnector::OnInformation(json message)
{
}

void WebRTCBridge::UnrealConnector::OnPackage(rtc::binary inPackage)
{
  rtc::RtpHeader* header = reinterpret_cast<rtc::RtpHeader*>(inPackage.data());
  
}

void WebRTCBridge::UnrealConnector::OnChannelMessage(std::string inMessage)
{
}
