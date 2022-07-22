#include "connector.hpp"


AC::NoBufferThread::NoBufferThread(std::weak_ptr<ApplicationTrack> inDataDestination,
  std::weak_ptr<BridgeSocket> inDataSource)
    : DataDestination(inDataDestination), DataSource(inDataSource)
{
  Thread = std::make_unique<std::thread>(&AC::NoBufferThread::Run,this);
}

void AC::NoBufferThread::Run()
{
  
}

void AC::Connector::SetupApplicationConnection()
{

}
