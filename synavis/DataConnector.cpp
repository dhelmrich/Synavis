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

#undef min
#undef max

static std::string Prefix = "DataConnector: ";
static const Synavis::Logger::LoggerInstance lconnector = Synavis::Logger::Get()->LogStarter("DataConnector");

inline constexpr std::byte operator "" _b(unsigned long long i) noexcept
{
  return static_cast<std::byte>(i);
}

std::shared_ptr<rtc::DataChannel> Synavis::DataConnector::GetDataChannel()
{
  if (this->SelectedDataChannel)
    return this->SelectedDataChannel;
  if (this->DataChannels.empty())
    return nullptr;
  return this->DataChannels.front();
}

std::vector<std::string> Synavis::DataConnector::GetDataChannelNames() const
{
  std::vector<std::string> names;
  for (auto &dc : this->DataChannels)
  {
    if (!dc) continue;
    names.push_back(dc->label());
  }
  return names;
}

bool Synavis::DataConnector::SelectDataChannelByName(const std::string& Name)
{
  for (auto &dc : this->DataChannels)
  {
    if (!dc) continue;
    if (dc->label() == Name)
    {
      this->SelectedDataChannel = dc;
      return true;
    }
  }
  return false;
}

bool Synavis::DataConnector::SelectDataChannelByIndex(std::size_t Index)
{
  if (Index >= this->DataChannels.size()) return false;
  this->SelectedDataChannel = this->DataChannels[Index];
  return true;
}

void Synavis::DataConnector::SetupDataChannelHandlers(std::shared_ptr<rtc::DataChannel> channel)
{
  lconnector(ELogVerbosity::Info) << "Setting up DataChannel handlers for label " << channel->label() << std::endl;

  channel->onOpen([this, channel]()
    {
      lconnector(ELogVerbosity::Info) << "DataChannel (" << channel->label() << ") is OPEN!" << std::endl;
      if (channel->maxMessageSize() > std::numeric_limits<uint16_t>::max() - 3)
      {
        lconnector(ELogVerbosity::Warning) << "****************************************************************************" << std::endl;
        lconnector(ELogVerbosity::Warning) << "*                                                                          *" << std::endl;
        lconnector(ELogVerbosity::Warning) << "* WARNING: DataChannel message size is larger than the UE size byte uint16 *" << std::endl;
        lconnector(ELogVerbosity::Warning) << "*                                                                          *" << std::endl;
        lconnector(ELogVerbosity::Warning) << "****************************************************************************" << std::endl;
      }
      this->MaxMessageSize = std::min(channel->maxMessageSize(), static_cast<std::size_t>(std::numeric_limits<uint16_t>::max() - 3));
      state_ = EConnectionState::CONNECTED;
    });

  channel->onMessage(std::bind(&DataConnector::DataChannelMessageHandling, this, std::placeholders::_1));

  channel->onError([this](std::string error)
    {
      lconnector(ELogVerbosity::Error) << "DataChannel error: " << error << std::endl;
    });

  channel->onAvailable([this]()
    {
      if (OnDataChannelAvailableCallback.has_value())
        OnDataChannelAvailableCallback.value()();
    });

  channel->onBufferedAmountLow([this]()
    {
      lconnector(ELogVerbosity::Info) << "DataChannel buffered amount low" << std::endl;
    });

  channel->onClosed([this, channel]()
    {
      lconnector(ELogVerbosity::Info) << "DataChannel Label=" << channel->label() << " is closed!" << std::endl;
      this->state_ = EConnectionState::CLOSED;
      if (OnClosedCallback.has_value())
      {
        OnClosedCallback.value()();
      }
    });
}

Synavis::DataConnector::DataConnector()
{
}

Synavis::DataConnector::~DataConnector()
{
  SignallingServer->close();
  if(PeerConnection) PeerConnection->close();
  SubmissionHandler.Stop();
  for (auto &dc : DataChannels)
  {
    if (dc)
      dc->close();
  }
}

void Synavis::DataConnector::StartSignalling()
{
  std::string address = "ws://" + config_["SignallingIP"].get<std::string>()
    + ":" + std::to_string(config_["SignallingPort"].get<unsigned>());
  lconnector(ELogVerbosity::Info) << "Starting Signalling to " << address << std::endl;
  state_ = EConnectionState::STARTUP;
  SignallingServer->open(address);
  while (Block && state_ < EConnectionState::SIGNUP)
  {
    std::this_thread::yield();
  }
}

