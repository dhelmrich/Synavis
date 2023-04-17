#include "DataConnector.hpp"
#include <rtc/candidate.hpp>
#include <chrono>
#include <codecvt>
#include <locale>
#include <bit>
#include <fstream>

#ifdef _WIN32
#include <Windows.h>
#endif


inline constexpr std::byte operator "" _b(unsigned long long i) noexcept
{
  return static_cast<std::byte>(i);
}

WebRTCBridge::DataConnector::DataConnector()
{
  PeerConnection = std::make_shared<rtc::PeerConnection>(rtcconfig_);
  PeerConnection->onGatheringStateChange([this](auto state)
    {
      std::cout << Prefix << "Gathering state changed" << state << std::endl;
      switch (state)
      {
      case rtc::PeerConnection::GatheringState::Complete:
        std::cout << Prefix << "State switched to complete" << std::endl;
        break;
      case rtc::PeerConnection::GatheringState::InProgress:
        std::cout << Prefix << "State switched to in progress" << std::endl;
        break;
      case rtc::PeerConnection::GatheringState::New:
        std::cout << Prefix << "State switched to new connection" << std::endl;
        break;
      }
    });
  PeerConnection->onLocalCandidate([this](auto candidate)
    {
      json ice_message = { {"type","iceCandidate"},
        {"candidate", {{"candidate", candidate.candidate()},
                           "sdpMid", candidate.mid()}} };
  SignallingServer->send(ice_message.dump());
    });
  PeerConnection->onDataChannel([this](auto datachannel)
    {
      std::cout << Prefix << "I received a channel I did not ask for" << std::endl;
      datachannel->onOpen([this]()
        {
          std::cout << Prefix << "THEIR DataChannel connection is setup!" << std::endl;
        });
    });
  PeerConnection->onTrack([this](auto track)
    {
      std::cout << Prefix << "I received a track I did not ask for" << std::endl;
      track->onOpen([this, track]()
        {
          std::cout << Prefix << "Track connection is setup!" << std::endl;
          track->send(rtc::binary({ (std::byte)(EClientMessageType::QualityControlOwnership) }));
          track->send(rtc::binary({ std::byte{72},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0} }));
        });
    });
  PeerConnection->onSignalingStateChange([this](auto state)
    {
      std::cout << Prefix << "SS State changed: " << state << std::endl;
    });
  PeerConnection->onStateChange([this](rtc::PeerConnection::State state)
    {
      std::cout << Prefix << "State changed: " << state << std::endl;
      if (state == rtc::PeerConnection::State::Failed && OnFailedCallback.has_value())
      {
        OnFailedCallback.value()();
      }
    });
  SignallingServer = std::make_shared<rtc::WebSocket>();
  DataChannel = PeerConnection->createDataChannel("DataConnectionChannel");
  DataChannel->onOpen([this]()
    {
      std::cout << Prefix << "OUR DataChannel connection is setup!" << std::endl;

    });
  DataChannel->onMessage([this](auto messageordata)
    {
      if (std::holds_alternative<rtc::binary>(messageordata))
      {
        auto data = std::get<rtc::binary>(messageordata);
        if (DataReceptionCallback.has_value())
          DataReceptionCallback.value()(data);
      }
      else
      {
        auto message = std::get<std::string>(messageordata);
        if (MessageReceptionCallback.has_value())
          MessageReceptionCallback.value()(message);
      }
    });
  DataChannel->onError([this](std::string error)
    {
      std::cerr << "DataChannel error: " << error << std::endl;
    });
  DataChannel->onAvailable([this]()
    {
      std::cout << Prefix << "DataChannel is available" << std::endl;
      state_ = EConnectionState::CONNECTED;
      if (OnConnectedCallback.has_value())
      {
        OnConnectedCallback.value()();
      }
    });
  DataChannel->onBufferedAmountLow([this]()
    {
      std::cout << Prefix << "DataChannel buffered amount low" << std::endl;
    });
  DataChannel->onClosed([this]()
    {
      std::cout << Prefix << "DataChannel is CLOSED again" << std::endl;
      this->state_ = EConnectionState::CLOSED;
      if (OnClosedCallback.has_value())
      {
        OnClosedCallback.value()();
      }
    });

  SignallingServer->onOpen([this]()
    {
      state_ = EConnectionState::SIGNUP;
      std::cout << Prefix << "Signalling server connected" << std::endl;
      if (TakeFirstStep)
      {
        json role_request = { {"type","request"},{"request","role"} };
        std::cout << Prefix << "Attempting to prompt for role, this will fail if the server is not configured to do so" << std::endl;
        //SignallingServer->send(role_request.dump());
      }
      if (TakeFirstStep && PeerConnection->localDescription().has_value())
      {
        json offer = { {"type","offer"}, {"endpoint", "data"},{"sdp",PeerConnection->localDescription().value()} };
        SignallingServer->send(offer.dump());
        state_ = EConnectionState::OFFERED;
      }
    });
  SignallingServer->onMessage([this](auto messageordata)
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
          std::cout << Prefix << "Could not read package:" << e.what() << std::endl;
        }
        std::cout << Prefix << "I received a message of type: " << content["type"] << std::endl;
        if (content["type"] == "answer" || content["type"] == "offer")
        {
          std::string sdp = content["sdp"].get<std::string>();
          rtc::Description remote = sdp;
          if (content["type"] == "answer" && TakeFirstStep)
            PeerConnection->setRemoteDescription(remote);
          else if (content["type"] == "offer" && !TakeFirstStep)
            PeerConnection->setRemoteDescription(remote);
          if (!InitializedRemote)
          {
            RequiredCandidate.clear();
            // iterating through media sections in the descriptions
            for (unsigned i = 0; i < remote.mediaCount(); ++i)
            {
              auto extract = remote.media(i);
              if (std::holds_alternative<rtc::Description::Application*>(extract))
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
            std::cout << Prefix << "I have " << RequiredCandidate.size() << " required candidates: ";
            for (auto i = 0; i < RequiredCandidate.size(); ++i)
            {
              std::cout << Prefix << RequiredCandidate[i] << " ";
            }
            std::cout << Prefix << std::endl;
            InitializedRemote = true;
          }
        }
        else if (content["type"] == "iceCandidate")
        {
          // {"type": "iceCandidate", "candidate": {"candidate": "candidate:1 1 UDP 2122317823 172.26.15.227 42835 typ host", "sdpMLineIndex": "0", "sdpMid": "0"}}
          std::cout << Prefix << "Parsing" << std::endl;
          std::string sdpMid, candidate_string;
          int sdpMLineIndex;
          try
          {
            sdpMid = content["candidate"]["sdpMid"].get<std::string>();
            sdpMLineIndex = content["candidate"]["sdpMLineIndex"].get<int>();
            candidate_string = content["candidate"]["candidate"].get<std::string>();
          }
          catch (std::exception e)
          {
            std::cout << Prefix << "Could not parse candidate: " << e.what() << std::endl;
            return;
          }
          std::cout << Prefix << "I received a candidate for " << sdpMid << " with index " << sdpMLineIndex << " and candidate " << candidate_string << std::endl;
          rtc::Candidate ice(candidate_string, sdpMid);
          try
          {
            PeerConnection->addRemoteCandidate(ice);
            // remove from required candidates if succeeded
            RequiredCandidate.erase(std::remove_if(RequiredCandidate.begin(), RequiredCandidate.end(), [sdpMid](auto s) {return s == sdpMid; }), RequiredCandidate.end());
          }
          catch (std::exception e)
          {
            std::cout << Prefix << "Could not add remote candidate: " << e.what() << std::endl;
          }
          // if we have no more required candidates, we can send an answer
          if (RequiredCandidate.size() == 0)
          {
            std::cout << Prefix << "I have received all required candidates" << std::endl;
            if (!TakeFirstStep && PeerConnection->localDescription().has_value() && state_ < EConnectionState::OFFERED)
            {
              this->state_ = EConnectionState::OFFERED;
              SubmissionHandler.AddTask(std::bind(&DataConnector::CommunicateSDPs, this));
            }
            if (OnIceGatheringFinished.has_value())
            {
              OnIceGatheringFinished.value()();

            }
          }
          else
          {
            std::cout << Prefix << "I still have " << RequiredCandidate.size() << " required candidates: ";
            for (auto i = 0; i < RequiredCandidate.size(); ++i)
            {
              std::cout << Prefix << RequiredCandidate[i] << " ";
            }
            std::cout << Prefix << std::endl;
          }
        }
        else if (content["type"] == "control")
        {
          std::cout << Prefix << "Received a control message: " << content["message"] << std::endl;
        }
        else if (content["type"] == "id")
        {
          this->config_["id"] = content["id"];
          std::cout << Prefix << "Received an id: " << content["id"] << std::endl;
        }
        else if (content["type"] == "serverDisconnected")
        {
          PeerConnection.reset();
          std::cout << Prefix << "Reset peer connection because we received a disconnect" << std::endl;
        }
        else if (content["type"] == "config")
        {
          auto pc_options = content["peerConnectionOptions"];
          // TODO: Set peer connection options
        }
        else if (content["type"] == "playerCount")
        {
          std::cout << Prefix << "There are " << content["count"] << " players connected" << std::endl;
        }
        else if (content["type"] == "role")
        {
          std::cout << Prefix << "Received a role: " << content["role"] << std::endl;
          if (content["role"] == "server")
          {
            this->IsServer = true;
            PeerConnection->setLocalDescription();
          }
        }
        else if (content["type"] == "playerConnected")
        {
          json offer = { {"type","offer"}, {"endpoint", "data"},{"sdp",PeerConnection->localDescription().value()} };
          SignallingServer->send(offer.dump());
          SubmissionHandler.AddTask(std::bind(&DataConnector::CommunicateSDPs, this));
        }
        else if (content["type"] == "playerDisconnected")
        {
          PeerConnection.reset();
          std::cout << Prefix << "Resetting connection because we must be in server role and the player disconnected" << std::endl;
        }
        else
        {
          std::cout << Prefix << "unknown message?" << std::endl << content.dump() << std::endl;
        }
      }
    });
  SignallingServer->onClosed([this]()
    {
      state_ = EConnectionState::CLOSED;
      auto unix_time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

      std::cout << Prefix << "Signalling server was closed at timestamp " << unix_time << std::endl;

    });
  SignallingServer->onError([this](std::string error)
    {
      state_ = EConnectionState::STARTUP;
      SignallingServer->close();
      std::cerr << "Signalling server error: " << error << std::endl;
    });
}

