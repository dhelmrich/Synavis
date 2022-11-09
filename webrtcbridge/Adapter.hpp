#pragma once
#ifndef WEBRTC_ADAPTER_HPP
#define WEBRTC_ADAPTER_HPP

#include <json.hpp>
#include <variant>
#include <rtc/rtc.hpp>
#include "WebRTCBridge/export.hpp"

#include "WebRTCBridge.hpp"

namespace WebRTCBridge
{
    template<typename... Ts>
  __forceinline std::vector<std::byte> literalbytes(Ts&&... args) noexcept {
      return{std::byte(std::forward<Ts>(args))...};
  }

  // This is a general class that wraps the actual webrtc endpoint communication
  class WEBRTCBRIDGE_EXPORT Adapter
  {
  public:
    virtual ~Adapter() = default;
    friend class Bridge;
    using json = nlohmann::json;
    void SetupWebRTC();
    void CheckBridgeExtention(const std::string& SDP);
    
    // this is a helper function that should not be considered stable or without fault
    virtual std::string GetConnectionString() = NULL;
    virtual std::string GenerateSDP();
    virtual std::string Offer();
    virtual std::string Answer();
    virtual std::string PushSDP(std::string);
    rtc::PeerConnection* GetPeerConnection();
    void SetID(Bridge* Instigator, uint32_t ID);
    virtual void OnGatheringStateChange(rtc::PeerConnection::GatheringState inState) = NULL;
    virtual void OnTrack(std::shared_ptr<rtc::Track> inTrack) = NULL;
    virtual void OnLocalDescription(rtc::Description inDescription) = NULL;
    virtual void OnLocalCandidate(rtc::Candidate inCandidate) = NULL;
    virtual void OnDataChannel(std::shared_ptr<rtc::DataChannel> inChannel) = 0;
    virtual void OnRemoteInformation(json message) = NULL;
    virtual void OnChannelPackage(rtc::binary inPackage) = NULL;
    virtual void OnChannelMessage(std::string inMessage) = NULL;

    // Data streams to other Bridge
    // Bridge Pointer is also Shared, which means that
    // the Seeker class has to resolve the object destruction of
    // connections, which is intended anyways.
    std::shared_ptr<Bridge> Bridge;

  protected:
    rtc::Configuration rtcconfig_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    json config_;
    unsigned int MessagesReceived{0};
    unsigned int IceCandidatesReceived{0};
    unsigned int ExtMapOffset{ 0 };
    int ID{};

    json generated_offer_;
    json generated_answer_;

    std::optional<rtc::Description> StartupDescription_;
  };
}

#endif
