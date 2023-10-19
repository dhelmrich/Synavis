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

// libVPX includes
#include <vpx/vpx_decoder.h>
#include <vpx/vp8dx.h>


namespace Synavis
{

  struct SYNAVIS_EXPORT Frame
  {
    std::vector<uint8_t> Data;
    uint32_t Width;
    uint32_t Height;
    uint32_t Timestamp;
  };

  class SYNAVIS_EXPORT FrameDecode : public std::enable_shared_from_this<FrameDecode>
  {
    public: 
    FrameDecode();
    virtual ~FrameDecode();

    std::function<void(rtc::binary)> CreateAcceptor(std::function<void(rtc::binary)>&& Callback);

    void SetFrameCallback(std::function<void(Frame)> Callback);
    
    //std::vector<uint8_t> DecodeFrame(const uint8_t * Data, const size_t Size);
    //template < typename T >
    //void DecodeFrame(T Data)
    //{
    //  static_assert(is_pointer_convertible<T>::value, "Data must be a pointer type");
    //  DecodeFrame(reinterpret_cast<const uint8_t*>(Data.data()), Data.size());
    //}

    private:

      std::optional<std::function<void(Frame)>> FrameCallback;

    vpx_codec_ctx_t CodecContext;
    vpx_codec_dec_cfg_t CodecConfig;
    vpx_codec_iface_t* CodecInterface;
    vpx_codec_err_t CodecError;
  };
}


#endif //WEBRTCBRIDGE_FRAMEDECODE_HPP
