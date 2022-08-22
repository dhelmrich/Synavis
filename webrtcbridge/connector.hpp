#pragma once

#include "rtc/rtc.hpp"
#include <json.hpp>
#include <vector>
#include <fstream>
#include <compare>
#include <functional>
#include <thread>


#include "WebRTCBridge/export.hpp"
#include "seeker.hpp"

namespace WebRTCBridge
{
  class BridgeSocket;
  class NoBufferThread;
  class ApplicationTrack;
  /*!
   * @class Connector
   * A class to connect the application-side to the Unreal server-side bridge
   * This class is intended to be used inside an Apptainer/Container environment
   * Previoud knowledge about IP environment needed
   *
   */
  class WEBRTCBRIDGE_EXPORT Connector
  {
    friend class Seeker;
  public:
    ~Connector();
    Connector(Connector&& other) = default;

    void StartSignalling(std::string IP, int Port,
        bool keepAlive = true,
        bool useAuthentification = false);

    using json = nlohmann::json;
    void StartFrameReception();

    // this is a helper function that should not be considered stable or without fault
    std::string GetConnectionString();

    std::string GenerateSDP();
    std::string Answer();

    void SetupApplicationConnection();
    void AwaitSignalling();

    void OnInformation(json message);

    std::string PushSDP(std::string);


    // Data streams to other Bridge
    // Bridge Pointer is also Shared, which means that
    // the Seeker class has to resolve the object destruction of
    // connections, which is intended anyways.
    std::shared_ptr<class Seeker> Bridge;
    std::shared_ptr<BridgeSocket> Upstream;
    std::shared_ptr<BridgeSocket> Downstream;

    // WebRTC Connectivity
    std::optional<std::shared_ptr<rtc::PeerConnection>> ApplicationConnection;
    std::optional<NoBufferThread> TransmissionThread;

    // Data streams to Application
    std::shared_ptr<ApplicationTrack> VideoToApplication;
    std::shared_ptr<ApplicationTrack> AudioToApplication;
    std::shared_ptr<rtc::DataChannel> DataToApplication;
    std::shared_ptr<rtc::DataChannel> DataFromApplication;

  protected:
    Connector();
    
  private:
    rtc::Configuration rtcconfig_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::DataChannel> vdc_;
    json config_;
    unsigned int MessagesReceived{0};
    unsigned int IceCandidatesReceived{0};
    int ID{};
    std::uint64_t Time();

    std::optional<rtc::Description> Offer_;
  };

}