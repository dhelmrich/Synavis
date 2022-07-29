#include "seeker.hpp"
#include "connector.hpp"

AC::Seeker::Seeker()
{
  BridgeThread = std::make_unique<std::thread>(&Seeker::BridgeRun,this);
  ListenerThread = std::make_unique<std::thread>(&Seeker::Listen, this);
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

void AC::Seeker::BridgeSynchronize(AC::Connector* Instigator,
                                   std::variant<std::byte, std::string> Message, bool bFailIfNotResolved)
{

}

void AC::Seeker::BridgeSubmit(AC::Connector* Instigator, std::variant<std::byte, std::string> Message)
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

void AC::Seeker::Listen()
{

}

void AC::Seeker::FindBridge()
{
  std::unique_lock<std::mutex> lock(QueueAccess);
  lock.lock();

  lock.release();
}

void AC::Seeker::RecoverConnection()
{
}

std::shared_ptr<AC::Connector> AC::Seeker::CreateConnection()
{
  // structural wrapper to forego the need to create a fractured shared pointer
  struct Wrap { Wrap() :cont(AC::Connector()) {} AC::Connector cont; };
  auto t = std::make_shared<Wrap>();
  std::shared_ptr<AC::Connector> Connection{std::move(t),&t->cont };



  return Connection;

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


