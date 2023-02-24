#include "DataConnector.hpp"

WebRTCBridge::DataConnector::DataConnector()
{
  pc_ = std::make_shared<rtc::PeerConnection>(rtcconfig_);
  pc_->onGatheringStateChange([this](auto state)
    {
      
      std::cout << "Gathering state changed" << state << std::endl;
      switch(state)
      {
      case rtc::PeerConnection::GatheringState::Complete:
        std::cout << "State switched to complete" << std::endl;
        break;
      case rtc::PeerConnection::GatheringState::InProgress:
        std::cout << "State switched to in progress" << std::endl;
        break;
      case rtc::PeerConnection::GatheringState::New:
        std::cout << "State switched to new connection" << std::endl;
        break;
      }
    });
  pc_->onLocalCandidate([this](auto candidate)
  {
    json ice_message = {{"type","iceCandidate"},
      {"candidate", {{"candidate", candidate.candidate()},
                         "sdpMid", candidate.mid()}}};
    ss_->send(ice_message.dump());
  });
  pc_->onDataChannel([this](auto datachannel)
  {
    std::cout << "I received a channel I did not ask for" << std::endl;
    datachannel->onOpen([this]()
    {
      std::cout << "DataChannel connection is setup!" << std::endl;
    }); 
  });
  pc_->onTrack([this](auto track)
  {
    std::cout << "I received a track I did not ask for" << std::endl;
    track->onOpen([this,track]()
    {
           std::cout << "Track connection is setup!" << std::endl;
      track->send(rtc::binary({ (std::byte)(EClientMessageType::QualityControlOwnership) }));
      track->send(rtc::binary({ std::byte{72},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0} }));
    });
  });
  pc_->onSignalingStateChange([this](auto state)
  {
    std::cout << "SS State changed: " << state << std::endl;
  });
  pc_->onStateChange([this](rtc::PeerConnection::State state)
  {
    std::cout << "State changed: " << state << std::endl;
  });
  ss_ = std::make_shared<rtc::WebSocket>();
  DataChannel = pc_->createDataChannel("DataConnectionChannel");
  DataChannel->onOpen([this]()
    {
      std::cout << "DataChannel connection is setup!" << std::endl;
   
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
    std::cout << "Signalling server connected" << std::endl;
    if (TakeFirstStep && pc_->localDescription().has_value())
    {
      json offer = { {"type","offer"}, {"endpoint", "data"},{"sdp",pc_->localDescription().value()} };
      ss_->send(offer.dump());
      state_ = EConnectionState::OFFERED;
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
      catch (json::exception e)
      {
        std::cout << "Could not read package:" << e.what() << std::endl;
      }
      std::cout << "I received a message of type: " << content["type"] << std::endl;
      if (content["type"] == "answer" || content["type"] == "offer")
      {
        std::string sdp = content["sdp"].get<std::string>();
        rtc::Description remote = sdp;
        pc_->setRemoteDescription(remote);
        if(state_ == EConnectionState::OFFERED && content["type"] == "answer")
          state_ = EConnectionState::CONNECTED;
        if(!InitializedRemote)
        {
          RequiredCandidate.clear();
          // iterating through media sections in the descriptions
          for(auto i = 0; i < remote.mediaCount(); ++i)
          {
            auto extract = remote.media(i);
            if(std::holds_alternative<rtc::Description::Application*>(extract))
            {
              auto app = std::get<rtc::Description::Application*>(extract);
              RequiredCandidate.push_back(app->mid());
            }
            else
            {
              auto media = std::get<rtc::Description::Media*>(extract);
              RequiredCandidate.push_back(media->mid());
            }
          }
          std::cout << "I have " << RequiredCandidate.size() << " required candidates: ";
          for(auto i = 0; i < RequiredCandidate.size(); ++i)
          {
            std::cout << RequiredCandidate[i] << " ";
          }
          std::cout << std::endl;
          InitializedRemote = true;
        }
      }
      else if (content["type"] == "iceCandidate")
      {
        auto sdpMid = content["candidate"]["sdpMid"].get<std::string>();
        // remove sdpMid from required candidates
        RequiredCandidate.erase(std::remove_if(RequiredCandidate.begin(), RequiredCandidate.end(), [sdpMid](auto s){return s==sdpMid;}), RequiredCandidate.end());
        auto sdpMLineIndex = content["candidate"]["sdpMLineIndex"].get<int>();
        std::string candidate_string = content["candidate"]["candidate"].get<std::string>();
        rtc::Candidate ice(candidate_string, sdpMid);
        pc_->addRemoteCandidate(ice);
        // if we have no more required candidates, we can send an answer
        if (!TakeFirstStep && pc_->localDescription().has_value() && RequiredCandidate.size() == 0 && state_ < EConnectionState::OFFERED)
        {
          json offer = { {"type","answer"}, {"sdp",pc_->localDescription().value()} };
          ss_->send(offer.dump());
          std::cout << "I send my answer!" << std::endl;
          this->state_ = EConnectionState::OFFERED;
        }
      }
      else if(content["type"] == "control")
      {
        std::cout << "Received a control message: " << content["message"] << std::endl;
      }
      else if(content["type"] == "id")
      {
        this->config_["id"] = content["id"];
        std::cout << "Received an id: " << content["id"] << std::endl;
      }
      else if(content["type"] == "serverDisconnected")
      {
        pc_.reset();
        std::cout << "Reset peer connection because we received a disconnect" << std::endl;
      }
      else if(content["type"] == "config")
      {
        auto pc_options = content["peerConnectionOptions"];
        // TODO: Set peer connection options
      }
      else if(content["type"] == "playerCount")
      {
        std::cout << "There are " << content["count"] << " players connected" << std::endl;
      }
      else
      {
        std::cout << "unknown message?" << std::endl << content.dump() << std::endl;
      }
    }
  });
  ss_->onClosed([this]()
    {
      state_ = EConnectionState::CLOSED;
      std::cout << "Signalling server was closed" << std::endl;
    });
  ss_->onError([this](std::string error)
  {
    state_ = EConnectionState::CLOSED;
    std::cerr << "Signalling server error: " << error << std::endl;
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
  std::cout << "Starting Signalling to " << address << std::endl;
  state_ = EConnectionState::STARTUP;
  ss_->open(address);
}

void WebRTCBridge::DataConnector::SendData(rtc::binary Data)
{
  if (this->state_ != EConnectionState::CONNECTED)
    return;
  DataChannel->sendBuffer(Data);
}

void WebRTCBridge::DataConnector::SendMessage(std::string Message)
{
  if (this->state_ != EConnectionState::CONNECTED)
    return;
  json content = { {"origin",""},{"data","Message"} };
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
  bool all_found = true;
  // use items iterator for config to check if all required values are present
  for (auto& [key, value] : config_.items())
  {
    if (Config.find(key) == Config.end())
    {
      all_found = false;
      break;
    }
  }
  if (!all_found)
  {
    std::cerr << "Config is missing required values" << std::endl;
    std::cerr << "Required values are: " << std::endl;
    for (auto& [key, value] : config_.items())
    {
      std::cerr << key << " ";
    }
    std::cerr << std::endl << "Provided values are: " << std::endl;
    for (auto& [key, value] : Config.items())
    {
      std::cerr << key << " ";
    }
    throw std::runtime_error("Config is missing required values");
  }
  // inserting all values from config into config_
  // this is done to ensure that all required values are present
  for (auto& [key, value] : Config.items())
  {
       config_[key] = value;
  }

}

bool WebRTCBridge::DataConnector::IsRunning()
{
  // returns true if the connection is in a state where it can send and receive data
  return state_ < EConnectionState::CLOSED || ss_->isOpen();

}

void WebRTCBridge::DataConnector::CommunicateSDPs()
{
  if (pc_->localDescription().has_value())
  {
    json offer = { {"type","answer"}, {"sdp",pc_->localDescription().value()} };
    ss_->send(offer.dump());
    for (auto candidate : pc_->localDescription().value().extractCandidates())
    {
      json ice = { {"type","iceCandidate"}, {"candidate", {{"candidate",candidate.candidate()}, {"sdpMid",candidate.mid()}, {"sdpMLineIndex",1}}} };
      ss_->send(ice.dump());
    }
  }
}
