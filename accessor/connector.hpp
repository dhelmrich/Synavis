

#include <rtc/rtc.hpp>
#include <json.hpp>
#include <vector>
#include <fstream>
#include <compare>
#include <functional>

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


    Socket VideoConnection;
    Socket AudioConnection;
    

  private:
    rtc::Configuration rtcconfig_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::DataChannel> vdc_;
    rtc::Description::Video media_;
    rtc::WebSocket ss_;
    json config_;
    unsigned int MessagesReceived{0};
    unsigned int IceCandidatesReceived{0};
  };

}