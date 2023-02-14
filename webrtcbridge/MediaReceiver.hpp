#ifndef WEBRTC_MEDIA_RECEIVER_HPP
#define WEBRTC_MEDIA_RECEIVER_HPP
#pragma once

#include "WebRTCBridge/export.hpp"
#include "WebRTCBridge.hpp"
#include <json.hpp>

// a class that handles the WebRTC connection for a client that receives media
class WEBRTCBRIDGE_EXPORT MediaReceiver : public std::enable_shared_from_this<MediaReceiver>
{
public:
  using json = nlohmann::json;
  MediaReceiver(std::string url, std::string name, int port);
  ~MediaReceiver();
  bool ConnectToSignalingServer();
  int Run();
  std::string SDP();
  void Offer();
  void UseConfig(std::string filename);


};

#endif

