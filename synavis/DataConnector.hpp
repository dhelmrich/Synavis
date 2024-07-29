#ifndef WEBRTCBRIDGE_DATACONNECTOR_HPP
#define WEBRTCBRIDGE_DATACONNECTOR_HPP
#pragma once

#include <json.hpp>
#include <span>
#include <variant>
#include <rtc/rtc.hpp>
#include "Synavis/export.hpp"

#include "Synavis.hpp"
#include <rtc/peerconnection.hpp>
#include <rtc/datachannel.hpp>
#include <rtc/configuration.hpp>

namespace Synavis
{

class SYNAVIS_EXPORT DataConnector : public std::enable_shared_from_this<DataConnector>
{
public:
  using json = nlohmann::json;
  std::string Prefix = "";
  DataConnector();
  virtual ~DataConnector();
  virtual void Initialize();
  void StartSignalling();

  virtual void SendData(rtc::binary Data);
  void SendString(std::string Message);
  void SendJSON(json Message);
  bool SendBuffer(const std::span<const uint8_t>& Buffer, std::string Name, std::string Format = "raw");
  bool SendFloat64Buffer(const std::vector<double>& Buffer, std::string Name, std::string Format = "raw");
  bool SendFloat32Buffer(const std::vector<float>& Buffer, std::string Name, std::string Format = "raw");
  bool SendInt32Buffer(const std::vector<int32_t>& Buffer, std::string Name, std::string Format = "raw");
  void SendGeometry(const std::vector<double>& Vertices, const std::vector<uint32_t>& Indices, std::string Name, std::optional<std::vector<double>> Normals = std::nullopt, 
                    std::optional<std::vector<double>> UVs = std::nullopt, std::optional<std::vector<double>> Tangents = std::nullopt, bool AutoMessage = true);
  EConnectionState GetState();
  std::optional<std::function<void(rtc::binary)>> DataReceptionCallback;
  std::optional<std::function<void(std::string)>> MessageReceptionCallback;
  std::optional<std::string> IP {std::nullopt};
  std::optional<std::pair<int, int>> PortRange {std::nullopt};


  void SetDataCallback(std::function<void(rtc::binary)> Callback);
  void SetMessageCallback(std::function<void(std::string)> Callback);
  auto GetMessageCallback() { return MessageReceptionCallback; }
  auto GetDataCallback() { return DataReceptionCallback; }
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
  void SetOnRemoteDescriptionCallback(std::function<void(std::string)> Callback) { OnRemoteDescriptionCallback = Callback; }
  void SetOnDataChannelAvailableCallback(std::function<void(void)> Callback) { OnDataChannelAvailableCallback = Callback; }
  void SetRetryOnErrorResponse(bool Retry) { RetryOnErrorResponse = Retry; }

  void LockUntilConnected(unsigned additional_wait = 0);

  /**
   * \brief Set the DontWaitForAnswer flag. If set to true, the DataConnector
   * will not wait for an answer from the other side.
   * \param DontWait 
   */
  void SetDontWaitForAnswer(bool DontWait) { DontWaitForAnswer = DontWait; }
  void SetTimeOut(double TimeOut) { this->TimeOut = TimeOut; }

  /**
   * \brief Sets the geometry transmission behavior to fail if the transmission
   * could not be completed. This will raise an error, otherwise the transmission
   * is just discarded.
   * \param Fail 
   */
  void SetFailIfNotComplete(bool Fail) { FailIfNotComplete = Fail; }
  void CommunicateSDPs();
  void WriteSDPsToFile(std::string Filename);
  void SetLogVerbosity(ELogVerbosity Verbosity) { LogVerbosity = Verbosity; }

  // webrtc settings
  void SetIPForICE(std::string IP) { rtcconfig_.bindAddress = IP; }
  void SetPortRangeForICE(uint16_t Min, uint16_t Max) { rtcconfig_.portRangeBegin = Min; rtcconfig_.portRangeBegin = Max; }

  /**
   * brief Sets a message callback that is called when a message is received.
   * This is experimental with the intention to replace the MessageReceptionCallback
   * \param Callback
   */
  void exp__PushMessageCallback(auto Callback) { exp__OnMessagecallbacks.push_back(Callback); }
  void exp__ClearMessageCallbacks() { exp__OnMessagecallbacks.clear(); }
  void exp__ActivateCallbacks()
  {
    MessageReceptionCallback = [this](std::string Message)
    {
      for (auto& Callback : exp__OnMessagecallbacks)
      {
        Callback(Message);
      }
    };
  }
  void exp__DeactivateCallbacks();

protected:
  /**
   * Callbacks for additional custom behavior
   **/
  std::optional<std::function<void(void)>> OnConnectedCallback;
  std::optional<std::function<void(void)>> OnFailedCallback;
  std::optional<std::function<void(void)>> OnClosedCallback;
  std::optional<std::function<void(void)>> OnIceGatheringFinished;
  std::optional<std::function<void(std::string)>> OnRemoteDescriptionCallback;
  std::optional<std::function<void(void)>> OnDataChannelAvailableCallback;

  // mark as experimental; data channel message handling with many callbacks
  std::deque<std::function<void(std::string)>> exp__OnMessagecallbacks;

  inline void DataChannelMessageHandling(rtc::message_variant Data);

  inline void RegisterRemoteCandidate(const json& content);

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
  bool RetryOnErrorResponse = false;
  bool DontWaitForAnswer = false;
  double TimeOut = 10.0;
  unsigned int MessagesReceived{ 0 };
  std::size_t MaxMessageSize{ static_cast<std::size_t>(-1) };
  std::vector<std::string> RequiredCandidate;
  json config_{
    {"SignallingIP", int()},
    {"SignallingPort",int()}
  };
  WorkerThread SubmissionHandler;

  // Communication queue to allow for UE to retain the correct state
  std::deque<json> EarlyMessages;
};

}
#endif
