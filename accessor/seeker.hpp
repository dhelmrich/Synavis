#pragma once 
#include "rtc/rtc.hpp"
#include <json.hpp>
#include <vector>
#include <fstream>
#include <compare>
#include <functional>

#define MAX_RTP_SIZE 208 * 1024

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
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


#include "accessor/export.hpp"


namespace AC
{

  // forward definitions
  class Connector;

  struct ACCESSOR_EXPORT BridgeSocket
  {

    bool Valid = false;
    std::string Address;
    char* Reception;
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
    SOCKET Sock;
    sockaddr info;
    sockaddr_in Addr;
#elif __linux__
    int Sock, newsockfd;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    int n;
#endif

    bool Connect()
    {
    #ifdef _WIN32
      int size = sizeof(Addr);
      Sock = socket(AF_INET,SOCK_DGRAM,0);
      Addr.sin_addr.s_addr = inet_addr(Address.c_str());
      Addr.sin_port = htons(Port);
      Addr.sin_family = AF_INET;
      getsockname(Sock,&info,&size);
      Port = *reinterpret_cast<int*>(info.sa_data);
      if(bind(Sock,&info,sizeof(info)) < 0)
      {
        return false;
      }
      else
      {
        return true;
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

    int ReadSocketFromBinding()
    {
#ifdef _WIN32
#elif defined __linux__
#endif
    }

    static BridgeSocket GetFreeSocketPort(std::string adr = "127.0.0.1")
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
    std::byte* Package();
    std::string Copy();
    void Send(std::variant<std::byte, std::string> message);
  };
  
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


  class ACCESSOR_EXPORT Seeker: std::enable_shared_from_this<Seeker>
  {
  public:
    using json = nlohmann::json;
    Seeker();
    ~Seeker();
    virtual bool CheckSignallingActive();
    virtual void UseConfig(std::string filename);
    virtual bool EstablishedConnection();
    virtual void FindBridge();
    virtual void RecoverConnection();

    virtual std::shared_ptr<Connector> CreateConnection();
    virtual void DestroyConnection(std::shared_ptr<Connector> Connector);

    void ConfigureUpstream(Connector* Instigator, const json& Answer);

    virtual void CreateTask(std::function<void(void)>&& Task);
    virtual void BridgeSynchronize(AC::Connector* Instigator,
                           json Message, bool bFailIfNotResolved = false);
    void BridgeSubmit(AC::Connector* Instigator, std::variant<std::byte, std::string> Message) const;
    void BridgeRun();
    void Listen();

  protected:

    json Config{
      {
        {"LocalPort", int()},
        {"RemotePort",int()},
        {"LocalAddress",int()},
        {"RemoteAddress",int()}
      }};

    std::unordered_map<int,std::shared_ptr<Connector>> UserByID;
    std::vector<std::shared_ptr<Connector>> Users;
    std::unique_ptr<std::thread> BridgeThread;
    std::mutex QueueAccess;
    std::queue<std::function<void(void)>> CommInstructQueue;
    std::unique_ptr<std::thread> ListenerThread;
    std::mutex CommandAccess;
    std::queue<std::variant<std::byte, std::string>> CommandBuffer;
    std::condition_variable CommandAvailable;
    bool bNeedInfo{false};

    struct
    {
      std::shared_ptr<BridgeSocket> In;
      std::shared_ptr<BridgeSocket> Out;
    } BridgeConnection;
    
    std::condition_variable TaskAvaliable;

    int NextID{0};
  }; 
  
}
