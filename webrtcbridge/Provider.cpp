#include "Provider.hpp"
#include "UnrealConnector.hpp"


void WebRTCBridge::Provider::FindBridge()
{
  // THis is the unreal bridge side, it would be expedient if we didn't have
  // to know the port here and could set it up automatically
  std::unique_lock<std::mutex> lock(QueueAccess);
  lock.lock();
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
      std::chrono::utc_time<std::chrono::system_clock::duration> remoteutc;
      std::string format("%Y-%m-%d %X");
      std::stringstream ss(timecode);
      if(ss >> std::chrono::parse(format,remoteutc))
      {
        std::cout << "I received remote port info: " << Offer["Port"] << "." << std::endl;
        Config["RemotePort"] = Offer["Port"];
        Config["RemoteAddr"] = Offer["Address"];
        std::chrono::utc_time<std::chrono::system_clock::duration> localutctime;
        localutctime = std::chrono::utc_clock::now();
        json Answer = { {"Port",Config["LocalPort"]},
        {"Session",std::format("{:%Y-%m-%d %X}",localutctime)} };
      }
    }
    catch(...)
    {
      
    }
  }
  lock.release();
}

std::shared_ptr<WebRTCBridge::UnrealConnector> WebRTCBridge::Provider::CreateConnection()
{
  // Todo setup the connection
  struct Wrap { Wrap() : cont(WebRTCBridge::UnrealConnector()) {} WebRTCBridge::UnrealConnector cont; };
  auto t = std::make_shared<Wrap>();
  std::shared_ptr<WebRTCBridge::UnrealConnector> Connection{ std::move(t), &t->cont };
  Connection->Bridge = std::shared_ptr<Provider>(this);
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

void WebRTCBridge::Provider::OnSignallingMessage(std::string Message)
{
  json Content;
  try
  {
    Content = json::parse(Message);
    int id;
    if (!FindID(Content, id))
    {
      throw std::exception("Could not extract ID from SS response.");
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
