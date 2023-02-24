#pragma once
#ifndef WEBRTCBRIDGE_HPP
#define WEBRTCBRIDGE_HPP
#include <json.hpp>
#include <span>
#include <variant>
#include <chrono>
#include <rtc/rtc.hpp>
#include "WebRTCBridge/export.hpp"

#define MAX_RTP_SIZE 208 * 1024

#if defined _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>

bool ParseTimeFromString(std::string Source, std::chrono::utc_time<std::chrono::system_clock::duration>& Destination);

#elif __linux__
#include <sys/socket.h>
#include <sys/types.h> 
#include <netinet/in.h>
#include <arpa/inet.h>
#include <date/date.h>
#include <fcntl.h>
bool ParseTimeFromString(std::string Source, std::chrono::time_point<std::chrono::system_clock>& Destination);

#endif

namespace WebRTCBridge
{
  // forward definitions
  class Adapter;

  long TimeSince(std::chrono::system_clock::time_point t);

  class WEBRTCBRIDGE_EXPORT JSONScheme
  {
  public:
  protected:
  private:
  };

  class WEBRTCBRIDGE_EXPORT BridgeSocket
  {
  public:
    bool Valid = false;
    bool Outgoing = false;
    std::string Address;
    void SetAddress(std::string inAddress);
    std::string GetAddress();
    void SetBlockingEnabled(bool Blocking = true);
    char* Reception;
    std::span<std::byte> BinaryData;
    std::span<std::size_t> NumberData;
    std::string_view StringData;

    std::size_t ReceivedLength;
    BridgeSocket();
    BridgeSocket(BridgeSocket&& other)=default;
    ~BridgeSocket();
    int Port;
    int GetSocketPort();
    void SetSocketPort(int Port);

    std::string What();

#ifdef _WIN32
    SOCKET Sock{INVALID_SOCKET};
    sockaddr info;
    struct sockaddr_in Addr, Remote;
#elif __linux__
    int Sock, newsockfd;
    socklen_t clilen;
    struct sockaddr_in Addr;
    struct sockaddr_in Remote;
    int n;
#endif

    // this method connects the Socket to its respective output or input
    // in the input case, the address should be set to the remote end
    // it will automatically call either bind or connect
    // remember that this class is connectionless
    bool Connect();


    int ReadSocketFromBinding();

    static BridgeSocket GetFreeSocket(std::string adr = "127.0.0.1");

    int Peek();
    virtual int Receive(bool invalidIsFailure = false);
    virtual bool Send(std::variant<rtc::binary, std::string> message);

    template < typename N >
    std::span<N> Reinterpret()
    {
      return std::span<N>(reinterpret_cast<N*>(Reception), ReceivedLength / sizeof(N));
    }
  };

#pragma pack(1)
  struct BridgeRTPHeader
  {
    // Extension Header
    uint16_t profile_id{1667};
    uint16_t length{sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint32_t) };

    // Actual extension values
    uint16_t player_id;
    uint16_t streamer_id;
    uint32_t meta;
  };
