#pragma once

#include "rtc/rtc.hpp"
#include <json.hpp>
#include <vector>
#include <fstream>
#include <compare>
#include <functional>
#include <thread>


#include "WebRTCBridge/export.hpp"
#include "seeker.hpp"
#include "Adapter.hpp"

namespace WebRTCBridge
{
  struct BridgeSocket;
  class NoBufferThread;
  class ApplicationTrack;
  /*!
   * @class Connector
   * A class to connect the application-side to the Unreal server-side bridge
   * This class is intended to be used inside an Apptainer/Container environment
   * Previoud knowledge about IP environment needed
   *
   */
  class WEBRTCBRIDGE_EXPORT Connector : public Adapter
  {
    friend class Seeker;
  public:
    virtual ~Connector();
    Connector(Connector&& other) = default;

    using json = nlohmann::json;
    void StartFrameReception();

    void SetupApplicationConnection();
    void AwaitSignalling();

    virtual void OnInformation(json message) override;


    // Data streams to other Bridge
    // Bridge Pointer is also Shared, which means that
    // the Seeker class has to resolve the object destruction of
    // connections, which is intended anyways.
    std::shared_ptr<class Seeker> Bridge;
    std::shared_ptr<BridgeSocket> Upstream;
    std::shared_ptr<BridgeSocket> Downstream;

    // WebRTC Connectivity
    std::optional<std::shared_ptr<rtc::PeerConnection>> ApplicationConnection;
    std::optional<NoBufferThread> TransmissionThread;

    // Data streams to Application
    std::shared_ptr<ApplicationTrack> VideoToApplication;
    std::shared_ptr<ApplicationTrack> AudioToApplication;
    std::shared_ptr<rtc::DataChannel> DataToApplication;
    std::shared_ptr<rtc::DataChannel> DataFromApplication;


    virtual void OnGatheringStateChange(rtc::PeerConnection::GatheringState inState) override;
    virtual void OnTrack(std::shared_ptr<rtc::Track> inTrack) override;
    virtual void OnLocalDescription(rtc::Description inDescription) override;
    virtual void OnLocalCandidate(rtc::Candidate inCandidate) override;
    virtual void OnDataChannel(std::shared_ptr<rtc::DataChannel> inChannel) override;

  protected:
    Connector();
  public:
    void OnPackage(rtc::binary inPackage) override;
    void OnChannelMessage(std::string inMessage) override;
  private:
    rtc::Configuration rtcconfig_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::DataChannel> vdc_;
    json config_;
    unsigned int MessagesReceived{0};
    unsigned int IceCandidatesReceived{0};
    int ID{};

    std::optional<rtc::Description> Offer_;
  };

}