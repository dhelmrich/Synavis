#ifndef WEBRTCBRIDGE_FRAMEDECODE_HPP
#define WEBRTCBRIDGE_FRAMEDECODE_HPP

#pragma once

#include <json.hpp>
#include <mutex>
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
  // we allow for the registering of the av_log_set_callback much like we did for the libdatachannel log in the Synavis.hpp header
  void SYNAVIS_EXPORT RegisterAvLogCallback(bool bUseSynavis = false);

  enum class EOutputMode
  {
    Unchanged, // maintain original frame format (e.g. YUV420P with FFmpeg stride)
    PackedRGB // convert to tightly packed RGB24 (no stride, 3 bytes per pixel)
  };

  struct SYNAVIS_EXPORT FrameContent
  {
    std::vector<uint8_t> Data;
    uint32_t Width;
    uint32_t Height;
    uint32_t Timestamp;
    // Pixel format (AVPixelFormat) of the packed data or source frame
    int PixFmt{ -1 };
    // Linesizes for each plane. If Packed==true, these are the tightly-packed strides (W, W/2, W/2 for YUV420P).
    std::array<int,3> Linesize{0,0,0};
    // If true, Data is tightly packed and consumer may assume canonical sizes; otherwise Data contains
    // per-plane rows that include FFmpeg stride/padding and the consumer must use Linesize to read rows.
    bool Packed{false};
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
    bool extended_pid() { return ext_payload & 0b10000000; }
  };
#pragma pack(pop)

  class SYNAVIS_EXPORT PacketDepacketizer
  {
  public:
    PacketDepacketizer() = default;
    virtual ~PacketDepacketizer() = default;

    // this function should be called in sequence order
    // package reception handling is NOT handled here
    // this is purely for depacketizing the data
    virtual void AddPacket(const rtc::binary& Data) = 0;
    virtual bool IsFrameComplete() = 0;
    virtual AVPacket* GetAVFrame() = 0;
    virtual void ResetPacket();
    // Reserve capacity for the upcoming frame to avoid repeated allocations
    void ReserveFrame(size_t Size);
    // Accessors for diagnostics
    size_t FrameCapacity() const;
    size_t FrameSize() const;

  protected:
    uint32_t timestamp { static_cast<uint32_t>(-1) };
    std::vector<std::byte> frame;
  };

  class SYNAVIS_EXPORT VP9Depacketizer : public PacketDepacketizer
  {
  public:
    VP9Depacketizer();
    virtual ~VP9Depacketizer() override;

    virtual void AddPacket(const rtc::binary& Packet) override;
    virtual bool IsFrameComplete() override;
    virtual AVPacket* GetAVFrame() override;
    bool MarkerSeen = false;
    uint16_t PictureId = 0;
    // Diagnostic counters and timers migrated from MediaReceiver depacketizer
    uint32_t NumFragments = 0; // number of RTP fragments appended for current frame
    double LastPacketTime = 0.0; // milliseconds since epoch steady clock
    static constexpr double FRAME_TIMEOUT_MS = 100.0; // flush incomplete frames after 100 ms
  };

  class SYNAVIS_EXPORT H264Depacketizer : public PacketDepacketizer
  {
  public:
    H264Depacketizer() = default;
    virtual ~H264Depacketizer() override;
    virtual void AddPacket(const rtc::binary& Packet) override;
    virtual bool IsFrameComplete() override;
    virtual AVPacket* GetAVFrame() override;

  };

  class SYNAVIS_EXPORT FrameDecode : public std::enable_shared_from_this<FrameDecode>
  {
  public:
    // VideoInfo is optional (not exposed to Python), but codec is mandatory
    FrameDecode(ECodec StreamCodec, rtc::Track* VideoInfo = nullptr);
    virtual ~FrameDecode();

    // Parse a remote media description (as produced by MediaReceiver::RemoteMediaDescription)
    void ParseDescription(const nlohmann::json& desc);

    std::function<bool(rtc::binary, rtc::FrameInfo)> CreateAcceptor(std::function<void(FrameContent)>&& Callback);

    // set a direct frame callback (invoked with decoded FrameContent)
    void SetFrameCallback(std::function<void(FrameContent)>&& Callback)
    {
      FrameCallback = std::move(Callback);
    }

    void SetMaxFrameBuffer(uint32_t MaxFrames);

    // If true, only keyframes (intra frames) are accepted/decoded; all
    // inter-frames are dropped. This is useful when a late joiner or a
    // recovered receiver only wants self-contained frames.
    void SetAcceptOnlyKeyframes(bool b) { AcceptOnlyKeyframes = b; }
    bool GetAcceptOnlyKeyframes() const { return AcceptOnlyKeyframes; }

    EOutputMode OutputMode{EOutputMode::Unchanged};

    // If true, only keyframes (intra frames) are accepted/decoded; all
    // inter-frames are dropped.
    bool AcceptOnlyKeyframes{false};

  private:

    std::optional<std::function<void(FrameContent)>> FrameCallback;
    std::optional<std::function<void(std::variant<rtc::binary, std::string>)>> MessageCallback;

    inline AVPacket* InitializePacketFromData(uint32_t index);

    std::shared_ptr<WorkerThread> DecoderThread;
    uint32_t MaxFrames{64};

    std::map<uint32_t, std::vector<rtc::binary>> frameBuffer;
    std::deque<uint32_t> currentlyCapturing;
    // Protects frameBuffer/currentlyCapturing. AddPacket runs on the RTP receive
    // thread while the decode tasks run on DecoderThread (a separate WorkerThread),
    // so every access to frameBuffer/currentlyCapturing must be serialized under
    // this mutex (use std::lock_guard, never raw lock/unlock).
    mutable std::mutex FrameBufferMutex;

    void AddPacket(const rtc::binary& Data);

    // If AcceptOnlyKeyframes is enabled, decide from a frame's start packet
    // whether it should be accepted. Returns true if the packet may be
    // buffered, false if it (and its frame) must be dropped.
    bool ShouldAcceptPacket(const rtc::RtpHeader* Header, const uint8_t* Body);

    // ffmpeg decoding context
    AVCodecContext* CodecContext;
    const AVCodec* Codec;
    AVFrame* Frame;
    AVPacket* Packet;
    std::unique_ptr<PacketDepacketizer> Depacketizer;

    int ExpectedPayloadType = -1;

    uint64_t MaxMessageSize;
  };
}


#endif //WEBRTCBRIDGE_FRAMEDECODE_AV_HPP