WebRTCBridge::DataConnector::~DataConnector()
{
  SignallingServer->close();
  PeerConnection->close();
  while (SignallingServer->readyState() != rtc::WebSocket::State::Closed)
    std::this_thread::yield();
}

void WebRTCBridge::DataConnector::StartSignalling()
{
  std::string address = "ws://" + config_["SignallingIP"].get<std::string>()
    + ":" + std::to_string(config_["SignallingPort"].get<unsigned>());
  std::cout << Prefix << "Starting Signalling to " << address << std::endl;
  state_ = EConnectionState::STARTUP;
  SignallingServer->open(address);
  while (Block && state_ < EConnectionState::SIGNUP)
  {
    std::this_thread::yield();
  }
}

void WebRTCBridge::DataConnector::SendData(rtc::binary Data)
{
  if (this->state_ != EConnectionState::CONNECTED)
    return;
  if (Data.size() > DataChannel->maxMessageSize())
  {
    rtc::binary Chunk(DataChannel->maxMessageSize());
    const auto meta_size = sizeof(int) + sizeof(int) + sizeof(uint16_t) + sizeof(std::byte);
    unsigned int chunks{ 0 };
    for (; (meta_size * chunks + Data.size()) / chunks < DataChannel->maxMessageSize(); ++chunks);

    // iterate through the chunks
    for (auto i = 0u; i < chunks; ++i)
    {
      unsigned int n = 0u;
      n = InsertIntoBinary(Chunk, n, std::byte(50), uint16_t(0));
      n = InsertIntoBinary(Chunk, n, i, chunks);
      memcpy(Chunk.data() + n, Data.data() + i * chunks, DataChannel->maxMessageSize() - meta_size);
      DataChannel->sendBuffer(Chunk);
    }
  }
  else
  {
    DataChannel->sendBuffer(Data);
  }
}

