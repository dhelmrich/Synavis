#include "Synavis.hpp"
#include "Adapter.hpp"

#include <variant>
#include <fstream>
#include <span>

#ifdef __HAS_FORMAT
#include <format>
#else
#define FMT_HEADER_ONLY
#include <fmt/format.h>
namespace std
{
  using fmt::format;
}
#endif

#ifdef __linux__
#include <dlfcn.h>
#include <unistd.h>
#endif



static auto lbridge = Synavis::Logger::Get()->LogStarter("Bridge");


int Synavis::BridgeSocket::Receive(bool invalidIsFailure)
{
#ifdef _WIN32
  int length = sizeof(Remote);
  auto size = recvfrom(Sock, Reception, MAX_RTP_SIZE, 0, reinterpret_cast<sockaddr*>(&Remote), &length);
  //auto size = recv(Sock,Reception,MAX_RTP_SIZE,0);
  if (size < 0)
  {
    lbridge(Synavis::ELogVerbosity::Info) << "Encountered error!" << std::endl;
    return 0;
  }
  StringData = std::string_view(Reception, size);
  return size;
#elif __linux__
#include <sys/socket.h>
  socklen_t length = static_cast<socklen_t>(sizeof(Remote));
  auto size = recvfrom(Sock, Reception, MAX_RTP_SIZE, 0, reinterpret_cast<struct sockaddr*>(&Remote), &length);
  if (size < 0)
  {
    lbridge(Synavis::ELogVerbosity::Info) << "Encountered error!" << std::endl;
    return 0;
  }
  StringData = std::string_view(Reception, size);
  return 0;
#endif
}

void Synavis::ExitWithMessage(std::string Message, int Code)
{
  lbridge(Synavis::ELogVerbosity::Info) << Message << std::endl;
  exit(Code);
}

int64_t Synavis::TimeSince(std::chrono::system_clock::time_point t)
{
  std::chrono::system_clock::time_point sysnow = std::chrono::system_clock::now();
  auto orig_diff = sysnow - t;
  auto diff = std::chrono::duration_cast<std::chrono::seconds>(orig_diff);
  return diff.count();
}

double Synavis::HighRes()
{
  std::chrono::high_resolution_clock::time_point sysnow = std::chrono::high_resolution_clock::now();
  auto diff = std::chrono::duration<double>(sysnow.time_since_epoch());
  return diff.count();
}

