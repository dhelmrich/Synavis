#include "Provider.hpp"

void WebRTCBridge::Provider::ConnectToSignalling(std::string IP, int Port, bool keepAlive, bool useAuthentification)
{
}

void WebRTCBridge::Provider::CreateTask(std::function<void()>&& Task)
{
}

void WebRTCBridge::Provider::BridgeSynchronize(WebRTCBridge::UnrealReceiver* Instigator, json Message, bool bFailIfNotResolved)
{
}

void WebRTCBridge::Provider::BridgeSubmit(WebRTCBridge::UnrealReceiver* Instigator, std::variant<rtc::binary, std::string> Message) const
{
}

void WebRTCBridge::Provider::BridgeRun()
{
}

void WebRTCBridge::Provider::Listen()
{
}

void WebRTCBridge::Provider::FindBridge()
{
  
  std::unique_lock<std::mutex> lock(QueueAccess);
  lock.lock();
  CommInstructQueue.push([this]()
  {
    
  });
  lock.release();
}
