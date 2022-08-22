#include "seeker.hpp"

#include <variant>

#include "connector.hpp"

int WebRTCBridge::BridgeSocket::Peek()
{
return -1;
}

void WebRTCBridge::BridgeSocket::Send(std::variant<rtc::binary, std::string> message)
{
  if(Outgoing)
  {
    const char* buffer;
    int length;
    if(std::holds_alternative<std::string>(message))
    {
      buffer = std::get<std::string>(message).c_str();
      length = (int)std::get<std::string>(message).length();
    }
    else if (std::holds_alternative<rtc::binary>(message))
    {
      buffer = reinterpret_cast<const char*>(std::get<rtc::binary>(message).data());
      length = (int)std::get<rtc::binary>(message).size();
    }
    if(send(Sock,buffer,length,0) == SOCKET_ERROR)
    {
      
    }
  }
}

WebRTCBridge::Seeker::Seeker()
{
  BridgeThread = std::make_unique<std::thread>(&Seeker::BridgeRun,this);
  ListenerThread = std::make_unique<std::thread>(&Seeker::Listen, this);
}

WebRTCBridge::Seeker::~Seeker()
{
  
}

bool WebRTCBridge::Seeker::CheckSignallingActive()
{
  return false;
}



bool WebRTCBridge::Seeker::EstablishedConnection()
{
  BridgeConnection.In = std::make_shared<BridgeSocket>();
  BridgeConnection.Out = std::make_shared<BridgeSocket>();
  BridgeConnection.Out->Address = Config["LocalAddress"];
  BridgeConnection.Out->Port = Config["LocalPort"];
  BridgeConnection.In->Address = Config["RemoteAddress"];
  BridgeConnection.In->Port = Config["RemotePort"];
  if(BridgeConnection.Out->Connect() && BridgeConnection.In->Connect())
  {
    std::unique_lock<std::mutex> lock(QueueAccess);
    lock.lock();
    int PingPongSuccessful = -1;
    CommInstructQueue.push([this,&PingPongSuccessful]
    {
      BridgeConnection.Out->Send(json({{"ping",int()}}).dump());
      int reception{0};
      while((reception = BridgeConnection.In->Peek()) == 0)
      {
        std::this_thread::yield();
      }
      try
      {
        if(json::parse(BridgeConnection.In->StringData)["ping"] == 1)
        {
          PingPongSuccessful = 1;
        }
      }catch(...)
      {
        PingPongSuccessful = 0;
      }
    });
    lock.release();
    while(PingPongSuccessful == -1) std::this_thread::yield();
    return PingPongSuccessful == 1;
  }
  else
  {
    return false;
  }
}

void WebRTCBridge::Seeker::BridgeSynchronize(WebRTCBridge::Connector* Instigator,
                                   json Message, bool bFailIfNotResolved)
{
  Message["id"] = Instigator->ID;
  std::string Transmission = Message.dump();
  BridgeConnection.Out->Send(Transmission);
  auto messagelength = BridgeConnection.In->Receive(true);
  if(messagelength <= 0)
  {
    if(bFailIfNotResolved)
    {
      throw std::domain_error(std::string("Could not receive answer from Bridgehead and this synchronization is critical:\n\n")
      + "Message was:\n\n"
      + Message.dump(1,'\t'));
    }
  }
  else
  {
    json Answer;
    try
    {
      Answer = json::parse(BridgeConnection.In->StringData);
    }
    catch(std::exception e)
    {
      if(bFailIfNotResolved)
      {
        throw std::runtime_error(std::string("An error occured while parsing the Bridge response:\n\n")
        + e.what());
      }
    }
    catch(...)
    {
      if(bFailIfNotResolved)
      {
        throw std::exception("An unexpected error occured while parsing the Bridge response");
      }
    }
    Instigator->OnInformation(Answer);
  }
}

void WebRTCBridge::Seeker::BridgeSubmit(WebRTCBridge::Connector* Instigator, std::variant<rtc::binary, std::string> Message) const
{
  json Transmission = {{"id",Instigator->ID}};
  // we need to break this up because of json lib compatibility
  if(std::holds_alternative<std::string>(Message))
  {
    Transmission["data"] = std::get<std::string>(Message);
  }
  else
  {
    std::string CopyData(reinterpret_cast<const char*>(std::get<rtc::binary>(Message).data()),std::get<rtc::binary>(Message).size());
    Transmission["data"] = CopyData;
  }
  BridgeConnection.Out->Send(Transmission);
}

void WebRTCBridge::Seeker::BridgeRun()
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

void WebRTCBridge::Seeker::Listen()
{
  std::unique_lock<std::mutex> lock(CommandAccess);
  while(true)
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
      UserByID[app_id]->OnInformation(message);
    } catch( ... )
    {
      
    }
  }
}

