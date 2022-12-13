#include "Provider.hpp"
#include "UnrealConnector.hpp"

#include <chrono>
#ifdef __linux__
#include <date/date.h>
#include <date/tz.h>
std::string FormatTime(std::chrono::time_point<std::chrono::system_clock> t)
{
  return date::format("{:%Y-%m-%d %X}",t);
}
#elif _WIN32
std::string FormatTime(std::chrono::utc_time<std::chrono::system_clock::duration> t)
{
  return std::format("{:%Y-%m-%d %X}",t);
}
#endif

void WebRTCBridge::Provider::FindBridge()
{
  std::cout << this->Prefix() << "Find Bridge" << std::endl;
  const std::lock_guard<std::mutex> lock(QueueAccess);
  // THis is the unreal bridge side, it would be expedient if we didn't have
  // to know the port here and could set it up automatically
  auto messagelength = BridgeConnection.In->Receive(true);
  if(messagelength <= 0)
  {
    
  }
  else
  {
    json Offer;
    try
    {
      Offer = json::parse(BridgeConnection.In->StringData);
      std::string timecode = Offer["Session"];
      bool result;
#ifdef _WIN32
      std::chrono::utc_time<std::chrono::system_clock::duration> remoteutctime;
      result = ParseTimeFromString(timecode, remoteutctime);
      if(result)
      {
        std::cout << Prefix() << "I received remote port info: " << Offer["Port"] << "." << std::endl;
        Config["RemotePort"] = Offer["Port"];
        Config["RemoteAddr"] = Offer["Address"];
        std::chrono::utc_time<std::chrono::system_clock::duration> localutctime;
        localutctime = std::chrono::utc_clock::now();
        json Answer = { {"Port",Config["LocalPort"]},
        {"Session",std::format("{:%Y-%m-%d %X}",localutctime)} };
      }
#elif defined __linux__
      std::chrono::system_clock::time_point remotetime;
      result = ParseTimeFromString(timecode, remotetime);
      if(result)
      {
        std::cout << Prefix() << "I received remote port info: " << Offer["Port"] << "." << std::endl;
        Config["RemotePort"] = Offer["Port"];
        Config["RemoteAddr"] = Offer["Address"];
        std::chrono::system_clock localsystemtime;
        auto localnowtime = localsystemtime.now();
        json Answer = { {"Port",Config["LocalPort"]},
        {"Session",FormatTime(localnowtime)} };
      }
#endif
    }
    catch(...)
    {
      
    }
  }
}

std::shared_ptr<WebRTCBridge::UnrealConnector> WebRTCBridge::Provider::CreateConnection()
{
  // Todo setup the connection
  struct Wrap { Wrap() : cont(WebRTCBridge::UnrealConnector()) {} WebRTCBridge::UnrealConnector cont; };
  auto t = std::make_shared<Wrap>();
  std::shared_ptr<WebRTCBridge::UnrealConnector> Connection{ std::move(t), &t->cont };
  Connection->OwningBridge = std::shared_ptr<Provider>(this);
  Connection->SetID(this,++NextID);
  return Connection;
}

uint32_t WebRTCBridge::Provider::SignalNewEndpoint()
{
  // todo: once a new unreal instance is created (possibly here), we need to notify the bridges
  return 0;
}

