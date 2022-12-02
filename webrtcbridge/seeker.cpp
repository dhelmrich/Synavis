#include "Seeker.hpp"

#include <variant>
#include <memory>
#include <chrono>
#ifdef __linux__
#include <date/date.h>
#endif

#ifdef __linux__
#include <date/tz.h>
#endif

#include "Connector.hpp"

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
}

void WebRTCBridge::Seeker::FindBridge()
{
  // THis is a wait function.
  std::unique_lock<std::mutex> lock(QueueAccess);
  lock.lock();
#ifdef _WIN32
  std::chrono::utc_time<std::chrono::system_clock::duration> localutctime;
  localutctime = std::chrono::utc_clock::now();
  auto timestring = [localutctime]() -> std::string{
    std::stringstream ss;
    ss << std::format("{:%Y-%m-%d %X}",localutctime);
   return ss.str();} ();
#elif defined __linux__
  std::string timestring(26,'\0');
  auto localutctime = std::chrono::system_clock::now();
  
  timestring = [localutctime]() -> std::string{
    std::stringstream ss;
    ss << date::format("{:%Y-%m-%d %X}",localutctime);
   return ss.str();} ();
#endif
  json Offer = {{"Port",Config["LocalPort"]},
  {"Session",timestring}};

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
#ifdef _WIN32
      std::chrono::utc_time<std::chrono::system_clock::duration> remoteutctime;
      std::chrono::utc_time<std::chrono::system_clock::duration> localutctime;
      localutctime = std::chrono::utc_clock::now();
      bool result = ParseTimeFromString(timecode, remoteutctime);
      if(result)
      {
        // we are checking this for consistency reasons
        if(localutctime > remoteutctime)
        {
          std::cout << "Found the connection successfully." << std::endl;
        }
      }
#elif defined __linux__
      std::chrono::system_clock::time_point remotetime;
      bool result = ParseTimeFromString(timecode, remotetime);
      std::chrono::system_clock SystemClock;
      auto timepoint = SystemClock.now();
      if(result)
      {
        // we are checking this for consistency reasons
        if(timepoint > remotetime)
        {
          std::cout << "Found the connection successfully." << std::endl;
        }
      }
#endif
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

  Connection->OwningBridge = std::shared_ptr<Seeker>(this);
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

    // we MUST fail if this is not resolved as the sdp description
    // has to be SYNCHRONOUSLY valid on both ends of the bridge!
    // Additionally, we must process the answer before anything else happens
    // including the CREATION of the connection, as its initialization
    // depends on what we are hearing back from unreal in terms of
    // payloads and ssrc info
    auto NewConnection = CreateConnection();
    CreateTask(std::bind(&Seeker::BridgeSynchronize, this, NewConnection.get(), content, true));
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
        throw std::runtime_error("Tried to cast an adapter to a Connector and failed.");
      }
    }
    catch( ... )
    {
      std::cout << "From onMessage SS thread: Could not find Connector for ice candidate." << std::endl;
    }
    Endpoint->OnRemoteInformation(content);
  }
  else if(content["type"] == "connected")
  {
    // symmetric to the offer entry, but we do not create a connection here
    int id;
    if(!FindID(Message,id))
    {
      json response = {{"type","error"}, {"what","Could not extract ID from connection notice."}};
      SignallingConnection->send(response.dump());
    }
    else
    {
      CreateTask(std::bind(&Seeker::BridgeSynchronize, this, nullptr, content, true));
    }
  }
}

void WebRTCBridge::Seeker::OnSignallingData(rtc::binary Message)
{

}

uint32_t WebRTCBridge::Seeker::SignalNewEndpoint()
{
  // we do not need to do anything, in terms of default behavior, since the new endpoint is signalled
  // to the bridge first.
  return 0;
}

void WebRTCBridge::Seeker::RemoteMessage(json Message)
{
  if(Message["type"] == "offer")
  {
    int id;
    if (!FindID(Message, id))
    {
      BridgeSynchronize(nullptr, nullptr, json({{"type","error"},{"what","Could not deduce ID from message."}}, false));
    }
    // this is the answer to "connected"
    // the remote end will already have setup a connection here.
    auto NewConnection = CreateConnection();
    NewConnection->OnRemoteInformation(Message);
    EndpointById[id] = NewConnection;
  }
}

void WebRTCBridge::Seeker::InitConnection()
{
  Bridge::InitConnection();
}