void Synavis::DataConnector::SendData(rtc::binary Data)
{
  if (this->state_ != EConnectionState::CONNECTED)
    return;

  // Send raw binary data. If larger than MaxMessageSize, split into raw chunks
  // and send each chunk without any leading size/type prefix. The UE side
  // distinguishes JSON (text frames) from binary frames, so no extra framing
  // is necessary.
  std::size_t offset = 0;
  const std::size_t total = Data.size();
  while (offset < total)
  {
    std::size_t remaining = total - offset;
    std::size_t chunkSize = std::min(remaining, this->MaxMessageSize);
    rtc::binary chunk(chunkSize);
    memcpy(chunk.data(), Data.data() + offset, chunkSize);
    auto ch = GetDataChannel();
    if (!ch)
      return;
    ch->sendBuffer(chunk);
    offset += chunkSize;
  }
}

void Synavis::DataConnector::SendToSignallingServer(json Message)
{
  SignallingServer->send(Message.dump());
}

void Synavis::DataConnector::SendString(std::string Message)
{
  if (this->state_ != EConnectionState::CONNECTED)
    return;
  // Send as a plain text frame containing JSON. UE will parse text frames as JSON.
  json content = { {"origin","dataconnector"}, {"data", Message} };
  std::string json_message = content.dump();
  auto ch = GetDataChannel();
  if (!ch) return;
  ch->send(json_message);
}

void Synavis::DataConnector::SendJSON(json Message)
{
  if (this->state_ != EConnectionState::CONNECTED)
    return;
  // Send plain JSON as a text frame; UE will distinguish JSON via text parsing.
  std::string json_message = Message.dump();
  lconnector(ELogVerbosity::Info) << "Sending JSON: " << json_message << std::endl;
  auto ch = GetDataChannel();
  if (!ch) return;
  ch->send(json_message);
}

