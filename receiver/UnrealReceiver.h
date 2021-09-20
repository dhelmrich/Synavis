
// FORWARD DEFINITIONS
#include <rtc/rtc.hpp>
#include <json.hpp>
#include <vector>
#include <fstream>
#include <compare>
#include <functional>
#include "UnrealReceiver/export.hpp"

namespace UR
{

enum class UNREALRECEIVER_EXPORT EConnectionState
{
  STARTUP = 0,
  SIGNUP,
  OFFERED,
  CONNECTED,
  VIDEO,
  CLOSED,
  RTCERROR,
};

enum class UNREALRECEIVER_EXPORT EClientMessageType
{
	QualityControlOwnership = 0u,
	Response,
	Command,
	FreezeFrame,
	UnfreezeFrame,
	VideoEncoderAvgQP,
	LatencyTest,
	InitialSettings,
};

struct UNREALRECEIVER_EXPORT SaveRTP
{
  uint32_t timestamp{0};
  uint32_t ssrc{0};
  uint16_t sequence{0};
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
  }
  SaveRTP& operator=(rtc::RTP* package)
  {
    timestamp = package->timestamp();
    ssrc = package->ssrc();
    sequence = package->seqNumber();
    body.clear();
    body.insert(
      body.end(),
      (std::byte*)package->getBody(),
      (std::byte*)package->getBody() + package->getSize()
    );
    return *this;
  }

  std::strong_ordering operator <=>(const auto& other) const {
    return (timestamp <=> other.timestamp == 0) ? sequence <=> other.sequence : timestamp<=> other.timestamp;
    //return sequence <=> other.sequence;
  }

};

class UNREALRECEIVER_EXPORT UnrealReceiver
{
public:
  using json = nlohmann::json;
  UnrealReceiver();
  ~UnrealReceiver();
  virtual void RegisterWithSignalling();
  virtual int RunForever();
  void Offer();
  virtual void UseConfig(std::string filename);
  inline const EConnectionState& State(){return this->state_;};
  virtual void SetDataCallback(const std::function<void(std::vector<std::vector<unsigned char>>)>& DataCallback);
  virtual std::vector<std::vector<unsigned char>> EmptyCache();
protected:
private:
  EConnectionState state_{EConnectionState::STARTUP};
  rtc::Configuration rtcconfig_;
  std::shared_ptr<rtc::PeerConnection> pc_;
  std::shared_ptr<rtc::DataChannel> vdc_;
  std::shared_ptr<rtc::RtcpReceivingSession> sess_;
  std::shared_ptr<rtc::Track> track_;
  rtc::Description::Video media_;
  rtc::WebSocket ss_;
  json config_;
  unsigned int MessagesReceived{0};
  unsigned int IceCandidatesReceived{0};
  bool configinit = false;
  // Freeze Frame Definitions
  bool ReceivingFreezeFrame = false;
  std::vector<std::byte> JPGFrame;

  // RTP Package info
  uint32_t timestamp;

  std::vector<std::vector<std::byte>> Storage;
  std::size_t StorageSizes{0};
  std::vector<SaveRTP> Messages;
  std::ofstream OutputFile;

  std::function<void(std::vector<std::vector<unsigned char>>)> DataCallback_;

  std::size_t AnnouncedSize;
  inline bool ReceivedFrame() { return JPGFrame.size() > AnnouncedSize; }

  bool ReceivingFrame_;
  std::size_t framenumber = 1;
};

}
