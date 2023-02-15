#pragma once
#ifndef WEBRTC_PROVIDER_HPP
#define WEBRTC_PROVIDER_HPP
#include <rtc/rtc.hpp>
#include <json.hpp>
#include "WebRTCBridge.hpp"
#include "UnrealReceiver.hpp"
#include "WebRTCBridge/export.hpp"

namespace WebRTCBridge
{

  class UnrealReceiver;
  class UnrealConnector;

  // this is a bridge class that is used on the UE side
  class WEBRTCBRIDGE_EXPORT Provider : public Bridge
  {
  public:
    using json = nlohmann::json;

    virtual void FindBridge() override;

    std::shared_ptr<UnrealConnector> CreateConnection();
    
    
    void OnSignallingMessage(std::string Message) override;
    void OnSignallingData(rtc::binary Message) override;
    uint32_t SignalNewEndpoint() override;
    void RemoteMessage(json Message) override;
    bool EstablishedConnection(bool Shallow = true) override;
    void InitConnection() override;
    virtual std::string Prefix();
  };
}
#endif
