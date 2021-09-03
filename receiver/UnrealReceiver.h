
// FORWARD DEFINITIONS
#include <rtc/rtc.hpp>
#include <json.hpp>

enum class EConnectionState
{
  STARTUP = 0,
  SIGNUP,
  OFFERED,
  CONNECTED,
  VIDEO,
  CLOSED,
  ERROR,
};

enum class EClientMessageType
{
	QualityControlOwnership = 0,
	Response,
	Command,
	FreezeFrame,
	UnfreezeFrame,
	VideoEncoderAvgQP,
	LatencyTest,
	InitialSettings,
};

class UnrealReceiver
{
public:
  using json = nlohmann::json;
  UnrealReceiver();
  ~UnrealReceiver();
  void RegisterWithSignalling();
  void Offer();
  void UseConfig(std::string filename);
  inline const EConnectionState& State(){return this->state_;};
protected:
private:
  EConnectionState state_{EConnectionState::STARTUP};
  rtc::Configuration rtcconfig_;
  rtc::PeerConnection pc_;
  std::shared_ptr<rtc::DataChannel> dc_;
  rtc::WebSocket ss_;
  json config_;
};