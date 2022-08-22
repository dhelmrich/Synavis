#pragma once 
#include "rtc/rtc.hpp"
#include <json.hpp>
#include <vector>
#include <fstream>
#include <compare>
#include <functional>


#include "WebRTCBridge/export.hpp"
#include <span>


namespace WebRTCBridge
{
  class Connector;
  class BridgeSocket;

  class ACCESSOR_EXPORT Seeker: std::enable_shared_from_this<Seeker>
  {
  public:
    using json = nlohmann::json;
    Seeker();
    ~Seeker();

    // This methods checks whether the SigServ is reachable
    virtual bool CheckSignallingActive();
    
    virtual bool EstablishedConnection();
    virtual void FindBridge();
    virtual void RecoverConnection();


    virtual std::shared_ptr<Connector> CreateConnection();
    virtual void DestroyConnection(std::shared_ptr<Connector> Connector);

    void ConfigureUpstream(Connector* Instigator, const json& Answer);

    virtual void CreateTask(std::function<void(void)>&& Task);
    virtual void BridgeSynchronize(WebRTCBridge::Connector* Instigator,
                           json Message, bool bFailIfNotResolved = false);
    void BridgeSubmit(WebRTCBridge::Connector* Instigator, std::variant<rtc::binary, std::string> Message) const;
    void BridgeRun();
    void Listen();

    virtual void StartSignalling(std::string IP, int Port, bool keepAlive = true, bool useAuthentification = false);

  protected:

    json Config{
      {
        {"LocalPort", int()},
        {"RemotePort",int()},
        {"LocalAddress",int()},
        {"RemoteAddress",int()}
      }};

    std::unordered_map<int,std::shared_ptr<Connector>> UserByID;
    std::vector<std::shared_ptr<Connector>> Users;
    std::unique_ptr<std::thread> BridgeThread;
    std::mutex QueueAccess;
    std::queue<std::function<void(void)>> CommInstructQueue;
    std::unique_ptr<std::thread> ListenerThread;
    std::mutex CommandAccess;
    std::queue<std::variant<rtc::binary, std::string>> CommandBuffer;
    std::condition_variable CommandAvailable;
    bool bNeedInfo{false};

    // Signalling Server
    std::shared_ptr<rtc::WebSocket> SignallingConnection;



    struct
    {
      std::shared_ptr<BridgeSocket> In;
      std::shared_ptr<BridgeSocket> Out;
    } BridgeConnection;
    
    std::condition_variable TaskAvaliable;

    int NextID{0};
  }; 
  
}
