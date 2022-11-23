#pragma once
#ifndef CONNECTOR_HPP
#define CONNECTOR_HPP

#include "rtc/rtc.hpp"
#include <json.hpp>
#include <vector>
#include <fstream>
#include <compare>
#include <functional>
#include <thread>


#include "WebRTCBridge/export.hpp"
#include "Seeker.hpp"
#include "Adapter.hpp"

namespace WebRTCBridge
{
  class BridgeSocket;
  class NoBufferThread;
  class ApplicationTrack;


  /*!
   * @class Connector
   * A class that contains a whole unreal mimick to serve as webrtc peer.
   *
   */
  class WEBRTCBRIDGE_EXPORT Connector : public Adapter
  {
    friend class Seeker;
  public:
    virtual ~Connector();
    Connector(Connector&& other);

    using json = nlohmann::json;
    void StartFrameReception();

    void SetupApplicationConnection();
    void AwaitSignalling();

    virtual void OnRemoteInformation(json message) override;
    void SetReceptionPolicy(EDataReceptionPolicy inPolicy);

    // Data streams in total
    std::vector<StreamVariant> FromApplication;
    std::vector<StreamVariant> ToApplication;


    virtual void OnGatheringStateChange(rtc::PeerConnection::GatheringState inState) override;
    virtual void OnTrack(std::shared_ptr<rtc::Track> inTrack) override;
    virtual void OnLocalDescription(rtc::Description inDescription) override;
    virtual void OnLocalCandidate(rtc::Candidate inCandidate) override;
    virtual void OnDataChannel(std::shared_ptr<rtc::DataChannel> inChannel) override;

  protected:
    Connector();
    EDataReceptionPolicy Policy{ EDataReceptionPolicy::SynchronizedMetadata };
  public:
    void OnChannelPackage(rtc::binary inPackage) override;
    void OnChannelMessage(std::string inMessage) override;
    std::string GetConnectionString() override;
  };

}
#endif
