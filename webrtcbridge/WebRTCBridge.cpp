#include "WebRTCBridge.hpp"
#include "Adapter.hpp"

#include <fstream>

int WebRTCBridge::BridgeSocket::Receive(bool invalidIsFailure)
{
#ifdef _WIN32
  return 0;
#elif __linux__
  return 0;
#endif
}

WebRTCBridge::ApplicationTrack::ApplicationTrack(std::shared_ptr<rtc::Track> inTrack)
      : Track(inTrack) {}
      
void WebRTCBridge::ApplicationTrack::Send(std::byte* Data, unsigned Length)
{
  auto rtp = reinterpret_cast<rtc::RtpHeader*>(Data);
  rtp->setSsrc(SSRC);
  Track->send(Data, Length);
}

void WebRTCBridge::ApplicationTrack::ConfigureOutput(std::shared_ptr<rtc::RtcpSrReporter> inReporter)
{
}

void WebRTCBridge::ApplicationTrack::ConfigureIn()
{
}

bool WebRTCBridge::ApplicationTrack::Open()
{
  return Track->isOpen();
}

WebRTCBridge::NoBufferThread::NoBufferThread(std::weak_ptr<ApplicationTrack> inDataDestination,
                                   std::weak_ptr<BridgeSocket> inDataSource)
    : DataDestination(inDataDestination), DataSource(inDataSource)
{
  Thread = std::make_unique<std::thread>(&WebRTCBridge::NoBufferThread::Run,this);
}

void WebRTCBridge::NoBufferThread::Run()
{
  // Consume buffer until close (this should never be empty but we never know)
  auto DataDestinationPtr = DataDestination.lock();
  auto DataSourcePtr = DataSource.lock();
  int Length;
  while((Length = DataSourcePtr->Receive()) > 0)
  {
    if (Length < sizeof(rtc::RtpHeader) || !DataDestinationPtr->Open())
      continue;
    // This is a roundabout reinterpret_cast without having to actually do one
    DataDestinationPtr->Send(DataSourcePtr->BinaryData.data(),Length);
  }
}

WebRTCBridge::Bridge::Bridge()
{
  BridgeThread = std::async(std::launch::async, &Bridge::BridgeRun,this);

  ListenerThread = std::async(std::launch::async,&Bridge::Listen, this);
}

WebRTCBridge::Bridge::~Bridge()
{
}

void WebRTCBridge::Bridge::BridgeSynchronize(Adapter* Instigator, nlohmann::json Message, bool bFailIfNotResolved)
{
  Message["id"] = Instigator->ID;
  std::string Transmission = Message.dump();
  BridgeConnection.Out->Send(Transmission);
  auto messagelength = BridgeConnection.In->Receive(true);
  if (messagelength <= 0)
  {
    if (bFailIfNotResolved)
    {
      throw std::domain_error(std::string("Could not receive answer from Bridgehead and this synchronization is critical:\n\n")
        + "Message was:\n\n"
        + Message.dump(1, '\t'));
    }
  }
  else
  {
    json Answer;
    try
    {
      Answer = json::parse(BridgeConnection.In->StringData);
    }
    catch (std::exception e)
    {
      if (bFailIfNotResolved)
      {
        throw std::runtime_error(std::string("An error occured while parsing the Bridge response:\n\n")
          + e.what());
      }
    }
    catch (...)
    {
      if (bFailIfNotResolved)
      {
        throw std::exception("An unexpected error occured while parsing the Bridge response");
      }
    }
    Instigator->OnInformation(Answer);
  }
}

void WebRTCBridge::Bridge::CreateTask(std::function<void()>&& Task)
{
  std::unique_lock<std::mutex> lock(QueueAccess);
  lock.lock();
  CommInstructQueue.push(Task);
  lock.unlock();
}

void WebRTCBridge::Bridge::BridgeSubmit(Adapter* Instigator, std::variant<rtc::binary, std::string> Message) const
{
}

void WebRTCBridge::Bridge::BridgeRun()
{
}

void WebRTCBridge::Bridge::Listen()
{
  std::unique_lock<std::mutex> lock(CommandAccess);
  while (true)
  {
    CommandAvailable.wait(lock, [this]
      {
        return bNeedInfo && this->BridgeConnection.In->Peek() > 0;
      });
    bool isMessage = false;
    try
    {
      // all of these things must be available and also present
      // on the same layer of the json signal
      auto message = json::parse(this->BridgeConnection.In->Reception);
      std::string type = message["type"];
      auto app_id = message["id"].get<int>();
      EndpointById[app_id]->OnInformation(message);
    }
    catch (...)
    {

    }
  }
}

bool WebRTCBridge::Bridge::CheckSignallingActive()
{
  return SignallingConnection->isOpen();
}

bool WebRTCBridge::Bridge::EstablishedConnection()
{
  using namespace std::chrono_literals;
  auto status_bridge_thread = BridgeThread.wait_for(0ms);
  auto status_command_thread = ListenerThread.wait_for(0ms);
  return (BridgeConnection.In->Valid && BridgeConnection.Out->Valid
         && status_bridge_thread != std::future_status::ready
         && status_command_thread != std::future_status::ready);
}

void WebRTCBridge::Bridge::FindBridge()
{
}

void WebRTCBridge::Bridge::StartSignalling(std::string IP, int Port, bool keepAlive, bool useAuthentification)
{
  SignallingConnection = std::make_shared<rtc::WebSocket>();
  std::promise<void> RunGuard;
  auto Notifier = RunGuard.get_future();
  SignallingConnection->onOpen([this, &RunGuard]()
  {
    RunGuard.set_value();
  });
  SignallingConnection->onClosed([this, &RunGuard](){});
  SignallingConnection->onError([this, &RunGuard](auto error){});
  SignallingConnection->onMessage([this](auto message)
  {
    if(std::holds_alternative<std::string>(message))
    {
      OnSignallingMessage(std::get<std::string>(message));
    }
    else
    {
      OnSignallingData(std::get<rtc::binary>(message));
    }
  });
  if(useAuthentification)
  {
    // this is its own issue as we are probably obliged to conform to IDM guidelines here
    // these should all interface the same way, but here we should probably
    // call upon the jupyter-jsc service that should run in the background somewhere
  }
  else
  {
    SignallingConnection->open(IP + std::to_string(Port));
  }
  std::cout << "Waiting for Signalling Websocket to Connect." << std::endl;
  Notifier.wait();
}

void WebRTCBridge::Bridge::UseConfig(std::string filename)
{
  std::ifstream file(filename);
  auto fileConfig = json::parse(file);
  bool complete = true;
  for(auto key : Config)
    if(fileConfig.find(key) == fileConfig.end())
      complete = false;
}