bool Synavis::DataConnector::SendBuffer(const std::span<const uint8_t>& Buffer, std::string Name, std::string Format)
{
  // this method briefly exchanges the callback for message reception
  auto msg_callback = MessageReceptionCallback;
  int MessageState = (this->DontWaitForAnswer) ? 1 : 0;
  int StateTracker = 1;
  this->SetMessageCallback([&msg_callback, &MessageState, this](std::string Message)
    {
      lconnector(ELogVerbosity::Info) << "Message received: " << Message << std::endl;
      json content = json::parse(Message);
      if (content["type"] == "buffer")
      {
        MessageState = MessageState + 1;
      }
      else if (content["type"] == "error")
      {
        MessageState = -1;
      }
      else if (msg_callback.has_value())
      {
        msg_callback.value()(Message);
      }
    });
  auto WaitTimeout = [&MessageState, &StateTracker, this](bool bFail = true, double failtime = 2.0)
    {
      auto start_time = std::chrono::system_clock::now();
      auto failtime_seconds = std::chrono::duration<double>(failtime);
      auto waittime = failtime_seconds / 10.0;
      while (MessageState < StateTracker)
      {
        if (this->LogVerbosity >= ELogVerbosity::Verbose)
        {
          std::this_thread::sleep_for(waittime);
          lconnector(ELogVerbosity::Verbose) << "Waiting for message " << StateTracker << " time " << std::chrono::duration<double>(std::chrono::system_clock::now() - start_time).count() << " of " << failtime_seconds.count() << "." << std::endl;
        }
        else
        {
          std::this_thread::yield();
        }
        if (bFail && (std::chrono::duration<double>(std::chrono::system_clock::now() - start_time) > failtime_seconds))
        {
          lconnector(ELogVerbosity::Debug) << "Message reception timed out" << std::endl;
          MessageState = -1;
          break;
        }
      }
      StateTracker++;
    };
  std::size_t chunk_size{}, chunks{}, total_size{};
  const uint8_t* Source = nullptr;
  bool NeedToDelete = false;
  if (Format == "raw")
  {
    total_size = Buffer.size();
    chunk_size = this->MaxMessageSize - 4;
    chunks = std::max(Buffer.size() / chunk_size, static_cast<std::size_t>(1));
    Source = reinterpret_cast<const uint8_t*>(Buffer.data());
  }
  else if (Format == "base64")
  {
    total_size = EncodedSize(Buffer);
    chunk_size = this->MaxMessageSize - 4;
    chunks = total_size / chunk_size + 1;
    const auto ConvertedDat = Encode64(Buffer);
    const std::string DebugTest(ConvertedDat.begin(), ConvertedDat.end());
    Source = reinterpret_cast<const uint8_t*>(ConvertedDat.data());
    NeedToDelete = true;
  }
  else if (Format == "ascii")
  {
    const char* cstr = reinterpret_cast<const char*>(Buffer.data());
    total_size = Buffer.size();
    chunk_size = this->MaxMessageSize - 4;
    chunks = total_size / chunk_size + 1;
    Source = reinterpret_cast<const uint8_t*>(cstr);
  }
  if (!Source)
  {
    if (NeedToDelete)
    {
      delete[] Source;
    }
    throw std::runtime_error(Prefix + "Invalid format for buffer transmission");
  }
  // transmit
  lconnector(ELogVerbosity::Debug) << "Transmitting buffer of size " << Buffer.size() << " in " << chunks << " chunks of size " << chunk_size << std::endl;
  this->SendJSON({ {"type","buffer"}, {"start",Name }, {"size", total_size}, {"format", Format} });
  lconnector(ELogVerbosity::Debug) << "Sent start message" << std::endl;
  if (!DontWaitForAnswer)
  {
    WaitTimeout(this->FailIfNotComplete, TimeOut);
    lconnector(ELogVerbosity::Debug) << "Received start message" << std::endl;
  }
  rtc::binary bytes(std::min(chunk_size, total_size));
  uint8_t* buffer = reinterpret_cast<uint8_t*>(bytes.data());
  // move through the chunks
  lconnector(ELogVerbosity::Verbose) << "Message state is " << MessageState << " chunk info " << total_size << "->" << chunk_size << "(" << chunks << ")" << std::endl;
  for (int i = 0; i < chunks && MessageState > 0; i++)
  {
    const auto remaining = std::min(chunk_size, total_size - i * chunk_size);
    if (bytes.size() != remaining)
    {
      bytes.resize(remaining);
      buffer = reinterpret_cast<uint8_t*>(bytes.data());
    }
    // copy the chunk into the buffer
    memcpy(buffer, Source + i * chunk_size, remaining);
    // send the raw chunk as a binary frame (no prefix)
    lconnector(ELogVerbosity::Debug) << "Sending chunk " << i << " of length " << remaining << std::endl;
    auto ch = GetDataChannel();
    if (!ch) break;
    ch->sendBuffer(bytes);
    // wait for the message to be received
    if (!DontWaitForAnswer)
    {
      WaitTimeout(this->FailIfNotComplete, TimeOut);
      lconnector(ELogVerbosity::Debug) << "Received message " << i << std::endl;
    }
  }
  this->SendJSON({ {"type","buffer"},{"stop",Name} });
  if (!DontWaitForAnswer) WaitTimeout(this->FailIfNotComplete, TimeOut);
  lconnector(ELogVerbosity::Info) << "Sent stop message" << std::endl;
  // restore the original callback
  MessageReceptionCallback = msg_callback;
  if (NeedToDelete)
  {
    delete[] Source;
  }
  return this->DontWaitForAnswer || MessageState > 0;
}

bool Synavis::DataConnector::SendFloat64Buffer(const std::vector<double>& Buffer, std::string Name, std::string Format)
{
  return this->SendBuffer(std::span(reinterpret_cast<const uint8_t*>(Buffer.data()), Buffer.size() * sizeof(double)), Name, Format);
}

bool Synavis::DataConnector::SendFloat32Buffer(const std::vector<float>& Buffer, std::string Name, std::string Format)
{
  return this->SendBuffer(std::span(reinterpret_cast<const uint8_t*>(Buffer.data()), Buffer.size() * sizeof(float)), Name, Format);
}

bool Synavis::DataConnector::SendInt32Buffer(const std::vector<int32_t>& Buffer, std::string Name,
  std::string Format)
{
  return this->SendBuffer(std::span(reinterpret_cast<const uint8_t*>(Buffer.data()), Buffer.size() * sizeof(int32_t)), Name, Format);
}

