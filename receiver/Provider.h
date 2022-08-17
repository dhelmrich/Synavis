#include <rtc/rtc.hpp>
#include <json.hpp>


#include "UnrealReceiver/export.hpp"

namespace UR
{

  class UnrealReceiver;
  class UNREALRECEIVER_EXPORT Provider
  {
    using json = nlohmann::json;
    virtual void ConnectToSignalling(std::string IP, int Port, bool keepAlive = true, bool useAuthentification = false);
    virtual void CreateTask(std::function<void(void)>&& Task);
    virtual void BridgeSynchronize(UR::UnrealReceiver* Instigator,
                           json Message, bool bFailIfNotResolved = false);
    void BridgeSubmit(UR::UnrealReceiver* Instigator, std::variant<rtc::binary, std::string> Message) const;
    void BridgeRun();
    void Listen();
  protected:
    json Config{
      {
        {"LocalPort", int()},
        {"RemotePort",int()},
        {"LocalAddress",int()},
        {"RemoteAddress",int()}
      }};
    std::unordered_map<int,std::shared_ptr<UnrealReceiver>> AppByID;
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
    
    
    std::condition_variable TaskAvaliable;

    int NextID{0};
  };
}