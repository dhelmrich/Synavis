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
    BridgeSocket():Reception(new char[MAX_RTP_SIZE]){}
    
#ifdef _WIN32
    int Port;

    SOCKET Sock;
    sockaddr_in Addr;
#elif __linux__
    int Sock, newsockfd, portno;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    int n;
#endif

    static BridgeSocket GetFreeSocketPort(std::string adr = "127.0.0.1")
    {
      BridgeSocket s;
      s.Address = adr;
      s.Valid = false;
      
#ifdef _WIN32
      sockaddr info;
      int size = sizeof(Addr);
      s.Sock = socket(AF_INET,SOCK_DGRAM,0);
      s.Addr.sin_addr.s_addr = inet_addr(adr.c_str());
      s.Addr.sin_port = htons(0);
      s.Addr.sin_family = AF_INET;
      getsockname(s.Sock,&info,&size);
      s.Port = *reinterpret_cast<int*>(info.sa_data);
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
      s.serv_addr.sin_port = htons(s.portno);
      if (bind(s.Sock, (struct sockaddr*)&s.serv_addr,
        sizeof(s.serv_addr)) < 0)
        error("ERROR on binding");
#endif

      s.Valid = true;
      return s;
    }

    int Receive(bool invalidIsFailure = false);

    std::byte* Package();
  };

  struct ACCESSOR_EXPORT SaveRTP
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
    virtual bool EstablishedConnection(std::string ip = "127.0.0.1");
    virtual void FindBridge();
    virtual void RecoverConnection();

    virtual std::shared_ptr<Connector> CreateConnection();
    virtual void DestroyConnection(std::shared_ptr<Connector> Connector);

    void CreateTask(std::function<void(void)> Task);
    void BridgeSynchronize(std::shared_ptr<Connector> Instigator,
                           std::variant<std::byte, std::string> Message, bool bFailIfNotResolved = false);
    void BridgeSubmit(std::variant<std::byte, std::string> Message);
    void BridgeRun();

  protected:
    std::vector<std::shared_ptr<Connector>> Users;
    std::unique_ptr<std::thread> BridgeThread;
    std::mutex QueueAccess;
    std::queue<std::function<void(void)>> CommInstructQueue;


    
    std::condition_variable TaskAvaliable;
    
  }; 
  
}
