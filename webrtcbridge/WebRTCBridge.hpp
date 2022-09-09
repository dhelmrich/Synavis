#pragma once
#include <json.hpp>
#include <span>
#include <variant>
#include <rtc/rtc.hpp>
#include "WebRTCBridge/export.hpp"

#define MAX_RTP_SIZE 208 * 1024

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#elif __linux__
#include <sys/socket.h>
#include <sys/types.h> 
#include <netinet/in.h>

void error(const char *msg)
{
    perror(msg);
    exit(1);
}
#endif


namespace WebRTCBridge
{
  // forward definitions
  class Adapter;


  struct WEBRTCBRIDGE_EXPORT BridgeSocket
  {

    bool Valid = false;
    bool Outgoing = false;
    std::string Address;
    char* Reception;
    std::span<std::byte> BinaryData;
    std::span<std::size_t> NumberData;
    std::string_view StringData;

    std::size_t ReceivedLength;
    BridgeSocket():Reception(new char[MAX_RTP_SIZE]){}
    BridgeSocket(BridgeSocket&& other)=default;
    ~BridgeSocket()
    {
    
#ifdef _WIN32
      closesocket(Sock);
#elif defined __linux__
      shutdown(Sock,2);
#endif

    }
    int Port;
   
#ifdef _WIN32
    SOCKET Sock{INVALID_SOCKET};
    sockaddr info;
    sockaddr_in Addr;
#elif __linux__
    int Sock, newsockfd;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    int n;
#endif

    // this method connects the Socket to its respective output or input
    // in the input case, the address should be set to the remote end
    // it will automatically call either bind or connect
    // remember that this class is connectionless
    bool Connect()
    {
    #ifdef _WIN32
      int size = sizeof(Addr);
      // we are employing connectionless sockets for the transmission
      // as we are using a constant udp stream that we are forwarding
      // The important note here is that the stream is connectionless
      // and we are doing this because we do not want any recv/rep pattern
      // breaking the logical flow of the program
      // the bridge itself is also never waiting for answers and as such,
      // will use a lazy-style send/receive pattern that will will be able
      // to parse answers without needing a strict order of things to do
      // ...
      // this is also why there are so many threads in this program.
      // ...
      // That being said, I am also not super keen on this setup, so any
      // suggestion is always welcome.
      Sock = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
      Addr.sin_addr.s_addr = inet_addr(Address.c_str());
      Addr.sin_port = htons(Port);
      Addr.sin_family = AF_INET;
      getsockname(Sock,&info,&size);
      Port = *reinterpret_cast<int*>(info.sa_data);
      if(Outgoing)
      {
        if(bind(Sock,&info,sizeof(info)) == SOCKET_ERROR)
        {
          closesocket(Sock);
          WSACleanup();
          return false;
        }
        else
        {
          return true;
        }
      }
      else
      {
        if(connect(Sock,&info,sizeof(info)) == SOCKET_ERROR)
        {
          closesocket(Sock);
          WSACleanup();
          return false;
        }
        else
        {
          return true;
        }
      }
    #elif defined __linux__
      s.Sock = socket(AF_INET, SOCK_DGRAM, 0);
      if (s.Sock < 0)
      {
        return s; 
      }
      bzero((char*)&s.serv_addr, sizeof(serv_addr));
      serv_addr.sin_family = AF_INET;
      serv_addr.sin_addr.s_addr = INADDR_ANY;
      serv_addr.sin_port = htons(s.Port);
      if (bind(Sock, (struct sockaddr*)&s.serv_addr,
        sizeof(serv_addr)) < 0)
      {
        return false;
      }
    #endif

      return true;
    }

    bool Test()
    {
      
    }

    int ReadSocketFromBinding()
    {
#ifdef _WIN32
#elif defined __linux__
#endif
    }

