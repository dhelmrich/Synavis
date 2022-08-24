#pragma once
#include <rtc/rtc.hpp>
#include <json.hpp>
#include "WebRTCBridge.hpp"
#include "UnrealReceiver.hpp"
#include "WebRTCBridge/export.hpp"

namespace WebRTCBridge
{

  class UnrealReceiver;


  class WEBRTCBRIDGE_EXPORT Provider : Bridge
  {
    using json = nlohmann::json;

    virtual void FindBridge() override;
  };
}