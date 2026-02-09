#include "FrameDecodeAV.hpp"

// libAV includes
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/buffer.h>
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

  VP9Depacketizer::VP9Depacketizer()
  {
    frame.reserve(1500); // pre-allocate typical MTU size for efficiency
  }

  VP9Depacketizer::~VP9Depacketizer()
  {
  }

  void VP9Depacketizer::AddPacket(rtc::binary Data)
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

  void H264Depacketizer::AddPacket(rtc::binary Packet)
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

  FrameDecode::FrameDecode(rtc::Track* VideoInfo, ECodec StreamCodec)
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
          ldecoder(ELogVerbosity::Info) << "Decoder thread started for timestamp " << ts << std::endl;
          while (avcodec_receive_frame(CodecContext, Frame) != AVERROR(EAGAIN))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          ldecoder(ELogVerbosity::Debug) << "Flushed decoder buffers, now sending packet for timestamp " << ts << std::endl;
          // create a packet from the buffer
          AVPacket* packet = InitializePacketFromData(ts);
          ldecoder(ELogVerbosity::Debug) << "Initialized AVPacket from data for timestamp " << ts << std::endl;
          if (!packet)
          {
            ldecoder(ELogVerbosity::Warning) << "InitializePacketFromData returned null for timestamp " << ts << " (incomplete frame or sequence error)" << std::endl;
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
                ldecoder(ELogVerbosity::Debug) << "Buffered packet dump: " << oss.str() << std::endl;
                ++pktIdx;
              }
            }
            // packet is not complete
            return;
          }
          int GotFrame = 0;
          int Result = 0;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
          Result = avcodec_decode_video2(CodecContext, Frame, &GotFrame, packet);
#else
          Result = avcodec_send_packet(CodecContext, packet);
          lffmpeg(ELogVerbosity::Debug) << "avcodec_send_packet result: " << Result << std::endl;
          if (Result < 0)
          {
            char Error[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(Result, Error, AV_ERROR_MAX_STRING_SIZE);
            lffmpeg(ELogVerbosity::Error) << "avcodec_send_packet failed: " << Error << std::endl;
          }
          GotFrame = avcodec_receive_frame(CodecContext, Frame);
          lffmpeg(ELogVerbosity::Debug) << "avcodec_receive_frame return: " << GotFrame << std::endl;
#endif
          // check if the frame is decoded
          if (Result < 0)
          {
            // already logged send failure above
          }
          else
          {
            // frame decoded?
            if (GotFrame >= 0)
            {
              // create a frame content
              FrameContent Content;
              Content.Width = Frame->width;
              Content.Height = Frame->height;
              Content.Data = std::vector<uint8_t>(Frame->data[0], Frame->data[0] + Frame->linesize[0] * Frame->height);
              Content.Data.insert(Content.Data.end(), Frame->data[1],
                                  Frame->data[1] + Frame->linesize[1] * Frame->height / 2);
              Content.Data.insert(Content.Data.end(), Frame->data[2],
                                  Frame->data[2] + Frame->linesize[2] * Frame->height / 2);
              
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
    Depacketizer->ResetPacket();
    // to extract the VP9 package from multiple RTP packages, we need to sort them by sequence number
    // return nullptr if the frameBuffer does not contain the index
    if (frameBuffer.find(index) == frameBuffer.end())
    {
      ldecoder(ELogVerbosity::Debug) << "InitializePacketFromData: no frame for index " << index << std::endl;
      return nullptr;
    }
    auto& frame = frameBuffer[index];
    std::ranges::sort(frame, [](const rtc::binary& a, const rtc::binary& b)
    {
      return reinterpret_cast<const rtc::RtpHeader*>(a.data())->seqNumber()
        < reinterpret_cast<const rtc::RtpHeader*>(b.data())->seqNumber();
    });
    // ensure that the sequence numbers are correct
    int sq = -1;
    int size = 0;
    for (auto& packet : frame)
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
          ldecoder(ELogVerbosity::Warning) << "InitializePacketFromData: sequence gap for index " << index << 
            " expected " << (sq + 1) << " got " << header->seqNumber() << std::endl;
          return nullptr;
        }
        else
          sq = header->seqNumber();
      }
    }
    // Reserve expected total frame size in the depacketizer to avoid
    // repeated allocations during AddPacket (prevents allocation churn).
    Depacketizer->ReserveFrame(static_cast<size_t>(size));
    for (auto& packet : frame)
    {
      Depacketizer->AddPacket(packet);
    }
    AVPacket* out = Depacketizer->GetAVFrame(); // implicit copy
    if (!out)
    {
      ldecoder(ELogVerbosity::Warning) << "Depacketizer returned null AVPacket for index " << index << " size=" << size << std::endl;
    }
    return out;
  }

  void FrameDecode::AddPacket(rtc::binary Data)
  {
    const rtc::RtpHeader* Header = reinterpret_cast<const rtc::RtpHeader*>(Data.data());
    const uint8_t* body = reinterpret_cast<const uint8_t*>(Header->getBody());


    // check if timestamp is already in the buffer
    if (frameBuffer.find(Header->timestamp()) == frameBuffer.end())
    {
      // checif the buffer is full
      if (frameBuffer.size() >= MaxFrames)
      {
        // remove the oldest frame
        uint32_t oldest = currentlyCapturing.front();
        currentlyCapturing.pop_front();
        frameBuffer.erase(oldest);
        // log to verbose
        ldecoder(ELogVerbosity::Debug) << "Removed frame " << oldest << " from buffer" << std::endl;
      }
      else
      {
        frameBuffer[Header->timestamp()] = std::vector<rtc::binary>();
        // insert the frame into the buffer
        frameBuffer[Header->timestamp()].push_back(Data); // copy!
      }
    }
    else
    {
      // insert the packet into the buffer
      frameBuffer[Header->timestamp()].push_back(Data); // copy!
    }
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