void WebRTCBridge::DataConnector::SendString(std::string Message)
{
  if (this->state_ != EConnectionState::CONNECTED)
    return;
  json content = { {"origin","dataconnector"},{"data",Message} };
  std::string json_message = content.dump();
  // prepare bytes that Unreal expects at the beginning of the message
  rtc::binary bytes(3 + 2 * json_message.length());
  bytes.at(0) = DataChannelByte;
  uint16_t* buffer = reinterpret_cast<uint16_t*>(&(bytes.at(1)));
  *buffer = static_cast<uint16_t>(json_message.size());
  for (int i = 0; i < json_message.size(); i++)
  {
    bytes.at(3 + 2 * i) = static_cast<std::byte>(json_message.at(i));
    bytes.at(3 + 2 * i + 1) = 0_b;
  }
  DataChannel->sendBuffer(bytes);
}

void WebRTCBridge::DataConnector::SendJSON(json Message)
{
  if (this->state_ != EConnectionState::CONNECTED)
    return;
  std::string json_message = Message.dump();
  // prepare bytes that Unreal expects at the beginning of the message
  rtc::binary bytes(3 + 2 * json_message.length());
  bytes.at(0) = DataChannelByte;
  uint16_t* buffer = reinterpret_cast<uint16_t*>(&(bytes.at(1)));
  *buffer = static_cast<uint16_t>(json_message.size());
  for (int i = 0; i < json_message.size(); i++)
  {
    bytes.at(3 + 2 * i) = static_cast<std::byte>(json_message.at(i));
    bytes.at(3 + 2 * i + 1) = 0_b;
  }
  DataChannel->sendBuffer(bytes);
}

