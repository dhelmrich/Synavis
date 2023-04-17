#ifndef WEBRTC_MEDIA_RECEIVER_HPP.
#define WEBRTC_MEDIA_RECEIVER_HPP
#pragma once

#include "WebRTCBridge/export.hpp"
#include "WebRTCBridge.hpp"
#include "DataConnector.hpp"
#include <json.hpp>

namespace WebRTCBridge
{

// a class that handles the WebRTC connection for a client that receives media
class WEBRTCBRIDGE_EXPORT MediaReceiver : public DataConnector, public std::enable_shared_from_this<MediaReceiver>
{
public:
  using json = nlohmann::json;
  MediaReceiver();
  ~MediaReceiver() override;

  void SetFrameReceptionCallback(std::function<void(rtc::binary)> Callback){FrameReceptionCallback = Callback;}

  void SetOnTrackOpenCallback(std::function<void(void)> Callback){OnTrackOpenCallback = Callback;}

  virtual void PrintCommunicationData() override;

  std::vector<uint8_t> DecodeFrame(rtc::binary Frame);

protected:
  std::shared_ptr<rtc::Track> Track;
  rtc::Description::Video MediaDescription;
  std::shared_ptr<BridgeSocket> FrameRelay;
  std::shared_ptr<rtc::RtcpReceivingSession> RtcpReceivingSession;

  std::optional<std::function<void(rtc::binary)>> FrameReceptionCallback;
  std::optional<std::function<void(void)>> OnTrackOpenCallback;

};

} // namespace WebRTCBridge

#endif

