#pragma once
#ifndef WEBRTC_SEEKER_HPP
#define WEBRTC_SEEKER_HPP
#include "rtc/rtc.hpp"
#include <json.hpp>
#include <vector>
#include <fstream>
#include <compare>
#include <functional>
#include <memory>

#include "WebRTCBridge.hpp"

#include "WebRTCBridge/export.hpp"
#include <span>


namespace WebRTCBridge
{
  class Connector;
  class BridgeSocket;

  class WEBRTCBRIDGE_EXPORT Seeker : public Bridge, std::enable_shared_from_this<Seeker>
  {
  public:
    using json = nlohmann::json;
    Seeker();
    ~Seeker() override;

    // This methods checks whether the SigServ is reachable
    virtual bool CheckSignallingActive();
    
    virtual bool EstablishedConnection(bool Shallow = false) override;
    virtual void FindBridge() override;
    virtual void RecoverConnection();


    virtual std::shared_ptr<Connector> CreateConnection();
    virtual void DestroyConnection(std::shared_ptr<Connector> Connector);

    void ConfigureUpstream(Connector* Instigator, const json& Answer);
    void BridgeRun();
    void Listen();
    
    void OnSignallingMessage(std::string Message) override;
    void OnSignallingData(rtc::binary Message) override;
    uint32_t SignalNewEndpoint() override;
    void RemoteMessage(json Message) override;
  }; 
  
}
#endif
