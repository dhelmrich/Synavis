#include "FrameDecodeAV.hpp"

// libAV includes
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
}

namespace Synavis
{

  // byte literal
  std::byte operator"" _b(unsigned long long Value)
  {
    return static_cast<std::byte>(Value);
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
    DecoderThread->Run();
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
        while (avcodec_receive_frame(CodecContext, Frame) != AVERROR(EAGAIN));

        rtc::RtpHeader* Header = reinterpret_cast<rtc::RtpHeader*>(DataPtr);
        auto* body = reinterpret_cast<uint8_t*>(Header->getBody());

        std::cout << "Packet ssrc: " << Header->ssrc() << " - time: " << Header->timestamp()
          << " - seq: " << Header->seqNumber() << " - payload: " << Header->payloadType()
          << " - Extension: " << Header->extension() << " - Marker: " << Header->marker();
        if (Header->getExtensionHeader()) std::cout << " - Extension header: " << Header->getExtensionHeaderSize();
        std::cout << std::endl;
        return;
        Packet->data = DataPtr;
        Packet->size = Data.size(); //- sizeof(rtc::RtpHeader);
        Packet->pts = Header->timestamp();
        Packet->flags = AV_PKT_FLAG_KEY;



        // decode the frame
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
        int GotFrame = 0;
        int Result = avcodec_decode_video2(CodecContext, Frame, &GotFrame, Packet);
#else
        int Result = avcodec_send_packet(CodecContext, Packet);
        int GotFrame = avcodec_receive_frame(CodecContext, Frame);
#endif
        // check if the frame is decoded
        if (Result < 0)
        {
          // not a frame
          Callback(Data);
        }
        else
        {
          // frame decoded
          if (GotFrame)
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
            Callback(Data);
          }
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
    auto& frame = frameBuffer[index];
    std::ranges::sort(frame, [](const rtc::binary& a, const rtc::binary& b)
      {
        // sort by ssrc
        return reinterpret_cast<const rtc::RtpHeader*>(a.data())->ssrc() < reinterpret_cast<const rtc::RtpHeader*>(b.data())->ssrc();
      });
    // concatenate the packets
    std::vector<uint8_t> data;
    for (auto& packet : frame)
    {
      data.insert(data.end(), packet.begin(), packet.end());
    }
  }

  void FrameDecode::AddPacket(rtc::binary Data)
  {
    const rtc::RtpHeader* Header = reinterpret_cast<const rtc::RtpHeader*>(Data.data());
    const uint8_t* body = reinterpret_cast<const uint8_t*>(Header->getBody());

    // check if ssrc is already in the buffer
    if (frameBuffer.find(Header->ssrc()) == frameBuffer.end())
    {
      // checif the buffer is full
      if (frameBuffer.size() >= MaxFrames)
      {
        // remove the oldest frame
        uint32_t oldest = currentlyCapturing.front();
        currentlyCapturing.pop_front();
        frameBuffer.erase(oldest);
        // log to verbose
        Logger::Get()("FrameDecode", ELogVerbosity::Verbose) << "Removed frame " << oldest << " from buffer" << std::endl;
      }
    }
  }
}
