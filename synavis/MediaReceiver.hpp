#ifndef SYNAVIS_MEDIA_RECEIVER_HPP
#define SYNAVIS_MEDIA_RECEIVER_HPP
#pragma once

#include "Synavis/export.hpp"
#include "Synavis.hpp"
#include "DataConnector.hpp"
#include <json.hpp>

#include "rtc/description.hpp"
#include "rtc/rtcpreceivingsession.hpp"
#include "rtc/track.hpp"
#include "rtc/rtc.hpp"
#include <chrono>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace Synavis
{

  
  

// a class that handles the WebRTC connection for a client that receives media
class SYNAVIS_EXPORT MediaReceiver : public DataConnector, public std::enable_shared_from_this<MediaReceiver>
{
public:
  using json = nlohmann::json;
  MediaReceiver();
  ~MediaReceiver() override;
  virtual void Initialize() override;

  void SetFrameReceptionCallback(std::function<bool(rtc::binary, rtc::FrameInfo)> Callback)
  {
    FrameReceptionCallback = Callback;
  }

  auto GetFrameReceptionCallback()
  {
    return this->FrameReceptionCallback;
  }

  void SetOnTrackOpenCallback(std::function<void(void)> Callback)
  {
    OnTrackOpenCallback = Callback;
  }

  void SetOnTrackCloseCallback(std::function<void(void)> Callback)
  {
    OnTrackCloseCallback = Callback;
  }

  void ConfigureRelay(std::string IP, int Port);

  virtual void PrintCommunicationData() override;

  void RequestKeyFrame();
  void SendMouseClick();
  void StartStreaming();
  void StopStreaming();
  void SetCodec(ECodec Codec) {this->Codec = Codec;}

  // Remote media introspection for external consumers
  json RemoteMediaDescription(int id);
  int NumRemoteMedia();

protected:
  std::vector<std::shared_ptr<rtc::Track>> theirTracks;
  std::vector<std::shared_ptr<rtc::RtcpReceivingSession>> theirRtcpSessions;
  rtc::Description::Video MediaDescription{"video", rtc::Description::Direction::RecvOnly};
  std::shared_ptr<BridgeSocket> FrameRelay;

  std::optional<std::function<bool(rtc::binary, rtc::FrameInfo)>> FrameReceptionCallback;
  std::optional<std::function<void(std::variant<rtc::binary, std::string>)>> MessageCallback;
  std::optional<std::function<void(void)>> OnTrackOpenCallback;
  std::optional<std::function<void(void)>> OnTrackCloseCallback;

  ECodec Codec;

  void MessageHandler(rtc::message_variant DataOrMessage);
  void FrameHandler(rtc::binary FrameData, rtc::FrameInfo Info);


private:


};

} // namespace Synavis

#endif

