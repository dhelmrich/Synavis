
// FORWARD DEFINITIONS
#include <rtc/rtc.hpp>
#include <json.hpp>
#include <vector>
#include <fstream>
#include <compare>
#include <functional>

#include "WebRTCBridge.hpp"
#include "WebRTCBridge/export.hpp"

namespace WebRTCBridge
{
struct WEBRTCBRIDGE_EXPORT SaveRTP
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

class WEBRTCBRIDGE_EXPORT UnrealReceiver
{
public:
  using json = nlohmann::json;
  UnrealReceiver();
  ~UnrealReceiver();
  virtual void RegisterWithSignalling();
  virtual int RunForever();
  std::string SessionDescriptionProtocol();
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
  rtc::WebSocket fw_;
  json config_;
  unsigned int MessagesReceived{0};
  unsigned int IceCandidatesReceived{0};
  bool configinit = false;
  // Freeze Frame Definitions
  bool ReceivingFreezeFrame = false;
  std::vector<std::byte> JPGFrame;
  std::string answersdp_;

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

  // Bridge Connection Settings
  bool bSpawnPorts {true};
  std::vector<int> Ports;

};

}