void WebRTCBridge::Seeker::FindBridge()
{
  
  std::unique_lock<std::mutex> lock(QueueAccess);
  lock.lock();
  CommInstructQueue.push([this]()
  {
    std::chrono::utc_time<std::chrono::system_clock::duration> localutctime;
    localutctime = std::chrono::utc_clock::now();
    json Offer = {{"Port",Config["LocalPort"]},

    {"Session",std::format("{:%Y-%m-%d %X}",localutctime)}};
    BridgeConnection.Out->Send(Offer.dump());
    auto messagelength = BridgeConnection.In->Receive(true);
    if(messagelength <= 0)
    {
      
    }
    else
    {
      json Answer;
      try
      {
        Answer = json::parse(BridgeConnection.In->StringData);
        std::string timecode = Answer["Session"];
        std::chrono::utc_time<std::chrono::system_clock::duration> remoteutctime;
        std::string format("%Y-%m-%d %X");
        std::stringstream ss(timecode);
        if(ss >> std::chrono::parse(format,remoteutctime))
        {
          // we are checking this for consistency reasons
          if(remoteutctime > localutctime)
          {
            std::cout << "Found the connection successfully." << std::endl;
          }
        }
      }
      catch(...)
      {
        
      }
    }
  });
  lock.release();
}

void WebRTCBridge::Seeker::RecoverConnection()
{
}


std::shared_ptr<WebRTCBridge::Connector> WebRTCBridge::Seeker::CreateConnection()
{
  // structural wrapper to forego the need to create a fractured shared pointer
  struct Wrap { Wrap() :cont(WebRTCBridge::Connector()) {} WebRTCBridge::Connector cont; };
  auto t = std::make_shared<Wrap>();
  std::shared_ptr<WebRTCBridge::Connector> Connection{std::move(t),&t->cont };

  Connection->Bridge = std::shared_ptr<Seeker>(this);
  Connection->ID = ++NextID;

  Connection->Upstream = std::make_shared<BridgeSocket>
  (std::move(BridgeSocket::GetFreeSocketPort(Config["LocalAddress"])));
  
  return Connection;

}

// this is copied on purpose so that the reference counter should be at least 1
// when entering this method
void WebRTCBridge::Seeker::DestroyConnection(std::shared_ptr<Connector> Connector)
{
  UserByID.erase(Connector->ID);
}

void WebRTCBridge::Seeker::ConfigureUpstream(Connector* Instigator, const json& Answer)
{
  Instigator->Upstream->Address = Config["RemoteAddress"];
  Instigator->Upstream->Port = Config["RemotePort"];
  Instigator->Upstream->Connect();

}

void WebRTCBridge::Seeker::CreateTask(std::function<void(void)>&& Task)
{
  std::unique_lock<std::mutex> lock(QueueAccess);
  lock.lock();
  CommInstructQueue.push(Task);
  lock.unlock();
}


void WebRTCBridge::Seeker::StartSignalling(std::string IP, int Port, bool keepAlive, bool useAuthentification)
{
  SignallingConnection = std::make_shared<rtc::WebSocket>();
  std::promise<void> RunGuard;
  auto Notifier = RunGuard.get_future();
  SignallingConnection->onOpen([this, &RunGuard]()
  {
    
  });
  SignallingConnection->onClosed([this, &RunGuard](){});
  SignallingConnection->onError([this, &RunGuard](auto error){});
  SignallingConnection->onMessage([
    this,
    &RunGuard,
    TentativeConnection = std::weak_ptr<rtc::WebSocket>(SignallingConnection)
  ](auto message)
  {
    // without the rtc library types the compiler will get confused as to what this is
    // they types are essentially std::bytes and std::string.
    if(std::holds_alternative<rtc::string>(message))
    {
      json content = json::parse(std::get<rtc::string>(message));
      int ID;
      if(!FindID(content,ID))
      {
        std::cout << "From onMessage SignallingServer Thread: Could not identify player id from input, discarding this message." << std::endl;

      }
      if(content["type"] == "offer")
      {
        std::cout << "I received an offer and this is the most crucial step in bridge setup!" << std::endl;
        std::string sdp = content["sdp"];

        // we MUST fail if this is not resolved as the sdp description
        // has to be SYNCHRONOUSLY valid on both ends of the bridge!
        // Additionally, we must process the answer before anything else happens
        // including the CREATION of the connection, as its initialization
        // depends on what we are hearing back from unreal in terms of
        // payloads and ssrc info
        auto NewConnection = CreateConnection();
        CreateTask(std::bind(&Seeker::BridgeSynchronize, this, NewConnection.get(), sdp, true));
      }
      else if(content["type"] == "iceCandidate")
      {
        std::shared_ptr<Connector> Connector;
        try
        {
        Connector = UserByID[ID];
        }
        catch( ... )
        {
          std::cout << "From onMessage SS thread: Could not find connector for ice candidate." << std::endl;
        }
        Connector->OnInformation(content);
      }

    }
  });
  std::cout << "Waiting for Signalling Websocket to Connect." << std::endl;
  Notifier.wait();
}

