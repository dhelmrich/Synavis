#pragma once

#include "Adapter.hpp"
#include "Provider.hpp"

namespace Synavis
{


  /*!
   * class UnrealConnector
   * A class that contains a whole application mimick to serve as webrtc peer.
   */
  class SYNAVIS_EXPORT UnrealConnector : public Adapter
  {
  public:
    friend class Provider;
    void OnGatheringStateChange(rtc::PeerConnection::GatheringState inState) override;
    void OnTrack(std::shared_ptr<rtc::Track> inTrack) override;
    void OnLocalDescription(rtc::Description inDescription) override;
    void OnLocalCandidate(rtc::Candidate inCandidate) override;
    void OnDataChannel(std::shared_ptr<rtc::DataChannel> inChannel) override;
    void OnRemoteInformation(json message) override;
    void OnChannelPackage(rtc::binary inPackage) override;
    void OnChannelMessage(std::string inMessage) override;
    
    void StartFrameReception();
    virtual void OnDecoderStreamData(std::vector<uint32_t> ImageData);
    void SignalKey();
    std::string GetConnectionString() override;

    std::shared_ptr<rtc::Track> VideoFromUnreal;
    std::shared_ptr<rtc::Track> AudioFromUnreal;
    std::shared_ptr<rtc::DataChannel> UnrealData;

  protected:
    EBridgeConnectionType ConnectionType { EBridgeConnectionType::BridgeMode };
    
  };
}
