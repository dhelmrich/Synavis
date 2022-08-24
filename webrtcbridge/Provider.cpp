#include "Provider.hpp"


void WebRTCBridge::Provider::FindBridge()
{
  
  std::unique_lock<std::mutex> lock(QueueAccess);
  lock.lock();
  CommInstructQueue.push([this]()
  {
    
  });
  lock.release();
}
