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

  
  // VP9 RTP depacketizer (header-only, inline for PySynavis)
class SYNAVIS_EXPORT Vp9RtpDepacketizer {
public:
  using FrameCallback = std::function<void(rtc::binary)>;

  explicit Vp9RtpDepacketizer(FrameCallback cb = nullptr)
    : OnFrameReady(std::move(cb)) {}

  void SetCallback(FrameCallback cb) { OnFrameReady = std::move(cb); }

  void OnRtpPacket(rtc::binary rtp_packet)
  {
    if (rtp_packet.size() < 13) return;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(rtp_packet.data());
    uint16_t seq = ReadBE16(data + 2);
    uint32_t ssrc = ReadBE32(data + 8);
    uint8_t desc = data[12];
    bool is_start = ((desc >> 2) & 0x01) != 0;
    bool is_end = ((desc >> 1) & 0x01) != 0;
    rtc::binary payload(rtp_packet.begin() + 13, rtp_packet.end());

    auto &asmbl = Assemblers[ssrc];
    asmbl.LastPacketTime = NowMs();

    if (is_start || asmbl.ExpectedSeq == 0 || seq < asmbl.ExpectedSeq || (seq - asmbl.ExpectedSeq > 1000)) {
      // New frame begin (or out-of-order reset)
      if (!asmbl.FrameData.empty() && asmbl.ExpectingMore) {
        // dropped partial frame
      }
      asmbl.FrameData.clear();
      asmbl.ExpectedSeq = seq;
      asmbl.ExpectingMore = true;
    }

    // append payload
    asmbl.FrameData.insert(asmbl.FrameData.end(), payload.begin(), payload.end());
    asmbl.ExpectedSeq = static_cast<uint16_t>(seq + 1);

    if (is_end) {
      if (OnFrameReady) {
        try { OnFrameReady(std::move(asmbl.FrameData)); }
        catch (...) {}
      }
      asmbl.FrameData.clear();
      asmbl.ExpectingMore = false;
    }

    // timeout flush
    if ((NowMs() - asmbl.LastPacketTime) > (FRAME_TIMEOUT_SEC * 1000.0)) {
      if (!asmbl.FrameData.empty()) {
        // timeout: drop partial
        asmbl.FrameData.clear();
      }
    }
  }

private:
  struct FrameAssembler {
    rtc::binary FrameData;
    uint16_t ExpectedSeq = 0;
    uint8_t PictureId = 0;
    bool ExpectingMore = true;
    double LastPacketTime = 0.0;
  };

  std::unordered_map<uint32_t, FrameAssembler> Assemblers; // SSRC -> assembler
  FrameCallback OnFrameReady;
  static constexpr double FRAME_TIMEOUT_SEC = 0.100; // seconds

  static uint32_t ReadBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
  }
  static uint16_t ReadBE16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
  }
  static double NowMs()
  {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
};

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

  auto GetFrameReceptionCallback(){ return this->FrameReceptionCallback; }

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


protected:
  std::vector<std::shared_ptr<rtc::Track>> theirTracks;
  rtc::Description::Video MediaDescription{"video", rtc::Description::Direction::RecvOnly};
  std::shared_ptr<BridgeSocket> FrameRelay;
  std::shared_ptr<rtc::RtcpReceivingSession> RtcpReceivingSession;
  std::shared_ptr<rtc::MediaHandler> BaseMediaHandler;

  std::optional<std::function<void(rtc::binary)>> FrameReceptionCallback;
  std::optional<std::function<void(void)>> OnTrackOpenCallback;
  std::optional<std::function<void(void)>> OnTrackCloseCallback;

  ECodec Codec;

  void MediaHandler(rtc::message_variant DataOrMessage);

  // allow Python (PySynavis) to set a VP9 frame callback
  void SetVp9FrameCallback(std::function<void(rtc::binary)> cb);

private:

  Vp9RtpDepacketizer Vp9Depacketizer;

};

} // namespace Synavis

#endif