void Synavis::DataConnector::SendGeometry(const std::vector<double>& Vertices, const std::vector<uint32_t>& Indices,
  std::string Name, std::optional<std::vector<double>> Normals, std::optional<std::vector<double>> UVs,
  std::optional<std::vector<double>> Tangents, bool AutoMessage)
{
  json Message = { {"type","geometry"},{"name",Name} };
  // calculate the total size[bytes] of the message if we were to send it as a single buffer
  std::size_t total_size = 3;
  // because a single buffer would mean that we use JSON, we need to add the base64 size of the buffers
  total_size += EncodedSize(Vertices);
  total_size += EncodedSize(Indices);
  if (Normals.has_value())
  {
    total_size += EncodedSize(Normals.value());
  }
  if (UVs.has_value())
  {
    total_size += EncodedSize(UVs.value());
  }
  if (Tangents.has_value())
  {
    total_size += EncodedSize(Tangents.value());
  }
  // check if we can send the message as a single buffer
  if (total_size < this->MaxMessageSize)
  {
    Message["type"] = "directbase64";
    Message["points"] = Encode64(Vertices);
    Message["triangles"] = Encode64(Indices);
    if (Normals.has_value())
    {
      Message["normals"] = Encode64(Normals.value());
    }
    if (UVs.has_value())
    {
      Message["texcoords"] = Encode64(UVs.value());
    }
    if (Tangents.has_value())
    {
      Message["tangents"] = Encode64(Tangents.value());
    }
    this->SendJSON(Message);
  }
  else
  {
    bool state = false;
    std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(Vertices.data()), reinterpret_cast<const uint8_t*>(Vertices.data() + Vertices.size()));
    // send the vertices
    do { state = this->SendBuffer(data, "points", "base64"); } while (!state && RetryOnErrorResponse);
    state = false;
    // send the indices
    data = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(Indices.data()), Indices.size() * sizeof(float));
    do { state = this->SendBuffer(data, "triangles", "base64"); } while (!state && RetryOnErrorResponse);
    state = false;
    // send the normals
    if (UVs.has_value())
    {
      data = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(Normals.value().data()), Normals.value().size() * sizeof(float));
      do { state = this->SendBuffer(data, "normals", "base64"); } while (!state && RetryOnErrorResponse);
      state = false;
    }
    // send the UVs
    if (UVs.has_value())
    {
      data = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(UVs.value().data()), UVs.value().size() * sizeof(float));
      do { state = this->SendBuffer(data, "uvs", "base64"); } while (!state && RetryOnErrorResponse);
      state = false;
    }
    // send the tangents
    if (Tangents.has_value())
    {
      data = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(Tangents.value().data()), Tangents.value().size() * sizeof(float));
      do { state = this->SendBuffer(data, "tangents", "base64"); } while (!state && RetryOnErrorResponse);
      state = false;
    }
    if (AutoMessage)
      this->SendJSON({ {"type","spawn"},{"object","ProceduralMeshComponent"} });
  }
}

Synavis::EConnectionState Synavis::DataConnector::GetState()
{
  return state_;
}

void Synavis::DataConnector::SetDataCallback(std::function<void(rtc::binary)> Callback)
{
  this->DataReceptionCallback = Callback;
}

void Synavis::DataConnector::SetMessageCallback(std::function<void(std::string)> Callback)
{
  this->MessageReceptionCallback = Callback;
}

void Synavis::DataConnector::SetConfigFile(std::string ConfigFile)
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

void Synavis::DataConnector::SetConfig(json Config)
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
    lconnector(ELogVerbosity::Error) << "Config is missing required values" << std::endl;
    lconnector(ELogVerbosity::Error) << "Required values are: " << std::endl;
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

bool Synavis::DataConnector::IsRunning()
{
  // returns true if the connection is in a state where it can send and receive data
  return state_ < EConnectionState::CLOSED || SignallingServer->isOpen();

}

// a method that outputs data channel information
void Synavis::DataConnector::PrintCommunicationData()
{
  auto max_message = this->MaxMessageSize;
  if (this->DataChannels.empty())
  {
    lconnector(ELogVerbosity::Info) << "No DataChannels present" << std::endl;
    return;
  }
  for (auto &dc : this->DataChannels)
  {
    if (!dc) continue;
    auto protocol = dc->protocol();
    auto label = dc->label();
    lconnector(ELogVerbosity::Info) << "Data Channel " << label << " has protocol " << protocol << " and max message size " << max_message << std::endl;
  }
}

