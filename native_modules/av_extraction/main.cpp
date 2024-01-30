
#include <rtc/rtc.hpp>
#include <json.hpp>

#include <span>
#include <string>
#include <iostream>

#include "MediaReceiver.hpp"
#include "FrameDecodeAV.hpp"

using json = nlohmann::json;
inline constexpr std::byte operator "" _b(unsigned long long i) noexcept
{
  return static_cast<std::byte>(i);
}

inline std::byte operator+(std::byte b, std::byte i) noexcept
{
  return static_cast<std::byte>(static_cast<int>(b) + static_cast<int>(i));
}

inline std::byte operator++(std::byte& b) noexcept
{
  return b = b + 1_b;
}

static Synavis::Logger::LoggerInstance lmain = Synavis::Logger::Get()->LogStarter("main");


int main(int args, char** argv)
{
  using namespace std::chrono_literals;
  auto dc = std::make_shared<Synavis::MediaReceiver>();
  //rtcInitLogger(RTC_LOG_VERBOSE,nullptr);
  // if we have arguments, we check if verbose logging is requested
  Synavis::ELogVerbosity LogVerbosity = Synavis::ELogVerbosity::Error;
  Synavis::ECodec codec = Synavis::ECodec::H264;
  json Config = { {"SignallingIP","127.0.0.1"}, {"SignallingPort", 8080} };
  if (args > 1)
  {
    for (int a = 1; a < args; ++a)
    {
      std::string arg = argv[a];
      if (arg == "-h" || arg == "--help")
      {
        std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
        std::cout << "Options:" << std::endl;
        std::cout << "  -v, --verbose\t\t\tEnable verbose logging on the WebRTC backend" << std::endl;
        std::cout << "  -i, --ip <ip address>\t\tSet the IP address to connect to for webrtc" << std::endl;
        std::cout << "  -l, --loglevel <log level>\t\tSet the log level (verbose, info, debug, warning, error, silent)" << std::endl;
        std::cout << "  -c, --codec <codec>\t\t\tSet the codec to use (h264, vp8, vp9, h265)" << std::endl;
        std::cout << "  -r, --relay <ip>:<port>\t\t\tSet the relay server to use" << std::endl;
        std::cout << "  -s  --signalling <ip>:<port>\t\t\tSet the signalling server to use" << std::endl;
        std::cout << "  -h, --help\t\t\t\tShow this help" << std::endl;
        return 0;
      }
      if (arg == "-l" || arg == "--loglevel")
      {
        if (args < a + 1)
        {
          std::cout << "No log level provided" << std::endl;
          return -1;
        }
        std::cout << "Setting log level to " << argv[a + 1] << std::endl;
        std::string loglevel = argv[a + 1];
        if (loglevel == "verbose")
        {
          LogVerbosity = Synavis::ELogVerbosity::Verbose;
        }
        else if (loglevel == "info")
        {
          LogVerbosity = Synavis::ELogVerbosity::Info;
        }
        else if (loglevel == "debug")
        {
          LogVerbosity = Synavis::ELogVerbosity::Debug;
        }
        else if (loglevel == "warning")
        {
          LogVerbosity = Synavis::ELogVerbosity::Warning;
        }
        else if (loglevel == "error")
        {
          LogVerbosity = Synavis::ELogVerbosity::Error;
        }
        else if (loglevel == "silent")
        {
          LogVerbosity = Synavis::ELogVerbosity::Silent;
        }
        else
        {
          std::cout << "Unknown log level " << loglevel << std::endl;
        }
      }
      if (arg == "-v" || arg == "--verbose")
      {
        lmain(Synavis::ELogVerbosity::Debug) << "Verbose logging enabled" << std::endl;
        rtcInitLogger(RTC_LOG_VERBOSE, nullptr);
      }
      if (arg == "-i" || arg == "--ip")
      {
        if (args < a + 1)
        {
          lmain(Synavis::ELogVerbosity::Debug) << "No IP address provided" << std::endl;
          return -1;
        }
        lmain(Synavis::ELogVerbosity::Debug) << "Setting IP to " << argv[a + 1] << std::endl;
        dc->IP = argv[a + 1];
      }
      if (arg == "-c" || arg == "--codec")
      {
        if (args < a + 1)
        {
          lmain(Synavis::ELogVerbosity::Debug) << "No codec provided" << std::endl;
          return -1;
        }
        lmain(Synavis::ELogVerbosity::Debug) << "Setting codec to " << argv[a + 1] << std::endl;
        std::string strcodec = argv[a + 1];
        if (strcodec == "h264")
        {
          codec = Synavis::ECodec::H264;
        }
        else if (strcodec == "vp8")
        {
          codec = Synavis::ECodec::VP8;
        }
        else if (strcodec == "vp9")
        {
          codec = Synavis::ECodec::VP9;
        }
        else if (strcodec == "h265")
        {
          codec = Synavis::ECodec::H265;
        }
        else
        {
          codec = Synavis::ECodec::None;
        }
      }
      if (arg == "-r" || arg == "--relay")
      {
        if (args < a + 1)
        {
          lmain(Synavis::ELogVerbosity::Debug) << "No relay provided" << std::endl;
          return -1;
        }
        lmain(Synavis::ELogVerbosity::Debug) << "Setting relay to " << argv[a + 1] << std::endl;
        std::string strrelay = argv[a + 1];
        auto pos = strrelay.find(':');
        if (pos == std::string::npos)
        {
          lmain(Synavis::ELogVerbosity::Debug) << "Invalid relay " << strrelay << std::endl;
          return -1;
        }
        auto ip = strrelay.substr(0, pos);
        auto port = std::stoi(strrelay.substr(pos + 1));
        // check if ip is valid
        if (ip.find_first_not_of("0123456789.") != std::string::npos)
        {
          lmain(Synavis::ELogVerbosity::Debug) << "Invalid relay " << strrelay << std::endl;
          return -1;
        }
        dc->ConfigureRelay(strrelay.substr(0, pos), std::stoi(strrelay.substr(pos + 1)));
      }
      if (arg == "-s" || arg == "--signalling")
      {
        if (args < a + 1)
        {
          lmain(Synavis::ELogVerbosity::Debug) << "No signalling provided" << std::endl;
          return -1;
        }
        lmain(Synavis::ELogVerbosity::Debug) << "Setting signalling to " << argv[a + 1] << std::endl;
        std::string strsignalling = argv[a + 1];
        auto pos = strsignalling.find(':');
        if (pos == std::string::npos)
        {
                   lmain(Synavis::ELogVerbosity::Debug) << "Invalid signalling " << strsignalling << std::endl;
          return -1;
        }
        auto ip = strsignalling.substr(0, pos);
        auto port = std::stoi(strsignalling.substr(pos + 1));
        // check if ip is valid
        if (ip.find_first_not_of("0123456789.") != std::string::npos)
        {
          lmain(Synavis::ELogVerbosity::Debug) << "Invalid signalling " << strsignalling << std::endl;
          return -1;
        }
        // put it in the config
        Config["SignallingIP"] = ip;
        Config["SignallingPort"] = port;
      }
    }
  }
  Synavis::Logger::Get()->SetVerbosity(LogVerbosity);
  Synavis::Logger::Get()->SetupLogfile(Synavis::OpenUniqueFile("log.txt"));
  dc->SetCodec(codec);
  dc->SetTakeFirstStep(false);
  //dc->ConfigureRelay("127.0.0.1", 5535);
  std::vector<int> FrameSizes;
  std::shared_ptr<Synavis::FrameDecode> vpx;

  if(codec != Synavis::ECodec::None)
  {
    vpx = std::make_shared<Synavis::FrameDecode>(nullptr, codec);
    dc->SetFrameReceptionCallback(vpx->CreateAcceptor([&FrameSizes](rtc::binary frame_or_data)
    {
      FrameSizes.push_back(static_cast<int>(frame_or_data.size()));
    }));
    vpx->SetFrameCallback([](Synavis::FrameContent frame)
    {
      lmain(Synavis::ELogVerbosity::Debug) << "Got frame (" << frame.Width << "/" << frame.Height << ")" << std::endl;
    });
  }
  dc->SetMessageCallback([](auto message)
  {
    lmain(Synavis::ELogVerbosity::Debug) << "Got message: " << message << std::endl;
  });

  dc->SetConfig(Config);
  lmain(Synavis::ELogVerbosity::Debug) << "----------------------------------------- Connecting ----------------------------------------------------" << std::endl;

  dc->Initialize();
  dc->StartSignalling();

  while (dc->GetState() != Synavis::EConnectionState::CONNECTED)
  {
    std::this_thread::yield();
  }
  lmain(Synavis::ELogVerbosity::Debug) << "----------------------------------------- Connected ------------------------------------------------------" << std::endl;

  std::this_thread::sleep_for(std::chrono::seconds(2));

  dc->SendJSON(json({ {"type","command"},{"name","cam"}, {"camera", "scene"} }));
  dc->StartStreaming();

  while (Synavis::EConnectionState::CONNECTED == dc->GetState())
  {
    std::this_thread::sleep_for(100ms);
    //dc->SendMouseClick();
    //dc->RequestKeyFrame();
  }
  return EXIT_SUCCESS;
}
