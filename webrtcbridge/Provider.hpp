#pragma once
#include <rtc/rtc.hpp>
#include <json.hpp>
#include "WebRTCBridge.hpp"
#include "UnrealReceiver.hpp"
#include "WebRTCBridge/export.hpp"

namespace WebRTCBridge
{

  class UnrealReceiver;
  class UnrealConnector;

  class WEBRTCBRIDGE_EXPORT Provider : public Bridge
  {
    using json = nlohmann::json;

    virtual void FindBridge() override;

    std::shared_ptr<UnrealConnector> CreateConnection();


  public:
    void OnSignallingMessage(std::string Message) override;
    void OnSignallingData(rtc::binary Message) override;
    uint32_t SignalNewEndpoint() override;
    void RemoteMessage(json Message) override;
  };
}