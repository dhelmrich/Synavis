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

// ensure that we are using a version of libavcodec that supports decoding VP9
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100) \
#error "libavcodec is too old, update to a newer version"
#endif


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
