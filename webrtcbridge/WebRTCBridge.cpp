#include "WebRTCBridge.hpp"
#include "Adapter.hpp"

#include <fstream>
#include <span>

int WebRTCBridge::BridgeSocket::Receive(bool invalidIsFailure)
{
#ifdef _WIN32
  return 0;
#elif __linux__
  return 0;
#endif
}

WebRTCBridge::NoBufferThread::NoBufferThread(std::shared_ptr<BridgeSocket> inDataSource)
    : SocketConnection(inDataSource)
{
  Thread = std::async(&WebRTCBridge::NoBufferThread::Run,this);
}

std::size_t WebRTCBridge::NoBufferThread::AddRTC(StreamVariant inRTC)
{

  return std::size_t();
}

std::size_t WebRTCBridge::NoBufferThread::AddRTC(StreamVariant&& inRTC)
{

  return std::size_t();
}

void WebRTCBridge::NoBufferThread::Run()
{
  // Consume buffer until close (this should never be empty but we never know)
  
  int Length;
  while ((Length = SocketConnection->Receive()) > 0)
  {
    if (Length < sizeof(rtc::RtpHeader))
      continue;
    if (ConnectionMode == EBridgeConnectionType::LockedMode)
    {
      // we gather all packages and submit them together, we will also discard packages
      // that are out of order
    }
    else
    {
      // TOOD if this check fails we might run into \0 at the end of the string.

      const auto& destination = WebRTCTracks[SocketConnection->NumberData[0]];
      const auto& byte_data = SocketConnection->BinaryData;

      rtc::RtpHeader* head = reinterpret_cast<rtc::RtpHeader*>(byte_data.data());
      auto target = head->getExtensionHeader() + this->RtpDestinationHeader;


      if(std::holds_alternative<std::shared_ptr<rtc::Track>>(destination))
      {
        std::get<std::shared_ptr<rtc::Track>>(destination)->send(byte_data.data(), byte_data.size());
      }
      else
      {
        std::get< std::shared_ptr<rtc::DataChannel> >(destination)->send(byte_data.data(), byte_data.size());
      }
    }
    // This is a roundabout reinterpret_cast without having to actually do one
    //DataDestinationPtr->Send(SocketConnection->BinaryData.data(),Length);
  }
}

WebRTCBridge::Bridge::Bridge()
{
  std::cout << "An instance of the WebRTCBridge was started, we are starting the threads..." << std::endl;
  BridgeThread = std::async(std::launch::async, &Bridge::BridgeRun,this);
  std::cout << "Bridge Thread started" << std::endl;
  ListenerThread = std::async(std::launch::async,&Bridge::Listen, this);
  std::cout << "Listener Thread Started" << std::endl;

  BridgeConnection.In = std::make_shared<BridgeSocket>();
  BridgeConnection.Out = std::make_shared<BridgeSocket>();
  BridgeConnection.DataOut = std::make_shared<BridgeSocket>();
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
    if(Answer["type"] == "ok")
    {
      
    }
    else if(Answer["type"] == "todo")
    {
      Instigator->OnRemoteInformation(Answer);
    }
  }
}

void WebRTCBridge::Bridge::CreateTask(std::function<void()>&& Task)
{
  std::unique_lock<std::mutex> lock(QueueAccess);
  lock.lock();
  CommInstructQueue.push(Task);
  lock.unlock();
}

void WebRTCBridge::Bridge::BridgeSubmit(Adapter* Instigator, StreamVariant origin, std::variant<rtc::binary, std::string> Message) const
{
  // we need to break this up because of json lib compatibility
  if (std::holds_alternative<std::string>(Message))
  {
    json Transmission = { {"id",Instigator->ID} };
    Transmission["data"] = std::get<std::string>(Message);
    BridgeConnection.DataOut->Send(Transmission);
  }
  else
  {

    auto data = std::get<rtc::binary>(Message);
    if(data.size() > RtpDestinationHeader + 13)
    {
      auto* p_proxy = reinterpret_cast<uint16_t*>((data.data() + RtpDestinationHeader));
      *p_proxy = Instigator->ID;
    }
    BridgeConnection.DataOut->Send(data);
  }
}

void WebRTCBridge::Bridge::InitConnection()
{
  if(Config["RemoteAddress"].is_number())
  {
    
  }
  else
  {
    BridgeConnection.In->Address = Config["RemoteAddress"];
  }
  BridgeConnection.In->Port = Config["RemotePort"];
}

void WebRTCBridge::Bridge::SetHeaderByteStart(uint32_t Byte)
{
  this->DataInThread->RtpDestinationHeader = Byte;
}

void WebRTCBridge::Bridge::BridgeRun()
{
  std::unique_lock<std::mutex> lock(QueueAccess);
  while (true)
  {
    TaskAvaliable.wait(lock, [this] {
      return (CommInstructQueue.size());
      });
    if (CommInstructQueue.size() > 0)
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
      auto message = json::parse(this->BridgeConnection.In->StringData);
      std::string type = message["type"];
      auto app_id = message["id"].get<int>();
      
      EndpointById[app_id]->OnRemoteInformation(message);
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

bool WebRTCBridge::Bridge::EstablishedConnection(bool Shallow)
{
  using namespace std::chrono_literals;
  auto status_bridge_thread = BridgeThread.wait_for(0ms);
  auto status_command_thread = ListenerThread.wait_for(0ms);
  if(!Shallow)
  {
    // check connection validity here and actually ping connected bridges
  }
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

void WebRTCBridge::Bridge::ConfigureTrackOutput(std::shared_ptr<rtc::Track> OutputStream, rtc::Description::Media* Media)
{
}

void WebRTCBridge::Bridge::SubmitToSignalling(json Message, Adapter* Endpoint)
{
  if(SignallingConnection->isOpen())
  {
    Message["ID"] = Endpoint->ID;
    SignallingConnection->send(Message);
  }
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