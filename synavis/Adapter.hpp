#pragma once
#ifndef SYNAVIS_ADAPTER_HPP
#define SYNAVIS_ADAPTER_HPP

#include <json.hpp>
#include <variant>
#include <rtc/rtc.hpp>
#include "Synavis/export.hpp"

#include "Synavis.hpp"

#ifndef __forceinline
#define __forceinline inline
#endif

namespace Synavis
{
    template<typename... Ts>
  __forceinline std::vector<std::byte> literalbytes(Ts&&... args) noexcept {
      return{std::byte(std::forward<Ts>(args))...};
  }

  // This is a general class that wraps the actual webrtc endpoint communication
  class SYNAVIS_EXPORT Adapter
  {
  public:
    virtual ~Adapter() = default;

    friend class Bridge;
    using json = nlohmann::json;
    void SetupWebRTC();
    void CheckBridgeExtention(const std::string& SDP);
    
    // this is a helper function that should not be considered stable or without fault
    virtual std::string GetConnectionString() = 0;
    virtual std::string GenerateSDP();
    virtual std::string Offer();
    virtual std::string Answer();
    virtual std::string PushSDP(std::string);
    rtc::PeerConnection* GetPeerConnection();
    void SetID(Bridge* Instigator, uint32_t ID);
    virtual void OnGatheringStateChange(rtc::PeerConnection::GatheringState inState) = 0;
    virtual void OnTrack(std::shared_ptr<rtc::Track> inTrack) = 0;
    virtual void OnLocalDescription(rtc::Description inDescription) = 0;
    virtual void OnLocalCandidate(rtc::Candidate inCandidate) = 0;
    virtual void OnDataChannel(std::shared_ptr<rtc::DataChannel> inChannel) = 0;
    virtual void OnRemoteInformation(json message) = 0;
    virtual void OnChannelPackage(rtc::binary inPackage) = 0;
    virtual void OnChannelMessage(std::string inMessage) = 0;

    // Data streams to other Bridge
    // Bridge Pointer is also Shared, which means that
    // the Seeker class has to resolve the object destruction of
    // connections, which is intended anyways.
    std::shared_ptr<Bridge> OwningBridge;

  protected:
    rtc::Configuration rtcconfig_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    json config_;
    unsigned int MessagesReceived{0};
    unsigned int IceCandidatesReceived{0};
    unsigned int ExtMapOffset{ 0 };
    int ID{};
    Adapter() = default;
    json generated_offer_;
    json generated_answer_;

    std::optional<rtc::Description> StartupDescription_;
  };
}

#endif
