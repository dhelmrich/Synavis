#include "seeker.hpp"

#include <variant>
#include <memory>

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

WebRTCBridge::Seeker::Seeker() : Bridge()
{
}

WebRTCBridge::Seeker::~Seeker()
{
  
}

bool WebRTCBridge::Seeker::CheckSignallingActive()
{
  return false;
}

bool WebRTCBridge::Seeker::EstablishedConnection(bool Shallow)
{
  if(Shallow)
  {
    return Bridge::EstablishedConnection(Shallow);
  }
  else
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
}

void WebRTCBridge::Seeker::FindBridge()
{
  // THis is a wait function.
  std::unique_lock<std::mutex> lock(QueueAccess);
  lock.lock();
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
  Connection->SetID(this,++NextID);
  
  return Connection;

}

// this is copied on purpose so that the reference counter should be at least 1
// when entering this method
void WebRTCBridge::Seeker::DestroyConnection(std::shared_ptr<Connector> Connector)
{
  EndpointById.erase(Connector->ID);
}

void WebRTCBridge::Seeker::ConfigureUpstream(Connector* Instigator, const json& Answer)
{

}

void WebRTCBridge::Seeker::BridgeRun()
{
  Bridge::BridgeRun();
}

void WebRTCBridge::Seeker::Listen()
{
  Bridge::Listen();
}

void WebRTCBridge::Seeker::OnSignallingMessage(std::string Message)
{
  json content = json::parse(Message);
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
    std::shared_ptr<Connector> Endpoint;
    try
    {
      std::shared_ptr<Adapter> fetch_object = EndpointById[ID];
      Endpoint = std::dynamic_pointer_cast<Connector>(fetch_object);
      if(!Endpoint)
      {
        throw std::exception("Tried to cast an adapter to a connector and failed.");
      }
    }
    catch( ... )
    {
      std::cout << "From onMessage SS thread: Could not find connector for ice candidate." << std::endl;
    }
    Endpoint->OnRemoteInformation(content);
  }
}

void WebRTCBridge::Seeker::OnSignallingData(rtc::binary Message)
{
}

