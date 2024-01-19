#include "FrameDecodeAV.hpp"

// libAV includes
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
}


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

  VP9Depacketizer::VP9Depacketizer()
  {
  }

  VP9Depacketizer::~VP9Depacketizer()
  {
  }

  void VP9Depacketizer::AddPacket(rtc::binary Data)
  {
    const rtc::RtpHeader* Header = reinterpret_cast<const rtc::RtpHeader*>(Data.data());
    
    const uint8_t* body = reinterpret_cast<const uint8_t*>(Header->getBody());
    
    if(frame.empty())
    {
      // check if the packet is a keyframe
      if (Header->marker() > 0 && body[0] == 0x9d && body[1] == 0x01 && body[2] == 0x2a)
      {
        // keyframe
        ldecoder(ELogVerbosity::Verbose) << "Keyframe" << std::endl;
        // create a new frame
        frame = std::vector<std::byte>(Data.begin() + sizeof(rtc::RtpHeader), Data.end());
        timestamp = Header->timestamp();
      }
      else
      {
        // not a keyframe
        ldecoder(ELogVerbosity::Verbose) << "Not a keyframe" << std::endl;
      }
    }
    else
    {
      // check if the timestamp is the same
      if (Header->timestamp() == timestamp)
      {
        // append the packet to the frame
        frame.insert(frame.end(), Data.begin() + sizeof(rtc::RtpHeader), Data.end());
      }
      else
      {
        // not the same timestamp
        ldecoder(ELogVerbosity::Verbose) << "Not the same timestamp" << std::endl;
      }
    }
  }

  bool VP9Depacketizer::IsFrameComplete()
  {
    return false;
  }

  FrameDecode::FrameDecode(rtc::Track* VideoInfo)
  {
    // initialize the decoder libav with vp9
    Codec = avcodec_find_decoder(AV_CODEC_ID_VP8);
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

    CodecContext->bit_rate = 400000;
    CodecContext->framerate = { 10, 1 };
    CodecContext->width = 1280;
    CodecContext->height = 720;

    if (VideoInfo)
    {
      MaxMessageSize = VideoInfo->maxMessageSize();
    }
    DecoderThread = std::make_shared<WorkerThread>();
  }

  FrameDecode::~FrameDecode()
  {
    av_frame_free(&Frame);
    av_packet_free(&Packet);
    avcodec_free_context(&CodecContext);
  }

  std::function<void(rtc::binary)> FrameDecode::CreateAcceptor(std::function<void(rtc::binary)>&& Callback)
  {
    return [this, Callback = std::move(Callback)](rtc::binary Data)
    {
      uint8_t* DataPtr = reinterpret_cast<uint8_t*>(Data.data());
      //precheck because a vpx frame has a minimum size
      if (Data.size() < 10)
      {
        Callback(Data);
      }



      // flush the decoder

      rtc::RtpHeader* Header = reinterpret_cast<rtc::RtpHeader*>(DataPtr);
      // depayload the VP9 RTP packet and extract the B and E bit
      
      auto* vp9Payload = reinterpret_cast<VP9Payload*>(Header->getBody());
      auto payload = Header->payloadType();
      bool start = vp9Payload->start();
      bool end = vp9Payload->end();
      // print payload type in verbose
      
      auto* body = reinterpret_cast<uint8_t*>(Header->getBody());
      auto& l = ldecoder(ELogVerbosity::Verbose) << "Packet ssrc: " << Header->ssrc() << " - time: " << Header->timestamp()
        << " - seq: " << Header->seqNumber() << " - payload: " << static_cast<uint16_t>(Header->payloadType())
        << " - Extension: " << Header->extension() << " - Marker: " << static_cast<uint16_t>(Header->marker());
      if (Header->getExtensionHeader()) l << " - Extension header: " << Header->getExtensionHeaderSize();
      l << std::endl;

      // add the packet to the buffer
      AddPacket(Data);

      // check if the marker is set
      if (Header->marker() > 0)
      {
        ldecoder(ELogVerbosity::Info) << "Frame complete, creating decoding task" << std::endl;
        DecoderThread->AddTask([this, ts = Header->timestamp(), Data]()
        {

          //while (avcodec_receive_frame(CodecContext, Frame) != AVERROR(EAGAIN));

          // create a packet from the buffer
          AVPacket* packet = InitializePacketFromData(ts);
          if (!packet)
          {
            // packet is not complete
            //Callback(Data);
            return;
          }
          // decode the frame
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
          int GotFrame = 0;
          int Result = avcodec_decode_video2(CodecContext, Frame, &GotFrame, Packet);
#else
          int Result = avcodec_send_packet(CodecContext, Packet);
          lffmpeg(ELogVerbosity::Verbose) << "Result: " << Result << std::endl;
          int GotFrame = avcodec_receive_frame(CodecContext, Frame);
          lffmpeg(ELogVerbosity::Verbose) << "GotFrame: " << GotFrame << std::endl;
#endif
          // check if the frame is decoded
          if (Result < 0)
          {
            // not a frame
            lffmpeg(ELogVerbosity::Verbose) << "Not a frame (didn't accept frame)" << std::endl;
            //Callback(Data);
          }
          else
          {
            // frame decoded
            if (GotFrame >= 0)
            {
              // create a frame content
              FrameContent Content;
              Content.Width = Frame->width;
              Content.Height = Frame->height;
              Content.Data = std::vector<uint8_t>(Frame->data[0], Frame->data[0] + Frame->linesize[0] * Frame->height);
              Content.Data.insert(Content.Data.end(), Frame->data[1], Frame->data[1] + Frame->linesize[1] * Frame->height / 2);
              Content.Data.insert(Content.Data.end(), Frame->data[2], Frame->data[2] + Frame->linesize[2] * Frame->height / 2);
              // call the callback
              if (FrameCallback.has_value())
              {
                FrameCallback.value()(Content);
              }
            }
            else
            {
              //Callback(Data);
              lffmpeg(ELogVerbosity::Verbose) << "Not a frame (didn't get frame)" << std::endl;
              // get the error from the decoder
              char Error[AV_ERROR_MAX_STRING_SIZE];
              av_strerror(Result, Error, AV_ERROR_MAX_STRING_SIZE);
              lffmpeg(ELogVerbosity::Error) << "Error decoding frame: " << Error << std::endl;
            }
          }
        });
      }
    };
  }

  void FrameDecode::SetFrameCallback(std::function<void(FrameContent)> Callback)
  {
    FrameCallback = Callback;
  }

  void FrameDecode::SetMaxFrameBuffer(uint32_t MaxFrames)
  {
    this->MaxFrames = MaxFrames;
  }

  inline AVPacket* FrameDecode::InitializePacketFromData(uint32_t index)
  {
    // to extract the VP9 package from multiple RTP packages, we need to sort them by sequence number

    // return nullptr if the frameBuffer does not contain the index
    if (frameBuffer.find(index) == frameBuffer.end())
    {
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
      size += packet.size() - sizeof(rtc::RtpHeader);
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
          return nullptr;
        }
        else
          sq = header->seqNumber();
      }
    }
    std::byte* data = new std::byte[size];
    for (auto& packet : frame)
    {
      // insert with pointer cast
      std::ranges::copy(packet.begin() + sizeof(rtc::RtpHeader), packet.end(), data);
    }
    // create the packet
    AVPacket* packet = av_packet_alloc();
    packet->data = AS_UINT8(data);
    packet->size = size;
    packet->flags = AV_PKT_FLAG_KEY;
    return packet;
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
        ldecoder(ELogVerbosity::Verbose) << "Removed frame " << oldest << " from buffer" << std::endl;
      }
      else
      {
        frameBuffer[Header->timestamp()] = std::vector<rtc::binary>();
        // insert the frame into the buffer
        frameBuffer[Header->timestamp()].push_back(Data);
      }
    }
    else
    {
      // insert the packet into the buffer
      frameBuffer[Header->timestamp()].push_back(Data);
    }
  }
}