std::string Synavis::GetLocalIP()
{
  std::string ip;
#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
  {
    // fetch error message
    auto error = WSAGetLastError();
    std::string message;
    message.resize(256, '\0');
    FormatMessage(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      0, error, 0, message.data(), 256, 0);
    lbridge(Synavis::ELogVerbosity::Info) << message << std::endl;
    return {};
  }
  else
  {
    char hostname[255];
    gethostname(hostname, sizeof(hostname));
    struct hostent* hostinfo;
    hostinfo = gethostbyname(hostname);
    ip = inet_ntoa(*(struct in_addr*)*hostinfo->h_addr_list);
  }
#elif defined __linux__
  int fd;
  struct ifreq ifr;
  fd = socket(AF_INET, SOCK_DGRAM, 0);
  ifr.ifr_addr.sa_family = AF_INET;
  strncpy(ifr.ifr_name, "ib0", IFNAMSIZ - 1);
  ioctl(fd, SIOCGIFADDR, &ifr);
  close(fd);
  ip = inet_ntoa(((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr);
#endif
  return ip;
}

std::string Synavis::FormattedTime(std::chrono::system_clock::time_point Time, bool ms)
{
  // format to YYYY_MM_DD_HH_MM_SS
  std::time_t t = std::chrono::system_clock::to_time_t(Time);
  std::tm tm;
#ifdef WIN32
  // use localtime_s
  localtime_s(&tm, &t);
#elif defined __linux__
  // use localtime_r
  localtime_r(&t, &tm);
#endif
  std::string time;
  if(ms)
  {
    // get milliseconds
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Time.time_since_epoch()).count() % 1000;
    time = std::format("{:04d}_{:02d}_{:02d}_{:02d}_{:02d}_{:02d}_{:03d}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
  }
  else
  {
    time = std::format("{:04d}_{:02d}_{:02d}_{:02d}_{:02d}_{:02d}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  }

  return time;
}

void Synavis::BridgeSocket::SetAddress(std::string inAddress)
{
  this->Address = inAddress;
}

std::string Synavis::BridgeSocket::GetAddress()
{
  return this->Address;
}

void Synavis::BridgeSocket::SetBlockingEnabled(bool Blocking)
{
  unsigned long Mode = !Blocking;
#ifdef _WIN32
  ioctlsocket(Sock, FIONBIO, &Mode);
#elif defined __linux__

  int flags = fcntl(Sock, F_GETFL, 0);
  if (flags >= 0)
  {
    if (Blocking)
    {
      flags &= (~O_NONBLOCK);
    }
    else
      flags |= O_NONBLOCK;
    fcntl(Sock, F_SETFL, flags);
}

#endif
}

Synavis::BridgeSocket::BridgeSocket() :Reception(new char[MAX_RTP_SIZE])
{
}

Synavis::BridgeSocket::~BridgeSocket()
{

#ifdef _WIN32
  auto result = shutdown(Sock, SD_BOTH);
  if (result == SOCKET_ERROR) {
    closesocket(Sock);
    WSACleanup();
  }
#elif defined __linux__
  shutdown(Sock, 2);
#endif
  delete[] Reception;
  }

int Synavis::BridgeSocket::GetSocketPort()
{
  return Port;
}

void Synavis::BridgeSocket::SetSocketPort(int Port)
{
  this->Port = Port;
}

std::string Synavis::BridgeSocket::What()
{
#ifdef _WIN32
  std::string message;
  message.resize(256, '\0');
  FormatMessage(
    FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    0, WSAGetLastError(), 0, message.data(), 256, 0);
  return message;
#elif defined __linux__

  return strerror(errno);

#endif
}

bool Synavis::BridgeSocket::Connect()
{
  if (Address == "localhost")
  {
    Address = "127.0.0.1";
  }
#ifdef _WIN32
  int size = sizeof(Addr);
  WSADATA wsaData;
  auto iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (iResult != 0)
  {
    return false;
  }
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
  // suggestion is always welcome.IPPROTO_HOPOPTS
  Sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  Addr.sin_addr.s_addr = inet_addr(Address.c_str());
  Addr.sin_port = htons(Port);
  Addr.sin_family = AF_INET;
  //getsockname(NULL,reinterpret_cast<sockaddr*>(&Addr),&size);
  //Port = *reinterpret_cast<int*>(info.sa_data);
  if (Outgoing)
  {
    if (connect(Sock, reinterpret_cast<sockaddr*>(&Addr), sizeof(sockaddr_in)) == SOCKET_ERROR)
    {
      closesocket(Sock);
      WSACleanup();
      return false;
    }
    else
    {
      this->Valid = Sock != INVALID_SOCKET;
      return this->Valid;
    }
  }
  else
  {
    if (bind(Sock, reinterpret_cast<sockaddr*>(&Addr), sizeof(sockaddr_in)) == SOCKET_ERROR)
    {
      closesocket(Sock);
      WSACleanup();
      return false;
    }
    else
    {
      this->Valid = Sock != INVALID_SOCKET;
      return this->Valid;
    }
  }
#elif defined __linux__
  Sock = socket(AF_INET, SOCK_DGRAM, 0);
  int state{ -1 };
  if (Sock < 0)
  {
    lbridge(Synavis::ELogVerbosity::Info) << "[BridgeSocket]: failed at establishing the socket" << std::endl;
    return false;
  }
  bzero((char*)&Addr, sizeof(Addr));
  Addr.sin_family = AF_INET;
  //Addr.sin_addr.s_addr <-- via parsing
  if (inet_aton(Address.c_str(), &Addr.sin_addr) == 0)
  {
    lbridge(Synavis::ELogVerbosity::Info) << "failed at parsing IP" << std::endl;
    return false;
  }
  Addr.sin_port = htons(Port);
  if (Outgoing)
  {
    if (connect(Sock, reinterpret_cast<sockaddr*>(&Addr), sizeof(sockaddr_in)) < 0)
    {
      lbridge(Synavis::ELogVerbosity::Info) << "failed at connecting" << std::endl;
      return false;
    }
  }
  else
  {
    bzero((char*)&Remote, sizeof(Remote));
    Remote.sin_family = AF_INET;
    Remote.sin_addr.s_addr = htonl(INADDR_ANY);

    if (inet_aton(Address.c_str(), &Addr.sin_addr) == 0)
    {
      lbridge(Synavis::ELogVerbosity::Info) << "failed at parsing IP" << std::endl;
      return false;
    }

    Remote.sin_port = htons(Port);
    const int option = 1;
    setsockopt(Sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(int));
    if ((state = bind(Sock, reinterpret_cast<struct sockaddr*>(&Remote),
      sizeof(sockaddr_in))) < 0)
    {
      lbridge(Synavis::ELogVerbosity::Info) << "[Receiver Thread]: Failed at binding with state: " << strerror(errno) << std::endl;
      return false;
    }
  }
  Valid = true;
  return true;
#endif

    }

void Synavis::BridgeSocket::Disconnect()
{
#ifdef _WIN32
  closesocket(Sock);
  WSACleanup();
#elif defined __linux__
  shutdown(Sock, 2);
#endif
}

int Synavis::BridgeSocket::ReadSocketFromBinding()
{
#ifdef _WIN32
  int size = sizeof(info);
  getsockname(Sock, &info, &size);
  Port = *reinterpret_cast<int*>(info.sa_data);
  return Port;
#elif defined __linux__
#endif
  return 0;
}

Synavis::BridgeSocket Synavis::BridgeSocket::GetFreeSocket(std::string adr)
{
  BridgeSocket s;
  s.Address = adr;
  s.Valid = false;

#ifdef _WIN32
  int size = sizeof(Addr);
  s.Sock = socket(AF_INET, SOCK_DGRAM, 0);
  s.Addr.sin_addr.s_addr = inet_addr(adr.c_str());
  s.Addr.sin_port = htons(s.Port);
  s.Addr.sin_family = AF_INET;
  getsockname(s.Sock, &s.info, &size);
  s.Port = *reinterpret_cast<int*>(s.info.sa_data);
  return s;
#elif __linux__
  s.Sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (s.Sock < 0)
  {
    return s;
  }
  bzero((char*)&s.Addr, sizeof(Addr));
  s.Port = 0;

  s.Addr.sin_family = AF_INET;
  s.Addr.sin_addr.s_addr = INADDR_ANY;
  s.Addr.sin_port = htons(s.Port);
  if (bind(s.Sock, (struct sockaddr*)&s.Addr,
    sizeof(s.Addr)) < 0)
    throw std::runtime_error("Could not connect to socket.");
#endif

  s.Valid = true;
  return s;
}

int Synavis::BridgeSocket::Peek()
{
#ifdef _WIN32
  SetBlockingEnabled(false);
  auto size = recv(Sock, Reception, MAX_RTP_SIZE, MSG_PEEK);
  if (size < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
  {
    return 0;
  }
  else
  {
    StringData = std::string_view(Reception, size);
  }
  SetBlockingEnabled(true);
#elif defined __linux__
  SetBlockingEnabled(false);
  auto size = recv(Sock, Reception, MAX_RTP_SIZE, 0);
  if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
  {
    return 0;
  }
  else
  {
    StringData = std::string_view(Reception, size);
  }
  SetBlockingEnabled(true);
#endif
  return size;
}

bool Synavis::BridgeSocket::Send(std::variant<rtc::binary, std::string> message)
{
  if (Outgoing && this->Valid)
  {
    const char* buffer;
    int length;
    if (std::holds_alternative<std::string>(message))
    {
      buffer = std::get<std::string>(message).c_str();
      length = static_cast<int>(std::get<std::string>(message).length());
    }
    else if (std::holds_alternative<rtc::binary>(message))
    {
      buffer = reinterpret_cast<const char*>(std::get<rtc::binary>(message).data());
      length = static_cast<int>(std::get<rtc::binary>(message).size());
    }
    int status;
    if ((status = send(Sock, buffer, length, 0)) < 0)
      //if((status = sendto(Sock, buffer, length, 0, reinterpret_cast<sockaddr*>(&Addr), sizeof(sockaddr_in))) == 0)
    {
      return false;
    }
    else
    {
      return true;
    }
  }
  else
  {
    return false;
  }
}

Synavis::CommandLineParser::CommandLineParser(int argc, char** argv)
{
  for (int i = 1; i < argc; i++) // skip program name
  {
    std::string arg = argv[i];
    if (arg[0] == '-')
    {
      // remove all leading dashes
      while (arg[0] == '-')
      {
        arg.erase(0, 1);
      }
      if (i + 1 < argc && argv[i + 1][0] != '-')
      {
        std::string value = argv[i + 1];
        Arguments[arg] = value;
      }
      else
      {
        Arguments[arg] = "";
      }
    }
  }
}

Synavis::CommandLineParser::CommandLineParser(std::vector<std::string> args)
{
  for (int i = 0; i < args.size(); i++)
  {
    std::string arg = args[i];
    if (arg[0] == '-')
    {
      // remove all leading dashes
      while (arg[0] == '-')
      {
        arg.erase(0, 1);
      }
      if (i + 1 < args.size() && args[i + 1][0] != '-')
      {
        std::string value = args[i + 1];
        this->Arguments[arg] = value;
      }
      else
      {
        this->Arguments[arg] = "";
      }
    }
  }
}

std::string Synavis::CommandLineParser::GetArgument(std::string Name)
{
  if (Arguments.find(Name) != Arguments.end())
  {
    return Arguments[Name];
  }
  else
  {
    return "";
  }
}

bool Synavis::CommandLineParser::HasArgument(std::string Name)
{
  return Arguments.find(Name) != Arguments.end();
}

Synavis::NoBufferThread::NoBufferThread(std::shared_ptr<BridgeSocket> inDataSource)
  : SocketConnection(inDataSource), Running(true)
{
  Thread = std::async(&Synavis::NoBufferThread::Run, this);
}

std::size_t Synavis::NoBufferThread::AddRTC(StreamVariant inRTC)
{

  return std::size_t();
}

std::size_t Synavis::NoBufferThread::AddRTC(StreamVariant&& inRTC)
{

  return std::size_t();
}

void Synavis::NoBufferThread::Stop()
{
  Running = false;
}

void Synavis::NoBufferThread::Run()
{
  // Consume buffer until close (this should never be empty but we never know)

  int Length;
  while (Running && (Length = SocketConnection->Receive()) > 0)
  {
    if (Length < sizeof(rtc::RtpHeader))
      continue;
    if (ConnectionMode == EBridgeConnectionType::LockedMode)
    {
      // we gather all packages and submit them together, we will also discard packages
      // that are out of order
    }
    else
    {
      // TOOD if this check fails we might run into \0 at the end of the string.

      const auto& destination = WebRTCTracks[SocketConnection->NumberData[0]];
      const auto& byte_data = SocketConnection->BinaryData;

      rtc::RtpHeader* head = reinterpret_cast<rtc::RtpHeader*>(byte_data.data());
      auto target = head->getExtensionHeader() + this->RtpDestinationHeader;


      if (std::holds_alternative<std::shared_ptr<rtc::Track>>(destination))
      {
        std::get<std::shared_ptr<rtc::Track>>(destination)->send(byte_data.data(), byte_data.size());
      }
      else
      {
        std::get< std::shared_ptr<rtc::DataChannel> >(destination)->send(byte_data.data(), byte_data.size());
      }
    }
    // This is a roundabout reinterpret_cast without having to actually do one
    //DataDestinationPtr->Send(SocketConnection->BinaryData.data(),Length);
  }
}



Synavis::WorkerThread::WorkerThread()
{
  Thread = std::async(std::launch::async, &WorkerThread::Run, this);
}

Synavis::WorkerThread::~WorkerThread()
{
  Running = false;
}

void Synavis::WorkerThread::Run()
{
  using namespace std::chrono_literals;
  std::unique_lock<std::mutex> lock(TaskMutex);
  while (Running)
  {
    TaskCondition.wait(lock, [this] {
      //lbridge(Synavis::ELogVerbosity::Info) << "Task condition " << ((Tasks.size() > 0) || !Running) << std::endl;
      return (Tasks.size() > 0) || !Running;
      });
    if (!Running) return;
    if (Tasks.size() > 0)
    {
      auto Task = std::move(Tasks.front());
      Tasks.pop();
      lock.unlock();
      Task();
      lock.lock();
    }
  }
}

void Synavis::WorkerThread::Stop()
{
  Running = false;
}

uint64_t Synavis::WorkerThread::GetTaskCount()
{
  return Tasks.size();
}

void Synavis::WorkerThread::AddTask(std::function<void()>&& Task)
{
  std::unique_lock<std::mutex> lock(TaskMutex);
  Tasks.push(Task);
  //Tasks.push([Task=std::move(Task), this]() { Task(); this->TaskCondition.notify_all(); }); // todo uncomment if there is a need for a notification
  this->TaskCondition.notify_all();
}

Synavis::Bridge::Bridge()
{
  lbridge(Synavis::ELogVerbosity::Info) << "An instance of the Synavis was started, we are starting the threads..." << std::endl;
  BridgeThread = std::async(std::launch::async, &Bridge::BridgeRun, this);
  lbridge(Synavis::ELogVerbosity::Info) << "Bridge Thread started" << std::endl;
  ListenerThread = std::async(std::launch::async, &Bridge::Listen, this);
  lbridge(Synavis::ELogVerbosity::Info) << "Listener Thread Started" << std::endl;

  BridgeConnection.In = std::make_shared<BridgeSocket>();
  BridgeConnection.Out = std::make_shared<BridgeSocket>();
  BridgeConnection.DataOut = std::make_shared<BridgeSocket>();

  SignallingConnection = std::make_shared<rtc::WebSocket>();
}

Synavis::Bridge::~Bridge()
{
  if (SignallingConnection->isOpen())
  {
    SignallingConnection->close();
  }
}

void Synavis::Bridge::SetTimeoutPolicy(EMessageTimeoutPolicy inPolicy,
  std::chrono::system_clock::duration inTimeout)
{
  this->Timeout = inTimeout;
  this->TimeoutPolicy = inPolicy;
}

void Synavis::Bridge::BridgeSynchronize(Adapter* Instigator, nlohmann::json Message, bool bFailIfNotResolved)
{
  if (Instigator != nullptr)
    Message["id"] = Instigator->ID;
  else
    Message["id"] = -1;
  std::string Transmission = Message.dump();
  BridgeConnection.Out->Send(Transmission);
  auto messagelength = BridgeConnection.In->Receive(true);
  if (messagelength <= 0)
  {
    if (bFailIfNotResolved)
    {
      throw std::domain_error(std::string("Could not receive answer from Bridgehead and this synchronization is critical:\n\n")
        + "Message was:\n\n"
        + Message.dump(1, '\t'));
    }
  }
  else
  {
    json Answer;
    try
    {
      Answer = json::parse(BridgeConnection.In->StringData);
    }
    catch (std::exception e)
    {
      if (bFailIfNotResolved)
      {
        throw std::runtime_error(std::string("An error occured while parsing the Bridge response:\n\n")
          + e.what());
      }
    }
    catch (...)
    {
      if (bFailIfNotResolved)
      {
        throw std::runtime_error("An unexpected error occured while parsing the Bridge response");
      }
    }
    if (Answer["type"] == "ok")
    {

    }
    else if (Answer["type"] == "todo")
    {
      Instigator->OnRemoteInformation(Answer);
    }
  }
}

void Synavis::Bridge::CreateTask(std::function<void()>&& Task)
{
  std::unique_lock<std::mutex> lock(QueueAccess);
  CommInstructQueue.push(Task);
  this->TaskAvaliable.notify_all();
}

void Synavis::Bridge::BridgeSubmit(Adapter* Instigator, StreamVariant origin, std::variant<rtc::binary, std::string> Message) const
{
  // we need to break this up because of json lib compatibility
  if (std::holds_alternative<std::string>(Message))
  {
    json Transmission = { {"id",Instigator->ID} };
    Transmission["data"] = std::get<std::string>(Message);
    BridgeConnection.Out->Send(Transmission.dump());
  }
  else
  {
    auto data = std::get<rtc::binary>(Message);
    if (data.size() > RtpDestinationHeader + 13)
    {
      auto* p_proxy = reinterpret_cast<uint16_t*>((data.data() + RtpDestinationHeader));
      *p_proxy = Instigator->ID;
    }
    BridgeConnection.DataOut->Send(data);
  }
}

void Synavis::Bridge::InitConnection()
{
  lbridge(Synavis::ELogVerbosity::Info) << "Init connection" << std::endl;
  BridgeConnection.In->Outgoing = false;

  BridgeConnection.In->Address = Config["RemoteAddress"].get<std::string>();
  BridgeConnection.In->Port = Config["RemotePort"].get<int>();

  lbridge(Synavis::ELogVerbosity::Info) << "Init Bridge In" << std::endl;
  if (!BridgeConnection.In->Connect())
  {
    lbridge(Synavis::ELogVerbosity::Error) << "Unexpected error when connecting to an incoming socket: " << BridgeConnection.In->What() << std::endl;
  }
  BridgeConnection.Out->Outgoing = true;
  BridgeConnection.Out->Address = Config["LocalAddress"].get<std::string>();
  BridgeConnection.Out->Port = Config["LocalPort"].get<int>();
  lbridge(Synavis::ELogVerbosity::Info) << "Init Bridge Out" << std::endl;
  if (!BridgeConnection.Out->Connect())
  {
    lbridge(Synavis::ELogVerbosity::Error) << "Unexpected error when connecting to an outgoing socket: " << BridgeConnection.Out->What() << std::endl;
  }
  /*
  BridgeConnection.DataOut->Outgoing = true;
  BridgeConnection.DataOut->Address = Config["LocalAddress"].get<std::string>();
  BridgeConnection.DataOut->Port = Config["LocalPort"].get<int>();
  lbridge(Synavis::ELogVerbosity::Info) << "Init Bridge Data Out" << std::endl;
  if(!BridgeConnection.DataOut->Connect())
  {
    lbridge(Synavis::ELogVerbosity::Info) << Prefix() << "Unexpected error when connecting to an incoming socket:"
     << std::endl << BridgeConnection.In->What() << std::endl;
  }
  */
}

void Synavis::Bridge::SetHeaderByteStart(uint32_t Byte)
{
  this->DataInThread->RtpDestinationHeader = Byte;
}

void Synavis::Bridge::BridgeRun()
{
  using namespace std::chrono_literals;
  std::unique_lock<std::mutex> lock(QueueAccess);
  while (Run)
  {
    // while((CommInstructQueue.size() == 0) && Run)
    // {
    //   lbridge(Synavis::ELogVerbosity::Info) << Prefix() << "BridgeThread entered while Run loop" << std::endl;
    //   lock.unlock(); // end critical section
    //   std::this_thread::sleep_for(10ms);
    //   lock.lock(); // enter critical section
    // }
    TaskAvaliable.wait(lock, [this] {
      lbridge(Synavis::ELogVerbosity::Info) << "BridgeThread entered while Run loop" << std::endl;
      return (CommInstructQueue.size() > 0) || !Run;
      });
    if (!Run)
    {
      lbridge(Synavis::ELogVerbosity::Verbose) << "BridgeThread exiting due to Run being false" << std::endl;
      return;
    }
    if (CommInstructQueue.size() > 0)
    {
      auto Task = std::move(CommInstructQueue.front());
      CommInstructQueue.pop();
      lock.unlock();
      Task();
      // locking at the end of the loop is necessary because next
      // top start of this scope requires there to be a locked lock.
      lock.lock();
    }
  }
  lbridge(Synavis::ELogVerbosity::Info) << "BridgeThread ended!" << std::endl;
}

void Synavis::Bridge::Listen()
{
  std::unique_lock<std::mutex> lock(CommandAccess);
  while (Run)
  {
    CommandAvailable.wait(lock, [this]
      {
        return (bNeedInfo && this->BridgeConnection.In->Peek() > 0) || !Run;
      });
    if (!Run) return;
    bool isMessage = false;
    try
    {
      // all of these things must be available and also present
      // on the same layer of the json signal
      auto message = json::parse(this->BridgeConnection.In->StringData);
      std::string type = message["type"];
      auto app_id = message["id"].get<int>();

      EndpointById[app_id]->OnRemoteInformation(message);
    }
    catch (...)
    {

    }
  }
}

bool Synavis::Bridge::CheckSignallingActive()
{
  return SignallingConnection->isOpen();
}

bool Synavis::Bridge::EstablishedConnection(bool Shallow)
{
  using namespace std::chrono_literals;
  auto status_bridge_thread = BridgeThread.wait_for(0ms);
  auto status_command_thread = ListenerThread.wait_for(0ms);
  if (!Shallow)
  {
    // check connection validity here and actually ping connected bridges
  }
  return (BridgeConnection.In->Valid && BridgeConnection.Out->Valid
    && status_bridge_thread != std::future_status::ready
    && status_command_thread != std::future_status::ready);
}

void Synavis::Bridge::FindBridge()
{

}

void Synavis::Bridge::StartSignalling(std::string IP, int Port, bool keepAlive, bool useAuthentification)
{
  using namespace std::chrono_literals;
  SignallingConnection = std::make_shared<rtc::WebSocket>();
  std::promise<void> RunGuard;
  auto Notifier = RunGuard.get_future();
  SignallingConnection->onOpen([this, &RunGuard]()
    {
      lbridge(Synavis::ELogVerbosity::Info) << "Opened Signalling" << std::endl;
      RunGuard.set_value();
    });
  SignallingConnection->onClosed([this, &RunGuard]() {});
  SignallingConnection->onError([this, &RunGuard](auto error) {});
  SignallingConnection->onMessage([this](auto message)
    {
      if (std::holds_alternative<std::string>(message))
      {
        OnSignallingMessage(std::get<std::string>(message));
      }
      else
      {
        OnSignallingData(std::get<rtc::binary>(message));
      }
    });
  if (useAuthentification)
  {
    // this is its own issue as we are probably obliged to conform to IDM guidelines here
    // these should all interface the same way, but here we should probably
    // call upon the jupyter-jsc service that should run in the background somewhere
  }
  else
  {
    std::string signalling_url = "ws://" + IP + ":" + std::to_string(Port) + "//";
    lbridge(Synavis::ELogVerbosity::Info) << "Opening Signalling Connection on " << signalling_url << std::endl;
    SignallingConnection->open(signalling_url);
  }
  lbridge(Synavis::ELogVerbosity::Info) << "Waiting for Signalling Websocket to Connect." << std::endl;
  Notifier.wait();
  lbridge(Synavis::ELogVerbosity::Info) << "Connected!" << std::endl;
}

void Synavis::Bridge::ConfigureTrackOutput(std::shared_ptr<rtc::Track> OutputStream, rtc::Description::Media* Media)
{
  // this is from media to track, as there is no direct function of configuring the track using the media
  // and the specific implementation of the rtc is largely hidden behind templated constructs.

  // there is nothing to be done here most likely, since the reporter hinge on the RTPPacketization
  // which we cannot provide as we do not actually packetize any frames
}

void Synavis::Bridge::SubmitToSignalling(json Message, Adapter* Endpoint)
{
  if (SignallingConnection->isOpen())
  {
    if (Endpoint != nullptr)
    {
      Message["id"] = Endpoint->ID;
    }
    SignallingConnection->send(Message.dump());
  }
}

void Synavis::Bridge::Stop()
{
  lbridge(Synavis::ELogVerbosity::Info) << "Stopping Bridge" << std::endl;
  Run = false;
  TaskAvaliable.notify_all();
  CommandAvailable.notify_all();
  lbridge(Synavis::ELogVerbosity::Info) << "Stopping Bridge: Signalling" << std::endl;
  if (SignallingConnection && SignallingConnection->isOpen())
    SignallingConnection->close();

  // Stop and join DataInThread if it exists
  if (DataInThread) {
    lbridge(Synavis::ELogVerbosity::Info) << "Stopping DataInThread" << std::endl;
    // If NoBufferThread has a Stop method, call it
    DataInThread->Stop();
  }
  // Join BridgeThread and ListenerThread
  if (BridgeThread.valid()) {
    lbridge(Synavis::ELogVerbosity::Info) << "Waiting for BridgeThread to finish" << std::endl;
    BridgeThread.wait();
  }
  if (ListenerThread.valid()) {
    lbridge(Synavis::ELogVerbosity::Info) << "Waiting for ListenerThread to finish" << std::endl;
    ListenerThread.wait();
  }

  lbridge(Synavis::ELogVerbosity::Info) << "Bridge stopped successfully." << std::endl;
}

Synavis::EMessageTimeoutPolicy Synavis::Bridge::GetTimeoutPolicy()
{
  return this->TimeoutPolicy;
}

void Synavis::Bridge::UseConfig(std::string filename)
{
  std::ifstream file(filename);
  auto fileConfig = json::parse(file);
  UseConfig(fileConfig);
}

void Synavis::Bridge::UseConfig(json fileConfig)
{

  this->Config = fileConfig;
  return;
  bool complete = true;
  for (auto key : Config)
    if (fileConfig.find(key) == fileConfig.end())
      complete = false;
  if (complete)
  {
    Config = fileConfig;
  }
}

#ifdef _WIN32
bool ParseTimeFromString(std::string Source, std::chrono::utc_time<std::chrono::system_clock::duration>& Destination)
{
  std::string format = "%Y-%m-%d %X";
  std::istringstream ss(Source);
  if (ss >> std::chrono::parse(format, Destination))
    return true;
  return false;
}
#elif defined __linux__
bool ParseTimeFromString(std::string Source, std::chrono::time_point<std::chrono::system_clock>& Destination)
{
  std::string format = "%Y-%m-%d %X";
  std::istringstream ss(Source);
  if (ss >> date::parse(format, Destination))
    return true;
  else return false;
}
#endif