void WebRTCBridge::Provider::RemoteMessage(json Message)
{
  // this is the initial message when also the playerID is being generated
  if (Message["type"] == "connected")
  {
    json data{ {"type","playerConnected"} };
    // todo I need a new ID here, and possibly the creation of the new connection
    // The signalling server can also keep track of IDs
    SignallingConnection->send(data.dump());
    // this automatically signals a new endpoint
  }
  else if (Message["type"] == "offer" || Message["type"] == "answer")
  {
    // lodo: this is symmetric and should also trigger on answer
    // we firstly will get an offer from unreal.
    // the pysignalling server should also be able to tell us
    // the details on the extension maps for the rtp packages
    // so we can actually read the info that is being included
    rtc::Description desc(Message["sdp"]);
    for (auto i = 0; i < desc.mediaCount(); ++i)
    {
      auto media = desc.media(i);
      if (std::holds_alternative<rtc::Description::Application*>(media))
      {
        auto app = std::get<rtc::Description::Application*>(media);
      }
      else
      {
        auto track = std::get<rtc::Description::Media*>(media);
        if (track->direction() == rtc::Description::Direction::SendOnly
          || track->direction() == rtc::Description::Direction::SendRecv)
        {
          // todo is this really a NEW track?
        }
      }
    }
    this->RtpDestinationHeader = Message["RtpDestinationHeader"];
  }
}

bool WebRTCBridge::Provider::EstablishedConnection(bool Shallow)
{
  using namespace std::chrono_literals;
  if(Shallow)
  {
    return Bridge::EstablishedConnection(Shallow);
  }
  else
  {
    int PingSuccessful = -1;
    std::chrono::system_clock::time_point precheck = std::chrono::system_clock::now();
    std::cout << Prefix() << "Sending the request to wait for ping" << std::endl;
    CreateTask([this,&PingSuccessful]
      {
        int reception{0};
        reception = BridgeConnection.In->Receive();
        // while((reception = BridgeConnection.In->Peek()) == 0)
        // {
        //   std::this_thread::yield();
        // }
        try
        {
          std::cout << Prefix() << "Received something that might be a ping:" << std::endl;
          std::cout << BridgeConnection.In->StringData << std::endl;
          if(json::parse(BridgeConnection.In->StringData)["ping"] == 0)
          {
            std::cout << Prefix() << "Sending pong!" << std::endl;
            BridgeConnection.Out->Send(json({{"ping",int(1)}}).dump());
            PingSuccessful = 1;
          }
        }catch(...)
        {
          PingSuccessful = 0;
        }
      });
    while(PingSuccessful == -1)
    {
      std::chrono::system_clock::time_point sysnow = std::chrono::system_clock::now();
      auto orig_diff = sysnow - precheck;
      auto diff = std::chrono::duration_cast<std::chrono::seconds>(orig_diff);
      if(diff < Timeout || TimeoutPolicy < EMessageTimeoutPolicy::Critical)
        std::this_thread::yield();
      else
        break;
    }
    return PingSuccessful == 1;
  }
}

void WebRTCBridge::Provider::InitConnection()
{
  Bridge::InitConnection();
}

std::string WebRTCBridge::Provider::Prefix()
{
  return "[ProviderThread]: ";
}

void WebRTCBridge::Provider::OnSignallingMessage(std::string Message)
{
  json Content;
  try
  {
    Content = json::parse(Message);
    int id;
    if (!FindID(Content, id))
    {
      throw std::runtime_error("Could not extract ID from SS response.");
    }
    else
    {
      // We do not even do this locally
      //EndpointById[id]->OnRemoteInformation(Content);

    }

    if (Content["type"] == "answer" || Content["type"] == "offer")
    {
      // answer must be relayed!!
      BridgeSynchronize(EndpointById[id].get(), Content, false);
      rtc::Description unrealdescription(Content["sdp"]);
      auto* unreal_endpoint = dynamic_cast<UnrealConnector*>(this->EndpointById[id].get());
      unreal_endpoint->pc_->setRemoteDescription(unrealdescription);
    }
    else if (Content["type"] == "icecandidate")
    {
      std::string candidate = Content["candidate"];
      rtc::Candidate ice(candidate, Content["sdpMid"]);
    }
    else if (Content["type"] == "playerConnected")
    {
      auto connection = CreateConnection();
      connection->ID = id;
      this->EndpointById[id] = connection;
    }
  }
  catch(...)
  {
    
  }
}

void WebRTCBridge::Provider::OnSignallingData(rtc::binary Message)
{
  // I would not know what to do here
}