    static BridgeSocket GetFreeSocket(std::string adr = "127.0.0.1")
    {
      BridgeSocket s;
      s.Address = adr;
      s.Valid = false;
      
    #ifdef _WIN32
      int size = sizeof(Addr);
      s.Sock = socket(AF_INET,SOCK_DGRAM,0);
      s.Addr.sin_addr.s_addr = inet_addr(adr.c_str());
      s.Addr.sin_port = htons(s.Port);
      s.Addr.sin_family = AF_INET;
      getsockname(s.Sock,&s.info,&size);
      s.Port = *reinterpret_cast<int*>(s.info.sa_data);
      return s;
    #elif __linux__
      s.Sock = socket(AF_INET, SOCK_DGRAM, 0);
      if (s.Sock < 0)
      {
        return s; 
      }
      bzero((char*)&s.serv_addr, sizeof(serv_addr));
      s.portno = 0;

      s.serv_addr.sin_family = AF_INET;
      s.serv_addr.sin_addr.s_addr = INADDR_ANY;
      s.serv_addr.sin_port = htons(s.Port);
      if (bind(s.Sock, (struct sockaddr*)&s.serv_addr,
        sizeof(s.serv_addr)) < 0)
        error("ERROR on binding");
    #endif

      s.Valid = true;
      return s;
    }

    int Peek();
    int Receive(bool invalidIsFailure = false);
    void Send(std::variant<rtc::binary, std::string> message);

    template < typename N >
    std::span<N> Reinterpret()
    {
      return std::span<N>(Reception, ReceivedLength / sizeof(N));
    }
  };
  
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

  class ApplicationTrack
  {
  public:
    const static rtc::SSRC SSRC = 42;
    ApplicationTrack(std::shared_ptr<rtc::Track> inTrack);
    void ConfigureInput(std::function<void(rtc::message_variant)>&& Handler);
    void ConfigureOutput(rtc::Description::Media* inConfig);
    std::shared_ptr<rtc::Track> Track;
    std::shared_ptr<rtc::RtcpSrReporter> SendReporter;
    rtc::Description::Video video_{"video",
      rtc::Description::Direction::SendOnly};
    void Send(std::byte* Data, unsigned int Length);
    bool Open();
  };

  using StreamVariant = std::variant<std::shared_ptr<rtc::DataChannel>,
    std::shared_ptr<ApplicationTrack>>;

  class NoBufferThread
  {
  public:
    const int ReceptionSize = 208 * 1024 * 1024;
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

  class WEBRTCBRIDGE_EXPORT Bridge
  {
  public:
    Bridge();
    virtual ~Bridge();
    void UseConfig(std::string filename);
    using json = nlohmann::json;
    virtual void BridgeSynchronize(Adapter* Instigator,
                                   nlohmann::json Message, bool bFailIfNotResolved = false);
    void CreateTask(std::function<void(void)>&& Task);
    void BridgeSubmit(Adapter* Instigator, std::variant<rtc::binary, std::string> Message) const;
    virtual void BridgeRun();
    virtual void Listen();
    virtual bool CheckSignallingActive();

    virtual bool EstablishedConnection();
    virtual void FindBridge();
    virtual void StartSignalling(std::string IP, int Port, bool keepAlive = true, bool useAuthentification = false);

    inline bool FindID(const json& Jason, int& ID)
    {
      decltype(Jason.begin()) id_entry;
      for(auto it = Jason.begin(); it != Jason.end(); ++it)
      {
        for(auto name : {"id", "player_id", "app_id"})
        {
          if(id_entry.key() == name && id_entry.value().is_number_integer())
          {
            ID = id_entry.value().get<int>();
            return true;
          }
        }
      }
      return false;
    }



    virtual void OnSignallingMessage(std::string Message) = NULL;
    virtual void OnSignallingData(rtc::binary Message) = NULL;

  protected:

    json Config{
      {
        {"LocalPort", int()},
        {"RemotePort",int()},
        {"LocalAddress",int()},
        {"RemoteAddress",int()}
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
      // Data out is being called without lockign!
      // There should be no order logic behind the packages, they should just be sent as-is!
      std::shared_ptr<BridgeSocket> DataOut;
    } BridgeConnection;

    std::condition_variable TaskAvaliable;

    int NextID{ 0 };

  };
}