#pragma pop
  
  enum class WEBRTCBRIDGE_EXPORT EClientMessageType
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

  enum class WEBRTCBRIDGE_EXPORT EConnectionState
  {
    STARTUP = (std::uint8_t)EClientMessageType::InitialSettings + 1u,
    SIGNUP,
    OFFERED,
    CONNECTED,
    VIDEO,
    CLOSED,
    RTCERROR,
  };

  enum class WEBRTCBRIDGE_EXPORT EBridgeConnectionType
  {
    LockedMode = (std::uint8_t)EConnectionState::RTCERROR + 1u,
    BridgeMode,
    DirectMode
  };

  enum class WEBRTCBRIDGE_EXPORT EDataReceptionPolicy
  {
    TempFile = (std::uint8_t)EBridgeConnectionType::DirectMode + 1u,
    BinaryCallback,
    SynchronizedMetadata,
    AsynchronousMetadata,
    JsonCallback,
    Loss
  };

  enum class WEBRTCBRIDGE_EXPORT EMessageTimeoutPolicy
  {
    None = (std::uint8_t)EDataReceptionPolicy::Loss + 1u,
    Critical,
    All
  };

  using StreamVariant = std::variant<std::shared_ptr<rtc::DataChannel>,
    std::shared_ptr<rtc::Track>>;

  class WEBRTCBRIDGE_EXPORT NoBufferThread
  {
  public:
    const int ReceptionSize = 208 * 1024 * 1024;
    uint32_t RtpDestinationHeader{};
    EBridgeConnectionType ConnectionMode{ EBridgeConnectionType::DirectMode };
    NoBufferThread(std::shared_ptr<BridgeSocket> inSocketConnection);
    std::size_t AddRTC(StreamVariant inRTC);
    std::size_t AddRTC(StreamVariant&& inRTC);
    void Run();
  private:
    std::future<void> Thread;
    std::map<std::size_t, StreamVariant> WebRTCTracks;
    std::shared_ptr<BridgeSocket> SocketConnection;
  };

  class WEBRTCBRIDGE_EXPORT WorkerThread
  {
  public:
    WorkerThread();
    ~WorkerThread();
    void Run();
    void AddTask(std::function<void(void)>&& Task);
  private:
    std::future<void> Thread;
    std::mutex TaskMutex;
    std::queue<std::function<void(void)>> Tasks;
    std::condition_variable TaskCondition;
    bool Running = true;
  };

  class WEBRTCBRIDGE_EXPORT Bridge
  {
  public:
    using json = nlohmann::json;
    Bridge();
    virtual ~Bridge();
    virtual std::string Prefix();
    void SetTimeoutPolicy(EMessageTimeoutPolicy inPolicy, std::chrono::system_clock::duration inTimeout);
    EMessageTimeoutPolicy GetTimeoutPolicy();
    void UseConfig(std::string filename);
    void UseConfig(json Config);
    virtual void BridgeSynchronize(Adapter* Instigator,
                                   json Message, bool bFailIfNotResolved = false);
    void CreateTask(std::function<void(void)>&& Task);
    void BridgeSubmit(Adapter* Instigator, StreamVariant origin, std::variant<rtc::binary, std::string> Message) const;
    virtual void InitConnection();
    void SetHeaderByteStart(uint32_t Byte);
    virtual void BridgeRun();
    virtual void Listen();
    virtual bool CheckSignallingActive();
    virtual bool EstablishedConnection(bool Shallow = true);
    virtual void FindBridge();
    virtual void StartSignalling(std::string IP, int Port, bool keepAlive = true, bool useAuthentification = false);
    void ConfigureTrackOutput(std::shared_ptr<rtc::Track> OutputStream, rtc::Description::Media* Media);
    void SubmitToSignalling(json Message, Adapter* Endpoint);
    inline bool FindID(const json& Jason, int& ID)
    {
      for(auto it = Jason.begin(); it != Jason.end(); ++it)
      {
        for(auto name : {"id", "player_id", "app_id"})
        {
          if(it.key() == name && it.value().is_number_integer())
          {
            ID = it.value().get<int>();
            return true;
          }
        }
      }
      return false;
    }
    // This method should be used to signal to the provider
    // that a new application has connected.
    virtual uint32_t SignalNewEndpoint() = 0;
    virtual void OnSignallingMessage(std::string Message) = 0;
    virtual void RemoteMessage(json Message) = 0;
    virtual void OnSignallingData(rtc::binary Message) = 0;
    void Stop();
  protected:
    EMessageTimeoutPolicy TimeoutPolicy;
    std::chrono::system_clock::duration Timeout;
    json Config{
      {
        {"LocalPort", int()},
        {"RemotePort",int()},
        {"LocalAddress",int()},
        {"RemoteAddress",int()},
        {"Signalling",int()}
      }};
    std::unordered_map<int,std::shared_ptr<Adapter>> EndpointById;
    std::future<void> BridgeThread;
    std::mutex QueueAccess;
    std::queue<std::function<void(void)>> CommInstructQueue;
    std::future<void> ListenerThread;
    std::mutex CommandAccess;
    std::queue<std::variant<rtc::binary, std::string>> CommandBuffer;
    std::condition_variable CommandAvailable;
    bool bNeedInfo{false};
    // Signalling Server
    std::shared_ptr<rtc::WebSocket> SignallingConnection;
    EBridgeConnectionType ConnectionMode{ EBridgeConnectionType::BridgeMode };
    std::shared_ptr<NoBufferThread> DataInThread;
    struct
    {
      std::shared_ptr<BridgeSocket> In;
      std::shared_ptr<BridgeSocket> Out;
      // Data out is being called without locking!
      // There should be no order logic behind the packages, they should just be sent as-is!
      std::shared_ptr<BridgeSocket> DataOut;
    } BridgeConnection;
    std::condition_variable TaskAvaliable;
    // this will be set the first time an SDP is transmitted
    // this will be asymmetric because UE has authority
    // over the header layout
    uint32_t RtpDestinationHeader{};

    int NextID{ 0 };
  private:
    bool Run = true;
  };
}
#endif
