#pragma once

#include "Adapter.hpp"

namespace WebRTCBridge
{
  class UnrealConnector : public Adapter
  {
  public:
    void OnGatheringStateChange(rtc::PeerConnection::GatheringState inState) override;
    void OnTrack(std::shared_ptr<rtc::Track> inTrack) override;
    void OnLocalDescription(rtc::Description inDescription) override;
    void OnLocalCandidate(rtc::Candidate inCandidate) override;
    void OnDataChannel(std::shared_ptr<rtc::DataChannel> inChannel) override;
    void OnInformation(json message) override;
    void OnPackage(rtc::binary inPackage) override;
    void OnChannelMessage(std::string inMessage) override;
  };
}