WebRTCBridge::EConnectionState WebRTCBridge::DataConnector::GetState()
{
  return state_;
}

void WebRTCBridge::DataConnector::SetDataCallback(std::function<void(rtc::binary)> Callback)
{
  this->DataReceptionCallback = Callback;
}

void WebRTCBridge::DataConnector::SetMessageCallback(std::function<void(std::string)> Callback)
{
  this->MessageReceptionCallback = Callback;
}

void WebRTCBridge::DataConnector::SetConfigFile(std::string ConfigFile)
{
  std::ifstream file(ConfigFile);
  if (!file.is_open())
  {
    std::cerr << "Could not open config file " << ConfigFile << std::endl;
    return;
  }
  json js = json::parse(file);
  SetConfig(js);
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
  return state_ < EConnectionState::CLOSED || SignallingServer->isOpen();

}

// a method that outputs data channel information
void WebRTCBridge::DataConnector::PrintCommunicationData()
{
  auto max_message = DataChannel->maxMessageSize();
  auto protocol = DataChannel->protocol();
  auto label = DataChannel->label();
  std::cout << Prefix << "Data Channel " << label << " has protocol " << protocol << " and max message size " << max_message << std::endl;

}

void WebRTCBridge::DataConnector::CommunicateSDPs()
{
  if (PeerConnection->localDescription().has_value())
  {
    if (!TakeFirstStep)
    {
      json offer = { {"type","answer"}, {"sdp",PeerConnection->localDescription().value()} };
      SignallingServer->send(offer.dump());
    }
    for (auto candidate : PeerConnection->localDescription().value().extractCandidates())
    {
      json ice = { {"type","iceCandidate"}, {"candidate", {{"candidate",candidate.candidate()}, {"sdpMid",candidate.mid()}, {"sdpMLineIndex",std::stoi(candidate.mid())}}} };
      SignallingServer->send(ice.dump());
    }
  }
}
