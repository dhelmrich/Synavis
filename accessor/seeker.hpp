#include <rtc/rtc.hpp>
#include <json.hpp>
#include <vector>
#include <fstream>
#include <compare>
#include <functional>

#include "accessor/export.hpp"

namespace AC
{
  enum class ACCESSOR_EXPORT EClientMessageType
  {
	  QualityControlOwnership = 0u,
	  Response,
	  Command,
	  FreezeFrame,
	  UnfreezeFrame,
	  VideoEncoderAvgQP,
	  LatencyTest,
	  InitialSettings
  };
enum class ACCESSOR_EXPORT EConnectionState
{
  STARTUP = 0,
  SIGNUP,
  OFFERED,
  CONNECTED,
  VIDEO,
  CLOSED,
  RTCERROR,
};

  struct ACCESSOR_EXPORT Socket
  {
    
  };

  struct ACCESSOR_EXPORT SaveRTP
  {
    uint32_t timestamp{0};
    uint32_t ssrc{0};
    uint16_t sequence{0};
    uint8_t payload_type{};
    bool has_padding{false};
    std::vector<std::byte> body;
    SaveRTP(){body.reserve(2048); }
    SaveRTP(rtc::RTP* package)
    {
      timestamp = package->timestamp();
      ssrc = package->ssrc();
      sequence = package->seqNumber();
      body.insert(
        body.end(),
        (std::byte*)package->getBody(),
        (std::byte*)package->getBody() + package->getSize()
      );
      payload_type = package->payloadType();
      has_padding = package->padding();
    }
    SaveRTP& operator=(rtc::RTP* package)
    {
      timestamp = package->timestamp();
      ssrc = package->ssrc();
      sequence = package->seqNumber();
      payload_type = package->payloadType();
      has_padding = package->padding();
      body.clear();
      body.insert(
        body.end(),
        (std::byte*)package->getBody(),
        (std::byte*)package->getBody() + package->getSize()
      );
      return *this;
    }

    inline void decodeH264Header()
    {
      
    }

    std::strong_ordering operator <=>(const auto& other) const {
      return (timestamp <=> other.timestamp == 0) ? sequence <=> other.sequence : timestamp<=> other.timestamp;
      //return sequence <=> other.sequence;
    }

  };

  class ACCESSOR_EXPORT Seeker: std::enable_shared_from_this<Seeker>
  {
  public:
    using json = nlohmann::json;
    Seeker();
    ~Seeker();
    virtual bool CheckSignallingActive();
    virtual void UseConfig(std::string filename);
    virtual bool EstablishedConnection(std::string ip = "127.0.0.1");
    virtual void FindBridge();
    virtual void RecoverConnection();

    

  protected:
    std::vector<int> Ports;
    
  };

}
