#ifndef WEBRTCBRIDGE_DATACONNECTOR_HPP
#define WEBRTCBRIDGE_DATACONNECTOR_HPP
#pragma once

#include <json.hpp>
#include <span>
#include <variant>
#include <rtc/rtc.hpp>
#include "WebRTCBridge/export.hpp"

#include "WebRTCBridge.hpp"
#include <rtc/peerconnection.hpp>
#include <rtc/datachannel.hpp>
#include <rtc/configuration.hpp>

namespace WebRTCBridge
{

class WEBRTCBRIDGE_EXPORT DataConnector : public std::enable_shared_from_this<DataConnector>
{
public:
  using json = nlohmann::json;
  std::string Prefix = "";
  DataConnector();
  virtual ~DataConnector();
  void StartSignalling();

  virtual void SendData(rtc::binary Data);
  void SendString(std::string Message);
  void SendJSON(json Message);
  void SendBuffer(const std::span<const uint8_t>& Buffer, std::string Name, std::string Format = "raw");
  void SendFloat64Buffer(const std::vector<double>& Buffer, std::string Name, std::string Format = "raw");
  void SendFloat32Buffer(const std::vector<float>& Buffer, std::string Name, std::string Format = "raw");
  void SendInt32Buffer(const std::vector<int32_t>& Buffer, std::string Name, std::string Format = "raw");
  void SendGeometry(const std::vector<double>& Vertices, const std::vector<uint32_t>& Indices, const std::vector<double>& Normals, std::string Name,
                    std::optional<std::vector<double>> UVs = std::nullopt, std::optional<std::vector<double>> Tangents = std::nullopt);
  EConnectionState GetState();
  std::optional<std::function<void(rtc::binary)>> DataReceptionCallback;
  std::optional<std::function<void(std::string)>> MessageReceptionCallback;
  void SetDataCallback(std::function<void(rtc::binary)> Callback);
  void SetMessageCallback(std::function<void(std::string)> Callback);
  std::shared_ptr<rtc::DataChannel> DataChannel;
  void SetConfigFile(std::string ConfigFile);
  void SetConfig(json Config);
  bool IsRunning();
  void SetTakeFirstStep(bool TakeFirstStep){this->TakeFirstStep=TakeFirstStep;}
  bool GetTakeFirstStep(){return this->TakeFirstStep;}
  virtual void PrintCommunicationData();
  void SetBlock(bool Block){this->Block=Block;}
  bool IsBlocking() const {return this->Block;}
  std::byte DataChannelByte{ 50 };
  void SetOnConnectedCallback(std::function<void(void)> Callback) { OnConnectedCallback = Callback; }
  void SetOnFailedCallback(std::function<void(void)> Callback) { OnFailedCallback = Callback; }
  void SetOnClosedCallback(std::function<void(void)> Callback) { OnClosedCallback = Callback; }
  void SetOnIceGatheringFinished(std::function<void(void)> Callback) { OnIceGatheringFinished = Callback; }
  void CommunicateSDPs();

  void SetLogVerbosity(ELogVerbosity Verbosity) { LogVerbosity = Verbosity; }

protected:

  /**
   * Callbacks for additional custom behavior
   **/
  std::optional<std::function<void(void)>> OnConnectedCallback;
  std::optional<std::function<void(void)>> OnFailedCallback;
  std::optional<std::function<void(void)>> OnClosedCallback;
  std::optional<std::function<void(void)>> OnIceGatheringFinished;

  ELogVerbosity LogVerbosity = ELogVerbosity::Warning;

  EConnectionState state_;

  rtc::Configuration rtcconfig_;
  rtc::Configuration webconfig_;
  std::shared_ptr<rtc::PeerConnection> PeerConnection;
  std::shared_ptr<rtc::WebSocket> SignallingServer;
  bool TakeFirstStep = false;
  bool InitializedRemote = false;
  bool IsServer = false;
  bool Block = false;
  bool FailIfNotComplete = false;
  unsigned int MessagesReceived{ 0 };
  std::vector<std::string> RequiredCandidate;
  json config_{
    {"SignallingIP", int()},
    {"SignallingPort",int()}
  };
  WorkerThread SubmissionHandler;
};

}
#endif
