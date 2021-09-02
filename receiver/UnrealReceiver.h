
// FORWARD DEFINITIONS
#include <rtc/rtc.hpp>
#include <json.hpp>

enum class ConnectionState
{
  STARTUP = 0,
  OFFERED,
  CONNECTED,
  VIDEO,
  CLOSED,
  ERROR,
};

enum class ClientMessageType
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
  UnrealReceiver()=default;
  ~UnrealReceiver();
  void RegisterWithSignalling();
  void UseConfig(std::string filename);
  inline const ConnectionState& State(){return this->state_;};
protected:
private:
  ConnectionState state_{ConnectionState::STARTUP};
  rtc::Configuration rtcconfig_;
  rtc::PeerConnection pc_{rtcconfig_};
  std::shared_ptr<rtc::DataChannel> dc_;
  rtc::WebSocket ss_;
  json config_;
};