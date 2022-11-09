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

class WEBRTCBRIDGE_EXPORT DataConnector : std::enable_shared_from_this<DataConnector>
{
public:
  using json = nlohmann::json;

  DataConnector();
  ~DataConnector();
  
  void SendData(rtc::binary Data);
  void SendMessage(std::string Message);

  std::function<void(rtc::binary)> DataReceptionCallback;
  std::shared_ptr<rtc::DataChannel> DataChannel;

protected:

  rtc::Configuration rtcconfig_;
  std::shared_ptr<rtc::PeerConnection> pc_;
  rtc::WebSocket ss_;
  unsigned int MessagesReceived{ 0 };
  json config_;

};


}
#endif
