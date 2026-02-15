#include "FrameDecodeAV.hpp"

// libAV includes
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/buffer.h>
// optional, using libswscale for pixel format conversion to RGB
#include <libswscale/swscale.h>
}

#include <sstream>
#include <iomanip>


// ensure that we are using a version of libavcodec that supports decoding VP9
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100) \
  || (LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100) && !defined(AV_CODEC_FLAG_GLOBAL_HEADER))
#error "libavcodec is too old, update to a newer version"
#endif


// global private logger initialization
static std::string Prefix = "FrameDecoder: ";
static const Synavis::Logger::LoggerInstance ldecoder = Synavis::Logger::Get()->LogStarter("FrameDecoder");
static const Synavis::Logger::LoggerInstance lthread = Synavis::Logger::Get()->LogStarter("WorkerThread");
static const Synavis::Logger::LoggerInstance lffmpeg = Synavis::Logger::Get()->LogStarter("FFmpeg");

namespace Synavis
{
  // byte literal
  std::byte operator"" _b(unsigned long long Value)
  {
    return static_cast<std::byte>(Value);
  }

  void PacketDepacketizer::ResetPacket()
  {
    frame.clear();
    timestamp = static_cast<uint32_t>(-1);
  }

  void PacketDepacketizer::ReserveFrame(size_t Size)
  {
    ldecoder(ELogVerbosity::Info) << "PacketDepacketizer: Reserving frame buffer with capacity " << Size << " bytes" << std::endl;
    frame.reserve(Size);
  }

  size_t PacketDepacketizer::FrameCapacity() const
  {
    return frame.capacity();
  }

  size_t PacketDepacketizer::FrameSize() const
  {
    return frame.size();
  }

  VP9Depacketizer::VP9Depacketizer()
  {
    ldecoder(ELogVerbosity::Info) << "VP9Depacketizer constructor: pre-reserving frame buffer to 8000 bytes" << std::endl;
    frame.reserve(8000); // pre-allocate typical MTU size for efficiency
    ldecoder(ELogVerbosity::Info) << "VP9Depacketizer constructor: initial buffer capacity is " << frame.capacity() << " bytes" << std::endl;
  }

  VP9Depacketizer::~VP9Depacketizer()
  {
  }

  void VP9Depacketizer::AddPacket(const rtc::binary& Data)
  {
    const rtc::RtpHeader* Header = reinterpret_cast<const rtc::RtpHeader*>(Data.data());
    size_t headerSize = Header->getSize();
    const uint8_t* body = reinterpret_cast<const uint8_t*>(Header->getBody());
    size_t bodyLen = 0;
    if (Data.size() > headerSize) bodyLen = Data.size() - headerSize;

    if (bodyLen == 0) return;

    // Flush stale partial frame if we've not received packets for longer than timeout
    double now = HighRes();
    if (!frame.empty() && (now - LastPacketTime) > FRAME_TIMEOUT_MS)
    {
      ldecoder(ELogVerbosity::Warning) << "VP9Depacketizer: frame timeout flush for ts=" << timestamp
                                       << " bytes=" << frame.size() << " numfrag=" << NumFragments << std::endl;
      frame.clear();
      MarkerSeen = false;
      NumFragments = 0;
      timestamp = static_cast<uint32_t>(-1);
    }

    // VP9 payload descriptor: first octet at body[0]
    uint8_t desc = body[0];
    bool is_start = (desc & 0x08) != 0; // B bit
    bool is_end = (desc & 0x04) != 0;   // E bit

    // compute how many header bytes follow the initial descriptor
    size_t consumed = 1; // descriptor consumed
    const uint8_t* hdr_ptr = body + 1;
    size_t remaining = (bodyLen > 1) ? bodyLen - 1 : 0;

    uint16_t parsed_pid = 0;
    if (desc & 0x80) // I bit: Picture ID present
    {
      // Picture ID is 7 bits or 15 bits (if high bit of first PID octet set)
      if (remaining < 1) return; // malformed
      uint8_t pid_oct = hdr_ptr[0];
      if (pid_oct & 0x80)
      {
        // 15-bit picture id: need two bytes
        if (remaining < 2) return; // malformed
        parsed_pid = static_cast<uint16_t>(((pid_oct & 0x7F) << 8) | hdr_ptr[1]);
        consumed += 2;
      }
      else
      {
        parsed_pid = static_cast<uint16_t>(pid_oct & 0x7F);
        consumed += 1;
      }
      hdr_ptr = body + consumed;
      remaining = (bodyLen > consumed) ? bodyLen - consumed : 0;
    }

    const std::byte* payloadStart = reinterpret_cast<const std::byte*>(body + consumed);
    size_t payloadLen = remaining;

    if (is_start)
    {
      // start a new frame
      frame.assign(payloadStart, payloadStart + payloadLen);
      timestamp = Header->timestamp();
      MarkerSeen = is_end || (Header->marker() > 0);
      PictureId = parsed_pid;
      NumFragments = 1;
      LastPacketTime = now;
      ldecoder(ELogVerbosity::Verbose) << "VP9Depacketizer: appended payload ssrc=" << Header->ssrc()
                                       << " seq=" << Header->seqNumber() << " numfrag=" << NumFragments
                                       << " total_frame_bytes=" << frame.size() << std::endl;
    }
    else
    {
      // check if the timestamp is the same
      if (Header->timestamp() == timestamp)
      {
        // append the packet payload (skipping descriptor and optional PID)
        frame.insert(frame.end(), payloadStart, payloadStart + payloadLen);
        if (is_end || (Header->marker() > 0)) MarkerSeen = true;
        ++NumFragments;
        LastPacketTime = now;
        ldecoder(ELogVerbosity::Verbose) << "VP9Depacketizer: appended payload ssrc=" << Header->ssrc()
                                         << " seq=" << Header->seqNumber() << " numfrag=" << NumFragments
                                         << " total_frame_bytes=" << frame.size() << std::endl;
        // if PID present, ensure consistency
        if (parsed_pid != 0 && PictureId != 0 && parsed_pid != PictureId)
        {
          ldecoder(ELogVerbosity::Warning) << "VP9Depacketizer: picture id mismatch (expected " << PictureId << " got " << parsed_pid << ")" << std::endl;
        }
      }
      else
      {
        // not the same timestamp - ignore this packet for current assembly
        ldecoder(ELogVerbosity::Verbose) << "Not the same timestamp" << std::endl;
      }
    }
    // If this packet completed the frame, log summary
    if (MarkerSeen)
    {
      ldecoder(ELogVerbosity::Info) << "VP9Depacketizer: complete frame ready ssrc=" << Header->ssrc()
                                    << " bytes=" << frame.size() << " numfrag=" << NumFragments << std::endl;
    }
  }

