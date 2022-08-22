#pragma once
#include <json.hpp>
#include <variant>
#include <rtc/rtc.hpp>
#include "WebRTCBridge/export.hpp"

#include "WebRTCBridge.hpp"

namespace WebRTCBridge
{
  class WEBRTCBRIDGE_EXPORT Adapter
  {
    friend class WebRTCBridge;
    using json = nlohmann::json;
    virtual void StartSignalling(std::string IP, int Port,
        bool keepAlive = true,
        bool useAuthentification = false);
    
    // this is a helper function that should not be considered stable or without fault
    virtual std::string GetConnectionString();

    virtual std::string GenerateSDP();
    virtual std::string Offer();
    virtual std::string Answer();
    virtual void OnInformation(json message) = NULL;
    virtual std::string PushSDP(std::string);

    // Data streams to other Bridge
    // Bridge Pointer is also Shared, which means that
    // the Seeker class has to resolve the object destruction of
    // connections, which is intended anyways.
    std::shared_ptr<WebRTCBridge> Bridge;
    std::shared_ptr<BridgeSocket> Upstream;
    std::shared_ptr<BridgeSocket> Downstream;

    // WebRTC Connectivity
    std::optional<std::shared_ptr<rtc::PeerConnection>> ApplicationConnection;
    std::optional<NoBufferThread> TransmissionThread;

  protected:
    rtc::Configuration rtcconfig_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::DataChannel> vdc_;
    json config_;
    unsigned int MessagesReceived{0};
    unsigned int IceCandidatesReceived{0};
    int ID{};
    std::uint64_t Time();

    std::optional<rtc::Description> StartupDescription_;
  };
}