#ifndef SYNAVIS_MEDIA_RECEIVER_HPP
#define SYNAVIS_MEDIA_RECEIVER_HPP
#pragma once

#include "Synavis/export.hpp"
#include "Synavis.hpp"
#include "DataConnector.hpp"
#include <json.hpp>

#include "rtc/description.hpp"

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

  void SetFrameReceptionCallback(std::function<void(rtc::binary)> Callback)
  {
    FrameReceptionCallback = Callback;
  }

  void SetOnTrackOpenCallback(std::function<void(void)> Callback)
  {
    OnTrackOpenCallback = Callback;
  }

  void ConfigureRelay(std::string IP, int Port);

  virtual void PrintCommunicationData() override;

  std::vector<uint8_t> DecodeFrame(rtc::binary Frame);

  void RequestKeyFrame();

  void SetCodec(ECodec Codec) {this->Codec = Codec;}


protected:
  std::shared_ptr<rtc::Track> Track;
  rtc::Description::Video MediaDescription{"video", rtc::Description::Direction::RecvOnly};
  std::shared_ptr<BridgeSocket> FrameRelay;
  std::shared_ptr<rtc::RtcpReceivingSession> RtcpReceivingSession;

  std::optional<std::function<void(rtc::binary)>> FrameReceptionCallback;
  std::optional<std::function<void(void)>> OnTrackOpenCallback;

  ECodec Codec;

  void MediaHandler(rtc::message_variant DataOrMessage);

};

} // namespace Synavis

#endif