  bool VP9Depacketizer::IsFrameComplete()
  {
    return (!frame.empty() && MarkerSeen);
  }

  AVPacket* VP9Depacketizer::GetAVFrame()
  {
    if (frame.empty()) return nullptr;
    // Diagnostic: report packetization summary before moving buffer to AVPacket
    ldecoder(ELogVerbosity::Debug) << "VP9Depacketizer: creating AVPacket bytes=" << frame.size() << " numfrag=" << NumFragments << std::endl;
    // Create an AVPacket that references the existing frame buffer without copying.
    // Move the frame vector to the heap and create an AVBufferRef that will delete
    // the vector when the packet is freed.
    auto* heapVec = new std::vector<std::byte>(std::move(frame));
    // frame is now empty; reset markers
    MarkerSeen = false;
    timestamp = static_cast<uint32_t>(-1);
    NumFragments = 0;
    LastPacketTime = 0.0;

    // av_buffer_create expects a uint8_t* data pointer
    uint8_t* dataPtr = reinterpret_cast<uint8_t*>(heapVec->data());
    int dataSize = static_cast<int>(heapVec->size());

    // Free callback will delete the heapVec pointer when buffer is unreferenced
    auto free_cb = [](void* opaque, uint8_t* data) {
      auto* v = static_cast<std::vector<std::byte>*>(opaque);
      delete v;
    };

    AVBufferRef* buf = av_buffer_create(dataPtr, dataSize, free_cb, heapVec, 0);
    if (!buf)
    {
      // cleanup on failure
      delete heapVec;
      return nullptr;
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet)
    {
      av_buffer_unref(&buf);
      return nullptr;
    }
    packet->buf = buf;
    packet->data = buf->data;
    packet->size = dataSize;
    return packet;
  }

  H264Depacketizer::~H264Depacketizer()
  {
  }

  void H264Depacketizer::AddPacket(const rtc::binary& Packet)
  {
    auto header = reinterpret_cast<const rtc::RtpHeader*>(Packet.data());
    auto sq = header->seqNumber();
    auto ts = header->timestamp();
    /*       Wang et al. (2016), RTP Payload format for High Efficiency Video Coding (HEVC)
     *       and Wang, et al. (2011), RTP Payload Format for H.264 Video
     *       Informative note: The first byte of a NAL unit co-serves as the
     *        RTP payload header.
     *
     *       0                   1                   2                   3
     *       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     *      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     *      |F|NRI|  Type   |                                               |
     *      +-+-+-+-+-+-+-+-+                                               |
     *      |                                                               |
     *      |               Bytes 2..n of a single NAL unit                 |
     *      |                                                               |
     *      |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     *      |                               :...OPTIONAL RTP padding        |
     *      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     */
    // remove the rtp header
    auto* data = header->getBody();
    auto size = Packet.size() - header->getSize();

    auto * nal = reinterpret_cast<const rtc::NalUnit*> (data);

    // add the data to the frame
    frame.insert(frame.end(), nal->payload().begin(), nal->payload().end());
  }

