#ifndef WEBRTCBRIDGE_FRAMEDECODE_HPP
#define WEBRTCBRIDGE_FRAMEDECODE_HPP

#pragma once

#include <json.hpp>
#include <span>
#include <variant>
#include <vector>
#include <rtc/rtc.hpp>
#include "Synavis/export.hpp"

#include "Synavis.hpp"




// forward declaration of the AVCodecContext struct
struct AVCodecContext;
struct AVCodec;
struct AVFrame;
struct AVPacket;

namespace rtc
{
  class Track;
}


namespace Synavis
{

  struct SYNAVIS_EXPORT FrameContent
  {
    std::vector<uint8_t> Data;
    uint32_t Width;
    uint32_t Height;
    uint32_t Timestamp;
  };

#pragma pack(push, 1)
  struct SYNAVIS_EXPORT VP9Payload
  {
    uint8_t payload;
    bool picture_id_present() { return payload & 0b10000000; }
    bool inter_pic_predicted() { return payload & 0b01000000; }
    bool layer_idx() { return payload & 0b00100000; }
    bool flexible() { return payload & 0b00010000; }
    bool start() { return payload & 0b00001000; }
    bool end() { return payload & 0b00000100; }
    bool scalability() { return payload & 0b00000010; }
    bool reserved() { return payload & 0b00000001; }
    uint8_t ext_payload;
    bool extended_pid() {return ext_payload & 0b10000000; }
  };
#pragma pack(pop)

  class SYNAVIS_EXPORT VP9Depacketizer
  {
    public:
    VP9Depacketizer();
    ~VP9Depacketizer();

    std::vector<std::byte> frame;
    void AddPacket(rtc::binary Data);
    bool IsFrameComplete();
  private:
    uint32_t timestamp;
  };

  class SYNAVIS_EXPORT FrameDecode : public std::enable_shared_from_this<FrameDecode>
  {
  public:
    FrameDecode(rtc::Track* VideoInfo = nullptr);
    virtual ~FrameDecode();

    std::function<void(rtc::binary)> CreateAcceptor(std::function<void(rtc::binary)>&& Callback);

    void SetFrameCallback(std::function<void(FrameContent)> Callback);

    void SetMaxFrameBuffer(uint32_t MaxFrames);

  private:

    std::optional<std::function<void(FrameContent)>> FrameCallback;

    inline AVPacket* InitializePacketFromData(uint32_t index);

    std::shared_ptr<WorkerThread> DecoderThread;
    uint32_t MaxFrames;
     
    std::map<uint32_t, std::vector<rtc::binary>> frameBuffer;
    std::deque<uint32_t> currentlyCapturing;

    void AddPacket(rtc::binary Data);

    // ffmpeg decoding context
    AVCodecContext* CodecContext;
    const AVCodec* Codec;
    AVFrame* Frame;
    AVPacket* Packet;

    uint64_t MaxMessageSize;
  };
}


#endif //WEBRTCBRIDGE_FRAMEDECODE_AV_HPP
