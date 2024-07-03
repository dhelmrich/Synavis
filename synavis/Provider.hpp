#pragma once
#ifndef SYNAVIS_PROVIDER_HPP
#define SYNAVIS_PROVIDER_HPP
#include <rtc/rtc.hpp>
#include <json.hpp>
#include "Synavis.hpp"
#include "UnrealReceiver.hpp"
#include "Synavis/export.hpp"

namespace Synavis
{

  class UnrealReceiver;
  class UnrealConnector;

  // this is a bridge class that is used on the UE side
  class SYNAVIS_EXPORT Provider : public Bridge
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
    virtual std::string Prefix() override;
  };
}
#endif
