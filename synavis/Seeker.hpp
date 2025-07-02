#pragma once
#ifndef SYNAVIS_SEEKER_HPP
#define SYNAVIS_SEEKER_HPP
#include "rtc/rtc.hpp"
#include <json.hpp>
#include <vector>
#include <fstream>
#include <compare>
#include <functional>
#include <memory>

#include "Synavis.hpp"

#include "Synavis/export.hpp"
#include <span>


namespace Synavis
{
  class Connector;
  class BridgeSocket;

  class SYNAVIS_EXPORT Seeker : public Bridge, std::enable_shared_from_this<Seeker>
  {
  public:
    using json = nlohmann::json;
    Seeker();
    ~Seeker() override;

    // This methods checks whether the SigServ is reachable
    virtual bool CheckSignallingActive() override;
    
    virtual bool EstablishedConnection(bool Shallow = true) override;
    virtual void FindBridge() override;
    virtual void RecoverConnection();


    virtual std::shared_ptr<Connector> CreateConnection();
    virtual void DestroyConnection(std::shared_ptr<Connector> Connector);

    void ConfigureUpstream(Connector* Instigator, const json& Answer);
    void BridgeRun() override;
    void Listen() override;
    
    void OnSignallingMessage(std::string Message) override;
    void OnSignallingData(rtc::binary Message) override;
    uint32_t SignalNewEndpoint() override;
    void RemoteMessage(json Message) override;
    void InitConnection() override;
  }; 
  
}
#endif
