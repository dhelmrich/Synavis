#include "DataConnector.hpp"

WebRTCBridge::DataConnector::DataConnector()
{
  pc_ = std::make_shared<rtc::PeerConnection>(rtcconfig_);
  pc_->onGatheringStateChange([this](auto state)
  {
    if(state == rtc::PeerConnection::GatheringState::Complete)
    {
      this->state_ = EConnectionState::CONNECTED;
    }
  });
  ss_ = std::make_shared<rtc::WebSocket>();
  DataChannel = pc_->createDataChannel("DataConnectionChannel");
  DataChannel->onOpen([this]()
  {
    
  });
  DataChannel->onMessage([this](auto messageordata)
  {
    if (std::holds_alternative<rtc::binary>(messageordata))
    {
      auto data = std::get<rtc::binary>(messageordata);
    }
    else
    {
      auto message = std::get<std::string>(messageordata);
    }
  });
  ss_->onOpen([this]()
  {
    state_ = EConnectionState::SIGNUP;
    if(pc_->localDescription().has_value())
    {
      json offer = {{"type","offer"}, {"endpoint", "data"},{"sdp",pc_->localDescription().value()}};
      ss_->send(offer.dump());
    }
  });
  ss_->onMessage([this](auto messageordata)
  {
    if (std::holds_alternative<rtc::binary>(messageordata))
    {
      auto data = std::get<rtc::binary>(messageordata);
    }
    else
    {
      auto message = std::get<std::string>(messageordata);
      json content;
      try
      {
        content = json::parse(message);
      }
      catch(json::exception e)
      {
        std::cout << "Could not read package:" << e.what() << std::endl;
      }
      if(content["type"] == "answer" || content["type"] == "offer")
      {
        std::string sdp = content["sdp"].get<std::string>();
        rtc::Description remote = sdp;
        pc_->setRemoteDescription(remote);
      }
      else if(content["type"] == "iceCandidate")
      {
        rtc::Candidate ice (content["iceCandidate"]["candidate"],content["candidate"]["sdpMid"]);
        pc_->addRemoteCandidate(ice);
        // TODO: make upper-level ice-candidate standard for all webrtc interactions
        // TODO: Check if Unreal accepts this change.
      }
    }
  });
  ss_->onClosed([this]()
  {
    state_ = EConnectionState::CLOSED;
  });
}

WebRTCBridge::DataConnector::~DataConnector()
{
  pc_->close();
  ss_->close();
}

void WebRTCBridge::DataConnector::StartSignalling()
{
  std::string address = "ws://" + config_["SignallingIP"].get<std::string>()
    + ":" + std::to_string(config_["SignallingPort"].get<unsigned>());
  ss_->open(address);
}

void WebRTCBridge::DataConnector::SendData(rtc::binary Data)
{
  if(this->state_ != EConnectionState::CONNECTED)
    return;
  DataChannel->sendBuffer(Data);
}

void WebRTCBridge::DataConnector::SendMessage(std::string Message)
{
  if(this->state_ != EConnectionState::CONNECTED)
    return;
  json content = {{"origin",""},{"data","Message"}};
  DataChannel->send(content.dump());
}

WebRTCBridge::EConnectionState WebRTCBridge::DataConnector::GetState()
{
  return state_;
}

void WebRTCBridge::DataConnector::SetCallback(std::function<void(rtc::binary)> Callback)
{
  this->DataReceptionCallback = Callback;
}

void WebRTCBridge::DataConnector::SetConfig(json Config)
{
  if(!std::all_of(config_.begin(), config_.end(), [&Config](auto& item)
  {
    return Config.find(item) != Config.end();
  }))
  {
    throw std::runtime_error("Config is missing required values");
  }
  // make sure that the default config values are present
  for(auto& [key, value] : config_.items())
  {
    if(Config.find(key) == Config.end())
    {
      Config[key] = value;
    }
  }
  
}