void Synavis::DataConnector::LockUntilConnected(unsigned additional_wait)
{
  while (state_ < EConnectionState::CONNECTED)
  {
    std::this_thread::yield();
  }
  if (additional_wait > 0)
    std::this_thread::sleep_for(std::chrono::milliseconds(additional_wait));
}

void Synavis::DataConnector::CommunicateSDPs()
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
      //json ice = { {"type","iceCandidate"}, {"candidate", {{"candidate",candidate.candidate()}, {"sdpMid",candidate.mid()}, {"sdpMLineIndex",std::stoi(candidate.mid())}}} };
      std::string ice = "{\"type\":\"iceCandidate\",\"candidate\":{\"candidate\":\"" + candidate.candidate() + "\",\"sdpMid\":\"" + candidate.mid() + "\",\"sdpMLineIndex\":" + std::to_string(std::stoi(candidate.mid())) + "}}";
      SignallingServer->send(ice);
    }
  }
}

void Synavis::DataConnector::WriteSDPsToFile(std::string Filename)
{
  lconnector(ELogVerbosity::Debug) << "Set Writing SDPs to file; Note that you need to call this function AFTER setting the RemoteInformation Callback." << std::endl;
  this->OnRemoteDescriptionCallback = [f_ = this->OnRemoteDescriptionCallback, Filename](std::string sdp)
    {
      std::ofstream file(Filename);
      file << sdp;
      file.close();
      if (f_.has_value())
        f_.value()(sdp);
    };
}

void Synavis::DataConnector::exp__DeactivateCallbacks()
{
  MessageReceptionCallback = std::nullopt;
  lconnector(ELogVerbosity::Warning) << "Deactivating experimental message reception also clears callback" << std::endl;
}

inline void Synavis::DataConnector::DataChannelMessageHandling(rtc::message_variant messageordata)
{
  if (std::holds_alternative<rtc::binary>(messageordata))
  {
    auto data = std::get<rtc::binary>(messageordata);
    lconnector(ELogVerbosity::Verbose) << "Binary frame received of size " << data.size() << std::endl;
    if (DataReceptionCallback.has_value())
      DataReceptionCallback.value()(data);
    return;
  }

  // Text frame: treat as JSON or plain message
  auto message = std::get<std::string>(messageordata);
  lconnector(ELogVerbosity::Verbose) << "Text frame received of size " << message.size() << std::endl;
  if (MessageReceptionCallback.has_value())
  {
    // Forward raw text message; higher layers can parse JSON if desired
    MessageReceptionCallback.value()(message);
  }
}

inline void Synavis::DataConnector::RegisterRemoteCandidate(const json& content)
{
  // {"type": "iceCandidate", "candidate": {"candidate": "candidate:1 1 UDP 2122317823 172.26.15.227 42835 typ host", "sdpMLineIndex": "0", "sdpMid": "0"}}
  lconnector(ELogVerbosity::Debug) << "Parsing ice candidate" << std::endl;
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
    lconnector(ELogVerbosity::Warning) << "Could not parse candidate: " << e.what() << std::endl;
    return;
  }
  lconnector(ELogVerbosity::Debug) << "I received a candidate for " << sdpMid << " with index " << sdpMLineIndex << " and candidate " << candidate_string << std::endl;
  rtc::Candidate ice(candidate_string, sdpMid);
  try
  {
    PeerConnection->addRemoteCandidate(ice);
    lconnector(ELogVerbosity::Debug) << "Added remote candidate" << std::endl;
    // remove from required candidates if succeeded
    RequiredCandidate.erase(std::remove_if(RequiredCandidate.begin(), RequiredCandidate.end(), [sdpMid](auto s) {return s == sdpMid; }), RequiredCandidate.end());
  }
  catch (std::exception e)
  {
    lconnector(ELogVerbosity::Error) << "Could not add remote candidate: " << e.what() << std::endl;
    lconnector(ELogVerbosity::Debug) << "Candidate was: " << ice << std::endl;
  }

}

