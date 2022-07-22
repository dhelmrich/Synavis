

#include <rtc/rtc.hpp>
#include <json.hpp>
#include <vector>
#include <fstream>
#include <compare>
#include <functional>
#include <thread>

#include "accessor/export.hpp"

#include "seeker.hpp"

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

  struct ApplicationTrack
  {
    std::shared_ptr<rtc::Track> Track;
    std::shared_ptr<rtc::RtcpSrReporter> SendReporter;
    ApplicationTrack(std::shared_ptr<rtc::Track> inTrack, std::shared_ptr<rtc::RtcpSrReporter> inSendReporter)
      : Track(inTrack), SendReporter(inSendReporter) {}
  };

  class NoBufferThread
  {
  public:
    NoBufferThread(std::weak_ptr<ApplicationTrack> inDataDestination, std::weak_ptr<BridgeSocket> inDataSource);
    void Run();
  private:
    std::unique_ptr<std::thread> Thread;
    std::weak_ptr<ApplicationTrack> DataDestination;
    std::weak_ptr<BridgeSocket> DataSource;
  };

  /*!
   * @class Connector
   * A class to connect the application-side to the Unreal server-side bridge
   * This class is intended to be used inside an Apptainer/Container environment
   * Previoud knowledge about IP environment needed
   *
   */
  class ACCESSOR_EXPORT Connector
  {
  public:
    using json = nlohmann::json;
    void StartFrameReception();

    // this is a helper function that should not be considered stable or without fault
    std::string GetConnectionString();

    std::string GenerateSDP();
    std::string Answer();

    void SetupApplicationConnection();
    void AwaitSignalling();
    std::string ProcessedSDP(std::string);

    void BridgeSynchronize(std::variant<std::byte, std::string> Message, bool bFailIfNotResolved = false);

    // Data streams to other Bridge
    std::shared_ptr<BridgeSocket> VideoConnection;
    std::shared_ptr<BridgeSocket> AudioConnection;
    std::shared_ptr<BridgeSocket> DataConnection;

    // WebRTC Connectivity
    std::optional<std::shared_ptr<rtc::PeerConnection>> ApplicationConnection;
    std::optional<NoBufferThread> AudioTransmissionThread;
    std::optional<NoBufferThread> VideoTransmissionThread;
    std::optional<NoBufferThread> DataThread;

    // Data streams to Application
    std::optional<std::shared_ptr<ApplicationTrack>> VideoToApplication;
    std::optional<std::shared_ptr<ApplicationTrack>> AudioToApplication;
    std::optional<std::shared_ptr<rtc::DataChannel>> DataToApplication;
    
  private:
    rtc::Configuration rtcconfig_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::DataChannel> vdc_;
    rtc::Description::Video media_;
    rtc::WebSocket ss_;
    json config_;
    unsigned int MessagesReceived{0};
    unsigned int IceCandidatesReceived{0};
    std::uint64_t Time();
  };

}