#include "seeker.hpp"
#include "connector.hpp"

AC::Seeker::Seeker()
{
  BridgeThread = std::make_unique<std::thread>(&Seeker::BridgeRun,this);
}

AC::Seeker::~Seeker()
{
  
}

bool AC::Seeker::CheckSignallingActive()
{
  return false;
}

void AC::Seeker::UseConfig(std::string filename)
{
}

bool AC::Seeker::EstablishedConnection(std::string ip)
{
  return false;
}

void AC::Seeker::BridgeSynchronize(std::shared_ptr<AC::Connector> Instigator,
                                   std::variant<std::byte, std::string> Message, bool bFailIfNotResolved)
{

}

void AC::Seeker::BridgeSubmit(std::variant<std::byte, std::string> Message)
{
  
}

void AC::Seeker::BridgeRun()
{
  std::unique_lock<std::mutex> lock(QueueAccess);
  while(true)
  {
    TaskAvaliable.wait(lock, [this]{
            return (CommInstructQueue.size());
        });
    if(CommInstructQueue.size() > 0)
    {
      auto Task = std::move(CommInstructQueue.front());
      lock.unlock();
      Task();

      // locking at the end of the loop is necessary because next
      // top start of this scope requiers there to be a locked lock.
      lock.lock();
    }
  }
}

void AC::Seeker::FindBridge()
{
}

void AC::Seeker::RecoverConnection()
{
}

std::shared_ptr<AC::Connector> AC::Seeker::CreateConnection()
{
  auto Connection = std::make_shared<Connector>();

}

void AC::Seeker::DestroyConnection(std::shared_ptr<Connector> Connector)
{

}

void AC::Seeker::CreateTask(std::function<void(void)> Task)
{
  std::unique_lock<std::mutex> lock(QueueAccess);
  lock.lock();
  CommInstructQueue.push(Task);
  lock.unlock();
}


