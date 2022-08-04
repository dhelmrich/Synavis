#pragma once

#include "rtc/rtc.hpp"
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
  class ApplicationTrack
  {
  public:
    ApplicationTrack(std::shared_ptr<rtc::Track> inTrack,
      std::shared_ptr<rtc::RtcpSrReporter> inSendReporter);
    std::shared_ptr<rtc::Track> Track;
    std::shared_ptr<rtc::RtcpSrReporter> SendReporter;
    rtc::Description::Video video_{"video",
      rtc::Description::Direction::SendOnly};
    const rtc::SSRC ssrc_ = 42;
    void Send(std::byte* Data, unsigned int Length);
    bool Open();
  };

  class NoBufferThread
  {
  public:
    const int ReceptionSize = 208 * 1024;
    NoBufferThread(std::weak_ptr<ApplicationTrack> inDataDestination,
      std::weak_ptr<BridgeSocket> inDataSource);
    void Run();
  private:
    std::unique_ptr<std::thread> Thread;
    std::tuple<
      std::weak_ptr<ApplicationTrack>,
      std::weak_ptr<ApplicationTrack>,
      std::weak_ptr<ApplicationTrack>
    > WebRTCTracks;
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
    friend class Seeker;
  public:
    ~Connector();

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

    void OnBridgeInformation(json message);

    std::string PushSDP(std::string);


    // Data streams to other Bridge
    // Bridge Pointer is also Shared, which means that
    // the Seeker class has to resolve the object destruction of
    // connections, which is intended anyways.
    std::shared_ptr<class Seeker> BridgePointer;
    std::shared_ptr<BridgeSocket> Connection;

    // Signalling Server
    std::shared_ptr<rtc::WebSocket> SignallingConnection;

    // WebRTC Connectivity
    std::optional<std::shared_ptr<rtc::PeerConnection>> ApplicationConnection;
    std::optional<NoBufferThread> AudioTransmissionThread;
    std::optional<NoBufferThread> VideoTransmissionThread;
    std::optional<NoBufferThread> DataThread;

    // Data streams to Application
    std::optional<std::shared_ptr<ApplicationTrack>> VideoToApplication;
    std::optional<std::shared_ptr<ApplicationTrack>> AudioToApplication;
    std::optional<std::shared_ptr<rtc::DataChannel>> DataToApplication;
    std::optional<std::shared_ptr<rtc::DataChannel>> DataFromApplication;

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
  };

}