  bool H264Depacketizer::IsFrameComplete()
  {
    return false;
  }

  AVPacket* H264Depacketizer::GetAVFrame()
  {
    if (frame.empty()) return nullptr;
    auto* heapVec = new std::vector<std::byte>(std::move(frame));
    uint8_t* dataPtr = reinterpret_cast<uint8_t*>(heapVec->data());
    int dataSize = static_cast<int>(heapVec->size());
    auto free_cb = [](void* opaque, uint8_t* data) {
      auto* v = static_cast<std::vector<std::byte>*>(opaque);
      delete v;
    };
    AVBufferRef* buf = av_buffer_create(dataPtr, dataSize, free_cb, heapVec, 0);
    if (!buf)
    {
      delete heapVec;
      return nullptr;
    }
    AVPacket* packet = av_packet_alloc();
    if (!packet)
    {
      av_buffer_unref(&buf);
      return nullptr;
    }
    packet->buf = buf;
    packet->data = buf->data;
    packet->size = dataSize;
    return packet;
  }

  FrameDecode::FrameDecode(ECodec StreamCodec, rtc::Track* VideoInfo)
  {
    ldecoder(ELogVerbosity::Info) << "Constructing FrameDecode (codec init)" << std::endl;
    switch (StreamCodec)
    {
    case ECodec::VP8:
      Codec = avcodec_find_decoder(AV_CODEC_ID_VP8);
      Depacketizer = std::make_unique<VP9Depacketizer>();
      break;
    case ECodec::VP9:
      Codec = avcodec_find_decoder(AV_CODEC_ID_VP9);
      Depacketizer = std::make_unique<VP9Depacketizer>();
      break;
    case ECodec::H264:
      Codec = avcodec_find_decoder(AV_CODEC_ID_H264);
      Depacketizer = std::make_unique<H264Depacketizer>();
      break;
    case ECodec::H265:
      Codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
      Depacketizer = std::make_unique<H264Depacketizer>();
      break;
    default:
      throw std::runtime_error("Codec not supported");
    }


    if (!Codec)
    {
      throw std::runtime_error("Codec not found");
    }

    CodecContext = avcodec_alloc_context3(Codec);
    if (!CodecContext)
    {
      throw std::runtime_error("Could not allocate video codec context");
    }

    if (avcodec_open2(CodecContext, Codec, NULL) < 0)
    {
      throw std::runtime_error("Could not open codec");
    }

    Frame = av_frame_alloc();
    if (!Frame)
    {
      throw std::runtime_error("Could not allocate video frame");
    }

    Packet = av_packet_alloc();
    if (!Packet)
    {
      throw std::runtime_error("Could not allocate packet");
    }

    // Pre-reserve the depacketizer buffer generously
    Depacketizer->ReserveFrame(2 * 1024 * 1024); // 2 MB, should be enough for typical frames and prevent fragmentation churn

    // TODO bitrate is also in the session description protocoll
    // framerate and resolution should be transmitted either through data channel or as video track package
    CodecContext->bit_rate = 400000;
    CodecContext->framerate = {10, 1};
    CodecContext->width = 1280;
    CodecContext->height = 720;

    if (VideoInfo)
    {
      MaxMessageSize = VideoInfo->maxMessageSize();
    }
    DecoderThread = std::make_shared<WorkerThread>();

    // sanity check: fire up the decoder thread with a logging-only task
    DecoderThread->AddTask([]()
    {
      ldecoder(ELogVerbosity::Info) << "Decoder thread initialized and running" << std::endl;
    });
  }

  void FrameDecode::ParseDescription(const nlohmann::json& desc)
  {
    // Expect JSON shaped like MediaReceiver::RemoteMediaDescription
    try
    {
      if (desc.contains("payloadTypes") && desc["payloadTypes"].is_array())
      {
        for (const auto &entry : desc["payloadTypes"])
        {
          if (!entry.is_object()) continue;
          if (entry.contains("format") && entry["format"].is_string())
          {
            std::string fmt = entry["format"].get<std::string>();
            int pt = entry.value("payloadType", -1);
            // accept common variants (VP9, VP9/90000)
            if (pt >= 0 && (fmt == "VP9" || fmt.rfind("VP9", 0) == 0 || fmt.find("VP9") != std::string::npos))
            {
              ExpectedPayloadType = pt;
              ldecoder(ELogVerbosity::Info) << "FrameDecode: expected VP9 payload type set to " << pt << std::endl;
              return;
            }
          }
        }
      }
    }
    catch (const std::exception &e)
    {
      ldecoder(ELogVerbosity::Error) << "ParseDescription threw: " << e.what() << std::endl;
    }
  }

