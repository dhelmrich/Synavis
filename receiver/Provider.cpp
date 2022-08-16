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
  };
}