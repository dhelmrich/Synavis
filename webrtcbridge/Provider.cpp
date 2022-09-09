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

void WebRTCBridge::Provider::OnSignallingMessage(std::string Message)
{
  json Content;
  try
  {
    Content = json::parse(Message);
    int id;
    if(!FindID(Content,id))
    {
      throw std::exception("Could not extract ID from SS response.");
    }
    if(Content["type"] == "answer")
    {
      EndpointById[id]->OnInformation(Content);
    }
    else if(Content["type"] == "icecandidate")
    {
      std::string candidate = Content["candidate"];
    }
  }
  catch(...)
  {
    
  }
}

void WebRTCBridge::Provider::OnSignallingData(rtc::binary Message)
{

}