  FrameDecode::~FrameDecode()
  {
    DecoderThread->Stop();
    av_frame_free(&Frame);
    av_packet_free(&Packet);
    avcodec_free_context(&CodecContext);
  }

  std::function<bool(rtc::binary, rtc::FrameInfo)> FrameDecode::CreateAcceptor(std::function<void(FrameContent)>&& Callback)
  {
    ldecoder(ELogVerbosity::Info) << "Creating acceptor callback for incoming packets" << std::endl;
    // Do not overwrite the member FrameCallback with the acceptor-provided
    // callback. Instead capture the provided callback locally so the
    // acceptor's consumer gets invoked for frames created by this acceptor,
    // while preserving any callback set previously via SetFrameCallback.
    std::shared_ptr<std::function<void(FrameContent)>> cbptr;
    if (Callback) cbptr = std::make_shared<std::function<void(FrameContent)>>(std::move(Callback));


    return [this, cbptr](rtc::binary Data, rtc::FrameInfo Info) -> bool
    {
      ldecoder(ELogVerbosity::Info) << "FrameDecode::Acceptor invoked, data size=" << Data.size() << std::endl;
      // ensure we log small packets as INFO so they are visible during diagnostics
      uint8_t* DataPtr = reinterpret_cast<uint8_t*>(Data.data());
      // precheck because a vpx frame has a minimum size
      if (Data.size() < 10)
      {
        ldecoder(ELogVerbosity::Debug) << "Packet too small (" << Data.size() << "), forwarding raw to message callback" << std::endl;
        // fallback to message callback if set
        if (MessageCallback.has_value())
        {
          MessageCallback.value()(Data);
        }
        return false;
      }
      else
      {
        ldecoder(ELogVerbosity::Debug) << "Packet size looks valid for VP9 frame, attempting to parse and decode" << std::endl;
      }

      rtc::RtpHeader* Header = reinterpret_cast<rtc::RtpHeader*>(DataPtr);

      // print payload type in verbose
      auto* body = reinterpret_cast<uint8_t*>(Header->getBody());

      // sanity check to see whether the pointers work
      if (body < DataPtr || body >= DataPtr + Data.size())
      {
        ldecoder(ELogVerbosity::Warning) << "Header body pointer is out of bounds, forwarding raw to message callback" << std::endl;
        // fallback to message callback if set
        if (MessageCallback.has_value())
        {
          MessageCallback.value()(Data);
        }
        return false;
      }

      auto& l = ldecoder(ELogVerbosity::Verbose) << "Packet ssrc: " << Header->ssrc() << " - time: " << Header->
        timestamp()
        << " - seq: " << Header->seqNumber() << " - payload: " << static_cast<uint16_t>(Header->payloadType())
        << " - Extension: " << Header->extension() << " - Marker: " << static_cast<uint16_t>(Header->marker());
      if (Header->getExtensionHeader()) l << " - Extension header: " << Header->getExtensionHeaderSize();
      l << std::endl;

      // Diagnostic: if marker is set, dump first bytes of the packet so we can
      // compare sender header layout with receiver parsing.
      if (Header->marker() > 0)
      {
        std::ostringstream oss;
        size_t hdrBytes = std::min<size_t>(13, Data.size());
        for (size_t i = 0; i < hdrBytes; ++i)
        {
          if (i) oss << ' ';
          oss << std::hex << std::setw(2) << std::setfill('0') << (static_cast<int>(DataPtr[i]) & 0xFF);
        }
        ldecoder(ELogVerbosity::Debug) << "Packet raw bytes: " << oss.str() << std::endl;
      }

      // If ParseDescription set an expected payload type, ignore/forward other PTs.
      if (ExpectedPayloadType >= 0)
      {
        if (static_cast<int>(Header->payloadType()) != ExpectedPayloadType)
        {
          ldecoder(ELogVerbosity::Verbose) << "FrameDecode::Acceptor: skipping packet with payload type " << static_cast<int>(Header->payloadType()) << std::endl;
          // forward raw packet to consumer callback for non-matching payloads
          if (MessageCallback.has_value())
          {
            MessageCallback.value()(Data);
          }
          return false;
        }
      }
        
      // if the remaining size is zero, skip
      if (static_cast<int64_t>(Data.size()) - static_cast<int64_t>(Header->getSize()) <= 0)
      {
        ldecoder(ELogVerbosity::Verbose) << "No payload packet" << std::endl;
        return false;
      }
      // add the packet to the buffer
      try {
        AddPacket(Data);
      } catch (const std::exception &e) {
        ldecoder(ELogVerbosity::Error) << "AddPacket threw: " << e.what() << std::endl;
        return false;
      }

      // Determine whether this packet actually ends the encoded frame.
      // Primary signal: RTP marker bit. Only if marker is not set do we
      // examine codec-level payload descriptors (e.g. VP9 E bit).
      bool packetEndsFrame = false;
      if (Header->marker() > 0)
      {
        packetEndsFrame = true;
        ldecoder(ELogVerbosity::Debug) << "Frame end signalled by RTP marker" << std::endl;
      }
      else
      {
        // safe guards: ensure header/body pointers are valid and payload long enough
        if (Data.size() > Header->getSize())
        {
          const uint8_t* bodyPtr = reinterpret_cast<const uint8_t*>(Header->getBody());
          // VP9 payload descriptor: E bit is 0x04
          uint8_t desc = bodyPtr[0];
          if ((desc & 0x04) != 0)
          {
            packetEndsFrame = true;
            ldecoder(ELogVerbosity::Debug) << "Frame end signalled by VP9 payload descriptor E bit" << std::endl;
          }
        }
      }

      if (packetEndsFrame)
      {
        ldecoder(ELogVerbosity::Info) << "Frame complete, creating decoding task" << std::endl;
        DecoderThread->AddTask([this, ts = Header->timestamp(), Data, cbptr]()
        {
          lthread(ELogVerbosity::Info) << "Decoder thread started for timestamp " << ts << std::endl;
          //while (avcodec_receive_frame(CodecContext, Frame) != AVERROR(EAGAIN))
          //  std::this_thread::sleep_for(std::chrono::milliseconds(1));
          //lthread(ELogVerbosity::Debug) << "Flushed decoder buffers, now sending packet for timestamp " << ts << std::endl;
          // create a packet from the buffer
          AVPacket* packet = InitializePacketFromData(ts); // this calls a reserve function, might throw bad_alloc
          lthread(ELogVerbosity::Debug) << "Initialized AVPacket from data for timestamp " << ts << std::endl;
          if (!packet)
          {
            lthread(ELogVerbosity::Warning) << "InitializePacketFromData returned null for timestamp " << ts << " (incomplete frame or sequence error)" << std::endl;
            // Diagnostic: dump any buffered packets we have for this timestamp so
            // the sender/receiver header bytes can be compared when we bail.
            if (frameBuffer.find(ts) != frameBuffer.end())
            {
              auto &buf = frameBuffer[ts];
              int pktIdx = 0;
              for (auto &pkt : buf)
              {
                const uint8_t* p = reinterpret_cast<const uint8_t*>(pkt.data());
                size_t hdrBytes = std::min<size_t>(13, pkt.size());
                std::ostringstream oss;
                oss << "idx=" << pktIdx << " seq=" << reinterpret_cast<const rtc::RtpHeader*>(p)->seqNumber() << " bytes=" << hdrBytes << " ";
                for (size_t i = 0; i < hdrBytes; ++i)
                {
                  if (i) oss << ' ';
                  oss << std::hex << std::setw(2) << std::setfill('0') << (static_cast<int>(p[i]) & 0xFF);
                }
                lthread(ELogVerbosity::Debug) << "Buffered packet dump: " << oss.str() << std::endl;
                ++pktIdx;
              }
            }
            else
            {
              lthread(ELogVerbosity::Debug) << "No buffered packets found for timestamp " << ts << std::endl;
            }
            // packet is not complete
            return;
          }
          lthread(ELogVerbosity::Debug) << "AVPacket ready for decoding for timestamp " << ts << std::endl;
          int GotFrame = 0;
          int Result = 0;

          Result = avcodec_send_packet(CodecContext, packet);
          lffmpeg(ELogVerbosity::Debug) << "avcodec_send_packet result: " << Result << std::endl;
          if (Result < 0)
          {
            char Error[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(Result, Error, AV_ERROR_MAX_STRING_SIZE);
            lffmpeg(ELogVerbosity::Error) << "avcodec_send_packet failed: " << Error << std::endl;
          }
          else
          {
            GotFrame = avcodec_receive_frame(CodecContext, Frame);
            lffmpeg(ELogVerbosity::Debug) << "avcodec_receive_frame return: " << GotFrame << std::endl;
            // frame decoded?
            if (GotFrame >= 0)
            {
              // create a frame content
              FrameContent Content;
              const int w = Frame->width;
              const int h = Frame->height;
              Content.Width = w;
              Content.Height = h;
              Content.Timestamp = ts;

              // Prefer to export a tightly packed YUV420P buffer (WxH Y, WxH/4 U, WxH/4 V).
              // Use linesize to copy each source row because linesize may contain padding.
              if (Frame->format == AV_PIX_FMT_YUV420P)
              {
                Content.Data.resize(static_cast<size_t>(w) * h * 3 / 2);
                uint8_t* dst = Content.Data.data();

                // Copy Y plane: each destination row is 'w' bytes, source row is linesize[0]
                for (int y = 0; y < h; ++y)
                {
                  memcpy(dst + y * w, Frame->data[0] + y * Frame->linesize[0], w);
                }

                const int ch = h / 2;
                const int cw = w / 2;
                uint8_t* dstU = dst + w * h;
                uint8_t* dstV = dstU + (w * h) / 4;

                // Copy U and V planes (4:2:0) row-by-row using source linesize but copying only active pixels
                for (int y = 0; y < ch; ++y)
                {
                  memcpy(dstU + y * cw, Frame->data[1] + y * Frame->linesize[1], cw);
                  memcpy(dstV + y * cw, Frame->data[2] + y * Frame->linesize[2], cw);
                }
                // Set metadata for consumer: packed, canonical linesizes and pixel format
                Content.PixFmt = static_cast<int>(Frame->format);
                Content.Linesize = { w, cw, cw };
                Content.Packed = true;
              }
              else
              {
                // Fallback: copy plane-by-plane using linesize and correct plane heights derived from
                // common chroma subsampling rules. This preserves stride/padding but produces a
                // contiguous buffer of (linesize * rows) per plane. Downstream must be stride-aware
                // or prefer the YUV420P re-packed path above.
                const int plane0_h = h;
                const int plane1_h = (h + 1) / 2;
                const int plane2_h = (h + 1) / 2;

                size_t y_bytes = static_cast<size_t>(Frame->linesize[0]) * plane0_h;
                size_t u_bytes = static_cast<size_t>(Frame->linesize[1]) * plane1_h;
                size_t v_bytes = static_cast<size_t>(Frame->linesize[2]) * plane2_h;

                Content.Data.reserve(y_bytes + u_bytes + v_bytes);
                // Append Y plane
                for (int y = 0; y < plane0_h; ++y)
                {
                  const uint8_t* src = Frame->data[0] + y * Frame->linesize[0];
                  Content.Data.insert(Content.Data.end(), src, src + Frame->linesize[0]);
                }
                // Append U plane
                for (int y = 0; y < plane1_h; ++y)
                {
                  const uint8_t* src = Frame->data[1] + y * Frame->linesize[1];
                  Content.Data.insert(Content.Data.end(), src, src + Frame->linesize[1]);
                }
                // Append V plane
                for (int y = 0; y < plane2_h; ++y)
                {
                  const uint8_t* src = Frame->data[2] + y * Frame->linesize[2];
                  Content.Data.insert(Content.Data.end(), src, src + Frame->linesize[2]);
                }
                // Set metadata for consumer: indicate source pixel format and actual linesizes
                Content.PixFmt = static_cast<int>(Frame->format);
                Content.Linesize = { Frame->linesize[0], Frame->linesize[1], Frame->linesize[2] };
                Content.Packed = false;
              }

              if (OutputMode == EOutputMode::PackedRGB)
              {
                lffmpeg(ELogVerbosity::Debug) << "Output mode is PackedRGB, converting frame to RGB24" << std::endl;
                // Convert the frame to RGB24 using sws_scale
                SwsContext* swsCtx = sws_getContext(w, h, static_cast<AVPixelFormat>(Frame->format),
                                                    w, h, AV_PIX_FMT_RGB24,
                                                    SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!swsCtx)
                {
                  lffmpeg(ELogVerbosity::Error) << "Failed to create SwsContext for RGB conversion" << std::endl;
                  return;
                }
                // Allocate buffer for RGB data (uint8_t bytes)
                std::vector<uint8_t> rgbData(static_cast<size_t>(w) * h * 3); // 3 bytes per pixel for RGB24
                uint8_t* rgbPtr = rgbData.data();
                int rgbLinesize = w * 3;

                // Set up source and destination pointers and linesizes for sws_scale
                const uint8_t* srcSlices[3] = { Frame->data[0], Frame->data[1], Frame->data[2] };
                const int srcLinesizes[3] = { Frame->linesize[0], Frame->linesize[1], Frame->linesize[2] };
                uint8_t* dstSlices[1] = { rgbPtr };
                int dstLinesizes[1] = { rgbLinesize };

                // Perform the conversion
                int ret = sws_scale(swsCtx, srcSlices, srcLinesizes, 0, h, dstSlices, dstLinesizes);
                sws_freeContext(swsCtx);
                if (ret <= 0)
                {
                  lffmpeg(ELogVerbosity::Error) << "sws_scale failed for RGB conversion" << std::endl;
                  return;
                }
                // Replace content data with converted RGB data and update metadata
                Content.Data = std::move(rgbData);
                Content.PixFmt = AV_PIX_FMT_RGB24;
                // For RGB output, mark as not YUV-packed so consumers don't attempt YUV unpacking.
                Content.Packed = false;
                // Provide linesize information (store RGB stride in first element)
                Content.Linesize = { rgbLinesize, 0, 0 };
              }
              
              // Prefer the acceptor-local callback if provided, otherwise
              // fall back to the member FrameCallback set via SetFrameCallback.
              if (cbptr && *cbptr)
              {
                (*cbptr)(Content);
              }
              else if (FrameCallback.has_value())
              {
                FrameCallback.value()(Content);
              }
            }
            else
            {
              // get the error from the decoder (use GotFrame as code)
              char Error[AV_ERROR_MAX_STRING_SIZE];
              av_strerror(GotFrame, Error, AV_ERROR_MAX_STRING_SIZE);
              lffmpeg(ELogVerbosity::Error) << "Error decoding frame (receive_frame returned " << GotFrame << "): " << Error << std::endl;
            }
          }
          // free the packet returned by depacketizer
          av_packet_free(&packet);
        });
        // We accepted the packet for decoding / processing
        return true;
      }
      // if we didn't reach marker or didn't queue a decode task yet, consider the packet accepted
      return true;
    };
  }
  
  void FrameDecode::SetMaxFrameBuffer(uint32_t MaxFrames)
  {
    this->MaxFrames = MaxFrames;
  }

  inline AVPacket* FrameDecode::InitializePacketFromData(uint32_t index)
  {
    lthread(ELogVerbosity::Info) << "Initializing AVPacket from buffered data for index " << index << std::endl;
    Depacketizer->ResetPacket();
    // to extract the VP9 package from multiple RTP packages, we need to sort them by sequence number
    // return nullptr if the frameBuffer does not contain the index
    if (frameBuffer.find(index) == frameBuffer.end())
    {
      lthread(ELogVerbosity::Debug) << "InitializePacketFromData: no frame for index " << index << std::endl;
      return nullptr;
    }
    lthread(ELogVerbosity::Debug) << "InitializePacketFromData: found frame for index " << index << " with " << frameBuffer[index].size() << " packets" << std::endl;
    auto& packets = frameBuffer[index];
    std::ranges::sort(packets, [](const rtc::binary& a, const rtc::binary& b)
    {
      return reinterpret_cast<const rtc::RtpHeader*>(a.data())->seqNumber()
        < reinterpret_cast<const rtc::RtpHeader*>(b.data())->seqNumber();
    });
    // ensure that the sequence numbers are correct
    int sq = -1;
    int size = 0;
    for (auto& packet : packets)
    {
      size += static_cast<int>(packet.size() - sizeof(rtc::RtpHeader));
      const rtc::RtpHeader* header = reinterpret_cast<const rtc::RtpHeader*>(packet.data());
      
      if (sq == -1)
      {
        sq = header->seqNumber();
      }
      else
      {
        if (sq + 1 != header->seqNumber())
        {
          // sequence number is not correct
          lthread(ELogVerbosity::Warning) << "InitializePacketFromData: sequence gap for index " << index << 
            " expected " << (sq + 1) << " got " << header->seqNumber() << std::endl;
          return nullptr;
        }
        else
          sq = header->seqNumber();
      }
    }
    // Reserve expected total frame size in the depacketizer to avoid
    // repeated allocations during AddPacket (prevents allocation churn).
    lthread(ELogVerbosity::Debug) << "Reserving frame buffer size " << size << " for index " << index << std::endl;
    //Depacketizer->ReserveFrame(static_cast<size_t>(size));
    // log capacity and size before adding packets (dep: depacketizer buffer)
    lthread(ELogVerbosity::Debug) << "Depacketizer buffer capacity before adding packets: " << Depacketizer->FrameCapacity() << " bytes, current size: " << Depacketizer->FrameSize() << " bytes" << std::endl;
    for (auto& packet : packets)
    {
      lthread(ELogVerbosity::Debug) << "Adding packet with size " << packet.size() << " to depacketizer for index " << index << std::endl;
      Depacketizer->AddPacket(packet);
    }
    lthread(ELogVerbosity::Debug) << "All packets added to depacketizer for index " << index << std::endl;
    AVPacket* out = Depacketizer->GetAVFrame(); // implicit copy
    if (!out)
    {
      ldecoder(ELogVerbosity::Warning) << "Depacketizer returned null AVPacket for index " << index << " size=" << size << std::endl;
    }
    return out;
  }

  void FrameDecode::AddPacket(const rtc::binary& Data)
  {
    lthread(ELogVerbosity::Verbose) << "Adding packet to frame buffer, size=" << Data.size() << std::endl;
    const rtc::RtpHeader* Header = reinterpret_cast<const rtc::RtpHeader*>(Data.data());
    const uint8_t* body = reinterpret_cast<const uint8_t*>(Header->getBody());


    // check if timestamp is already in the buffer
    if (frameBuffer.find(Header->timestamp()) == frameBuffer.end())
    {
      lthread(ELogVerbosity::Verbose) << "New timestamp " << Header->timestamp() << " - initializing buffer entry" << std::endl;
      // checif the buffer is full
      if (frameBuffer.size() >= MaxFrames)
      {
        lthread(ELogVerbosity::Verbose) << "Frame buffer full (size=" << frameBuffer.size() << "), removing oldest frame to make room for new timestamp " << Header->timestamp() << std::endl;
        // remove the oldest frame
        uint32_t oldest = currentlyCapturing.front();
        currentlyCapturing.pop_front();
        frameBuffer.erase(oldest);
        // log to verbose
        ldecoder(ELogVerbosity::Verbose) << "Removed frame " << oldest << " from buffer" << std::endl;
      }
      else
      {
        lthread(ELogVerbosity::Verbose) << "Frame buffer has space (size=" << frameBuffer.size() << "), adding new timestamp " << Header->timestamp() << std::endl;
        // create a new frame entry and track its timestamp so we can remove oldest later
        frameBuffer[Header->timestamp()] = std::vector<rtc::binary>();
        currentlyCapturing.push_back(Header->timestamp());
        // move the packet into the buffer
        frameBuffer[Header->timestamp()].push_back(std::move(Data)); // move!
      }
    }
    else
    {
      lthread(ELogVerbosity::Verbose) <<  "Existing timestamp " << Header->timestamp() << " - appending packet to existing buffer entry" << std::endl;
      // insert the packet into the buffer
      frameBuffer[Header->timestamp()].push_back(Data); // copy!
    }
  lthread(ELogVerbosity::Verbose) << "Current frame buffer size: " << frameBuffer.size() << " entries" << std::endl;
  }
}

extern "C"
{
  // a c callback interface for registering the av_log_set_callback from external consumers (e.g. PySynavis) without exposing the Synavis::Logger or other internals
  static void av_log_callback(void *ptr, int level, const char *fmt, va_list vargs)
  {
    // relay to Synavis logger, mapping FFmpeg log levels to Synavis log levels
    Synavis::ELogVerbosity Verbosity;
    switch (level)
    {
    case AV_LOG_PANIC:
    case AV_LOG_FATAL:
    case AV_LOG_ERROR:
      Verbosity = Synavis::ELogVerbosity::Error;
      break;
    case AV_LOG_WARNING:
      Verbosity = Synavis::ELogVerbosity::Warning;
      break;
    case AV_LOG_INFO:
      Verbosity = Synavis::ELogVerbosity::Info;
      break;
    case AV_LOG_VERBOSE:
    case AV_LOG_DEBUG:
    case AV_LOG_TRACE:
      Verbosity = Synavis::ELogVerbosity::Verbose;
      break;
    }
    // format the message using vsnprintf
    char Buffer[1024];
    vsnprintf(Buffer, sizeof(Buffer), fmt, vargs);
    // log the message with the appropriate verbosity
    lffmpeg(Verbosity) << Buffer << std::endl;
  }
}

void Synavis::RegisterAvLogCallback(bool bUseSynavis)
{
  // register av_log callback to forward libav logs to our logger
  if (bUseSynavis)
  {
    av_log_set_callback(av_log_callback);
    // Ensure libav emits debug/verbose output so our callback receives messages
    av_log_set_level(AV_LOG_DEBUG);
    lffmpeg(ELogVerbosity::Info) << "Registered custom av_log callback to forward FFmpeg logs to Synavis logger (level=DEBUG)" << std::endl;
  }
  else
  {
    av_log_set_callback(nullptr); // reset to default
    av_log_set_level(AV_LOG_INFO);
    lffmpeg(ELogVerbosity::Info) << "Reset av_log callback to default (level=INFO)" << std::endl;
  }
}
