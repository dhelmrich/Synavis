#include "FrameDecode.hpp"

namespace Synavis
{
  FrameDecode::FrameDecode()
  {
    CodecInterface = vpx_codec_vp9_dx();
    CodecConfig.threads = 1;
    CodecConfig.w = 1920;
    CodecConfig.h = 1080;
    CodecError = vpx_codec_dec_init(&CodecContext, CodecInterface, &CodecConfig, 0);
  }

  FrameDecode::~FrameDecode()
  {
    vpx_codec_destroy(&CodecContext);
  }

  std::function<void(rtc::binary)> FrameDecode::CreateAcceptor(std::function<void(rtc::binary)>&& Callback)
  {
    return [this, Callback = std::move(Callback)](rtc::binary Data)
    {
      //precheck because a vpx frame has a minimum size
      if (Data.size() < 10)
      {
        Callback(Data);
      }
      else if (vpx_codec_decode(&CodecContext, reinterpret_cast<const uint8_t*>(Data.data()), Data.size(), nullptr, 0) != VPX_CODEC_OK)
      {
        std::cout << "Failed to decode frame of size " << Data.size() << std::endl;
        std::cout << "Error: " << vpx_codec_error(&CodecContext) << std::endl;
        Callback(Data);
      }
      else
      {
        vpx_codec_iter_t iter = nullptr;
        vpx_image_t* img = vpx_codec_get_frame(&CodecContext, &iter);
        if (img != nullptr)
        {
          Frame Buffer;
          Buffer.Data.resize(img->d_w * img->d_h * 3 / 2);
          uint8_t* Y = Buffer.Data.data();
          uint8_t* U = Y + img->d_w * img->d_h;
          uint8_t* V = U + img->d_w * img->d_h / 4;
          for (int i = 0; i < img->d_h; i++)
          {
            memcpy(Y + i * img->d_w, img->planes[0] + i * img->stride[0], img->d_w);
          }
          for (int i = 0; i < img->d_h / 2; i++)
          {
            memcpy(U + i * img->d_w / 2, img->planes[1] + i * img->stride[1], img->d_w / 2);
            memcpy(V + i * img->d_w / 2, img->planes[2] + i * img->stride[2], img->d_w / 2);
          }
          if (FrameCallback.has_value())
          {
            FrameCallback.value()(Buffer);
          }
        }
      }
    };
  }

  void FrameDecode::SetFrameCallback(std::function<void(Frame)> Callback)
  {
    FrameCallback = Callback;
  }


}