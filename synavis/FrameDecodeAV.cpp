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

  FrameDecode::FrameDecode()
  {
    // initialize the decoder libav with vp9
    Codec = avcodec_find_decoder(AV_CODEC_ID_VP9);
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

        // try to depayload the rtp packet
        if (Data[0] == 0x90_b && Data[1] == 0x90_b && Data[2] == 0x90_b && Data[3] == 0x90_b)
        {
          // depayload the rtp packet
          Data.erase(Data.begin(), Data.begin() + 4);
          // make frame to avpacket
          Packet->data = DataPtr;
          Packet->size = Data.size();
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
              if(FrameCallback.has_value())
              {
                FrameCallback.value()(Content);
              }
            }

          }
        }
        else
        {
          Callback(Data);
        }

      };
  }

  void FrameDecode::SetFrameCallback(std::function<void(FrameContent)> Callback)
  {
    FrameCallback = Callback;
  }


}