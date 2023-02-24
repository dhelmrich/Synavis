#ifndef WEBRTCBRIDGE_DATACONNECTOR_HPP
#define WEBRTCBRIDGE_DATACONNECTOR_HPP
#pragma once

#include <json.hpp>
#include <span>
#include <variant>
#include <rtc/rtc.hpp>
#include "WebRTCBridge/export.hpp"

#include "WebRTCBridge.hpp"

namespace WebRTCBridge
{

class WEBRTCBRIDGE_EXPORT DataConnector : public std::enable_shared_from_this<DataConnector>
{
public:
  using json = nlohmann::json;
  DataConnector();
  ~DataConnector();
  void StartSignalling();
  virtual void SendData(rtc::binary Data);
  void SendMessage(std::string Message);
  EConnectionState GetState();
  void SetCallback(std::function<void(rtc::binary)> Callback);
  std::function<void(rtc::binary)> DataReceptionCallback;
  std::shared_ptr<rtc::DataChannel> DataChannel;
  void SetConfig(json Config);
  bool IsRunning();
  void SetTakeFirstStep(bool TakeFirstStep){this->TakeFirstStep=TakeFirstStep;}
  bool GetTakeFirstStep(){return this->TakeFirstStep;}

protected:

  void CommunicateSDPs();

  EConnectionState state_;
  rtc::Configuration rtcconfig_;
  rtc::Configuration webconfig_;
  std::shared_ptr<rtc::PeerConnection> pc_;
  std::shared_ptr<rtc::WebSocket> ss_;
  bool TakeFirstStep = false;
  bool InitializedRemote = false;
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