void Synavis::DataConnector::Initialize()
{
  if (IP.has_value()) rtcconfig_.bindAddress = IP.value();
  if (PortRange.has_value())
  {
    rtcconfig_.portRangeBegin = PortRange.value().first;
    rtcconfig_.portRangeEnd = PortRange.value().second;
  }
  rtcconfig_.enableIceTcp = false;
  rtcconfig_.portRangeBegin = 5000;
  rtcconfig_.portRangeEnd = 6000;
  PeerConnection = std::make_shared<rtc::PeerConnection>(rtcconfig_);
  PeerConnection->onGatheringStateChange([this](auto state)
    {
      lconnector(ELogVerbosity::Info) << "Gathering state changed" << state << std::endl;
      switch (state)
      {
      case rtc::PeerConnection::GatheringState::Complete:
        lconnector(ELogVerbosity::Info) << "State switched to complete" << std::endl;
        break;
      case rtc::PeerConnection::GatheringState::InProgress:
        lconnector(ELogVerbosity::Info) << "State switched to in progress" << std::endl;
        break;
      case rtc::PeerConnection::GatheringState::New:
        lconnector(ELogVerbosity::Info) << "State switched to new connection" << std::endl;
        break;
      }
    });
  PeerConnection->onLocalCandidate([this](auto candidate)
    {
      //json ice_message = { {"type","iceCandidate"},
      //  {"candidate", {{"candidate", candidate.candidate()},
      //                     "sdpMid", candidate.mid()}} };
      //SignallingServer->send(ice_message.dump());
    });
  PeerConnection->onDataChannel([this](auto datachannel)
    {
      lconnector(ELogVerbosity::Warning) << "I received a channel called " << datachannel->label() << std::endl;
      lconnector(ELogVerbosity::Info) << "Remote DataChannel label: " << datachannel->label() << std::endl;
      // assign the received channel and setup handlers
      this->DataChannels.push_back(datachannel);
      this->SetupDataChannelHandlers(datachannel);
    });
  PeerConnection->onTrack([this](auto track)
    {
      lconnector(ELogVerbosity::Warning) << "I received a track I did not ask for" << std::endl;
      track->onOpen([this, track]()
        {
          lconnector(ELogVerbosity::Info) << "Track connection is setup!" << std::endl;
          //track->send(rtc::binary({ (std::byte)(EClientMessageType::QualityControlOwnership) }));
          track->send(rtc::binary({ std::byte{72},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0} }));
        });
    });
  PeerConnection->onSignalingStateChange([this](auto state)
    {
      lconnector(ELogVerbosity::Info) << "SS State changed: " << state << std::endl;
    });
  PeerConnection->onStateChange([this](rtc::PeerConnection::State state)
    {
      lconnector(ELogVerbosity::Info) << "State changed: " << state << std::endl;
      if (state == rtc::PeerConnection::State::Failed && OnFailedCallback.has_value())
      {
        OnFailedCallback.value()();
      }
    });
  SignallingServer = std::make_shared<rtc::WebSocket>();

  // Only create an outgoing data channel if we're configured to take the first step
  if (TakeFirstStep)
  {
    auto ch = PeerConnection->createDataChannel("DataConnectionChannel");
    if (ch)
    {
      this->DataChannels.push_back(ch);
      lconnector(ELogVerbosity::Info) << "Created local DataChannel with label " << ch->label() << std::endl;
      this->SetupDataChannelHandlers(ch);
    }
  }
  SignallingServer->onOpen([this]()
    {
      state_ = EConnectionState::SIGNUP;
      lconnector(ELogVerbosity::Info) << "Signalling server connected" << std::endl;
      if (this->OnSignallingServerOnlineCallback.has_value())
      {
        this->OnSignallingServerOnlineCallback.value()();
      }
      if (TakeFirstStep)
      {
        json role_request = { {"type","request"},{"request","role"} };
        lconnector(ELogVerbosity::Info) << "Attempting to prompt for role, this will fail if the server is not configured to do so" << std::endl;
        //SignallingServer->send(role_request.dump());
      }
      if (TakeFirstStep && PeerConnection->localDescription().has_value())
      {
        json offer = { {"type","offer"}, {"endpoint", "data"},{"sdp",PeerConnection->localDescription().value()} };
        if (PeerConnection->hasMedia())
        {
          lconnector(ELogVerbosity::Info) << "PeerConnection has Media!" << std::endl;
        }
        else
        {
          lconnector(ELogVerbosity::Info) << "PeerConnection has no Media!" << std::endl;
        }
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
          lconnector(ELogVerbosity::Error) << "Could not read package:" << e.what() << std::endl;
        }
        lconnector(ELogVerbosity::Debug) << "I received a message of type: " << content["type"] << std::endl;
        if (content["type"] == "answer" || content["type"] == "offer")
        {
          lconnector(ELogVerbosity::Debug) << "Received an " << content["type"] << " from the server" << std::endl;
          std::string sdp = content["sdp"].get<std::string>();
          std::string type = content["type"].get<std::string>();
          rtc::Description remote(sdp, type);
          if (OnRemoteDescriptionCallback.has_value())
            OnRemoteDescriptionCallback.value()(sdp);
          if (content["type"] == "answer" && TakeFirstStep)
            PeerConnection->setRemoteDescription(remote);
          else if (content["type"] == "offer" && !TakeFirstStep)
          {
            // We received an offer and are configured to answer. Set the remote
            // description first, then explicitly create and set an answer. Avoid
            // calling the no-arg setLocalDescription which lets the library guess
            // (and can create an offer in races).
            PeerConnection->setRemoteDescription(remote);

            // If the remote description contains inline candidates (non-trickle
            // ICE), extract and add them explicitly so we don't rely solely on
            // separate "iceCandidate" messages from the signalling server.
            try
            {
              for (auto cand : remote.extractCandidates())
              {
                try
                {
                  PeerConnection->addRemoteCandidate(cand);
                  lconnector(ELogVerbosity::Debug) << "Added candidate from remote SDP: " << cand << std::endl;
                }
                catch (const std::exception& e)
                {
                  lconnector(ELogVerbosity::Warning) << "Failed to add candidate from remote SDP: " << e.what() << std::endl;
                }
              }
            }
            catch (const std::exception&)
            {
              // If extractCandidates is not supported or fails, continue; we
              // will still accept separate iceCandidate messages.
            }

            // Only attempt to create/set a local answer if we don't already
            // have a local description. This avoids races where repeated
            // offers or earlier actions already produced a local answer/offer.
            if (PeerConnection->localDescription().has_value())
            {
              lconnector(ELogVerbosity::Info) << "Local description already present; skipping create local Answer" << std::endl;
            }
            else
            {
              try
              {
                // Explicitly request the library to create and set a local Answer.
                PeerConnection->setLocalDescription(rtc::Description::Type::Answer);

                // Log the type and DTLS setup line for diagnostics
                if (PeerConnection->localDescription().has_value())
                {
                  std::string localSdp = PeerConnection->localDescription().value();
                  auto pos = localSdp.find("a=setup:");
                  if (pos != std::string::npos)
                  {
                    auto end = localSdp.find('\n', pos);
                    std::string setupLine = localSdp.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
                    lconnector(ELogVerbosity::Info) << "Created explicit answer; local a=setup line: " << setupLine << std::endl;
                  }
                  else
                  {
                    lconnector(ELogVerbosity::Info) << "Created explicit answer; no a=setup line found in local SDP" << std::endl;
                  }
                }
                else
                {
                  lconnector(ELogVerbosity::Warning) << "Created answer but localDescription() is not available" << std::endl;
                }
              }
              catch (const std::exception& e)
              {
                lconnector(ELogVerbosity::Error) << "Failed to create/set explicit answer: " << e.what() << std::endl;
              }
            }

            SubmissionHandler.AddTask(std::bind(&DataConnector::CommunicateSDPs, this));
          }
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
            auto& l = lconnector(ELogVerbosity::Info) << "I have " << RequiredCandidate.size() << " required candidates: ";
            for (auto i = 0; i < RequiredCandidate.size(); ++i)
            {
              l << RequiredCandidate[i] << " ";
            }
            l << std::endl;
            InitializedRemote = true;
            if (EarlyMessages.size() > 0)
            {
              RegisterRemoteCandidate(content);
              // if we have no more required candidates, we can send an answer
              if (RequiredCandidate.size() == 0)
              {
                lconnector(ELogVerbosity::Info) << "I have received all required candidates" << std::endl;
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
                auto& l = lconnector(ELogVerbosity::Debug) << "I still have " << RequiredCandidate.size() << " required candidates: ";
                for (auto i = 0; i < RequiredCandidate.size(); ++i)
                {
                  l << RequiredCandidate[i] << " ";
                }
                l << std::endl;
              }
            }
          }
        }
        else if (content["type"] == "iceCandidate")
        {
          if (!InitializedRemote)
          {
            EarlyMessages.push_back(content);
            lconnector(ELogVerbosity::Debug) << "Storing ICE for after remote was initialized" << std::endl;
          }
          else
          {
            RegisterRemoteCandidate(content);
            // if we have no more required candidates, we can send an answer
            if (RequiredCandidate.size() == 0)
            {
              lconnector(ELogVerbosity::Info) << "I have received all required candidates" << std::endl;
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
              auto& l = lconnector(ELogVerbosity::Debug) << "I still have " << RequiredCandidate.size() << " required candidates: ";
              for (auto i = 0; i < RequiredCandidate.size(); ++i)
              {
                l << RequiredCandidate[i] << " ";
              }
              l << std::endl;
            }
          }
        }
        else if (content["type"] == "control")
        {
          lconnector(ELogVerbosity::Debug) << "Received a control message: " << content["message"] << std::endl;
        }
        else if (content["type"] == "id")
        {
          this->config_["id"] = content["id"];
          lconnector(ELogVerbosity::Info) << "Received an id: " << content["id"] << std::endl;
        }
        else if (content["type"] == "serverDisconnected")
        {
          PeerConnection.reset();
          lconnector(ELogVerbosity::Warning) << "Reset peer connection because we received a disconnect" << std::endl;
        }
        else if (content["type"] == "config")
        {
          auto pc_options = content["peerConnectionOptions"];
          // TODO: Set peer connection options
        }
        else if (content["type"] == "playerCount")
        {
          lconnector(ELogVerbosity::Info) << "There are " << content["count"] << " players connected" << std::endl;
        }
        else if (content["type"] == "role")
        {
          lconnector(ELogVerbosity::Info) << "Received a role: " << content["role"] << std::endl;
          if (content["role"] == "server")
          {
            this->IsServer = true;
            // If we are configured to take the first step, explicitly create an
            // offer and set it as the local description. Avoid the no-arg
            // setLocalDescription() which can let the library guess and cause
            // dual-offer races.
            if (TakeFirstStep)
            {
              try
              {
                // Explicitly request the library to create and set a local Offer.
                PeerConnection->setLocalDescription(rtc::Description::Type::Offer);
                if (PeerConnection->localDescription().has_value())
                {
                  std::string localSdp = PeerConnection->localDescription().value();
                  auto pos = localSdp.find("a=setup:");
                  if (pos != std::string::npos)
                  {
                    auto end = localSdp.find('\n', pos);
                    std::string setupLine = localSdp.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
                    lconnector(ELogVerbosity::Info) << "Created explicit offer; local a=setup line: " << setupLine << std::endl;
                  }
                  else
                  {
                    lconnector(ELogVerbosity::Info) << "Created explicit offer; no a=setup line found in local SDP" << std::endl;
                  }
                }
                else
                {
                  lconnector(ELogVerbosity::Warning) << "Created offer but localDescription() is not available" << std::endl;
                }
              }
              catch (const std::exception& e)
              {
                lconnector(ELogVerbosity::Error) << "Failed to create/set explicit offer: " << e.what() << std::endl;
              }
            }
            else
            {
              lconnector(ELogVerbosity::Info) << "Role=server received but TakeFirstStep==false; deferring to remote offer." << std::endl;
            }
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
          lconnector(ELogVerbosity::Warning) << "Resetting connection because we must be in server role and the player disconnected" << std::endl;
        }
        else
        {
          lconnector(ELogVerbosity::Warning) << "unknown message?" << std::endl << content.dump() << std::endl;
        }
      }
    });
  SignallingServer->onClosed([this]()
    {
      state_ = EConnectionState::CLOSED;
      auto unix_time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

      lconnector(ELogVerbosity::Info) << "Signalling server was closed at timestamp " << unix_time << std::endl;

    });
  SignallingServer->onError([this](std::string error)
    {
      state_ = EConnectionState::STARTUP;
      SignallingServer->close();
      lconnector(ELogVerbosity::Error) << "Signalling server error: " << error << std::endl;
    });
}
