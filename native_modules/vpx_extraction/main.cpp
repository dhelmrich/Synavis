
#include <rtc/rtc.hpp>
#include <json.hpp>

#include <span>
#include <string>
#include <iostream>

#include "MediaReceiver.hpp"
#include "FrameDecode.hpp"

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


int main(int args, char** argv)
{
  using namespace std::chrono_literals;
  auto dc = std::make_shared<Synavis::MediaReceiver>();
  //rtcInitLogger(RTC_LOG_VERBOSE,nullptr);
  // if we have arguments, we check if verbose logging is requested
  Synavis::ELogVerbosity LogVerbosity = Synavis::ELogVerbosity::Error;
  if (args > 1)
  {
    for (int a = 1; a < args; ++a)
    {
      std::string arg = argv[a];
      if (arg == "-v" || arg == "--verbose")
      {
        std::cout << "Verbose logging enabled" << std::endl;
        rtcInitLogger(RTC_LOG_VERBOSE, nullptr);
      }
      if (arg == "-i" || arg == "--ip")
      {
        if (args < a + 1)
        {
          std::cout << "No IP address provided" << std::endl;
          return -1;
        }
        std::cout << "Setting IP to " << argv[a + 1] << std::endl;
        dc->IP = argv[a + 1];
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
    }
  }

  dc->Initialize();
  dc->SetTakeFirstStep(false);
  dc->SetLogVerbosity(Synavis::ELogVerbosity::Verbose);
  auto vpx = std::make_shared<Synavis::FrameDecode>();
  std::vector<int> FrameSizes;
  dc->SetDataCallback(vpx->CreateAcceptor([&FrameSizes](rtc::binary frame_or_data)
    {
      if (frame_or_data.size() < 10)
      {
        std::cout << std::string_view(reinterpret_cast<const char*>(frame_or_data.data()), frame_or_data.size()) << std::endl;
      }
      FrameSizes.push_back(frame_or_data.size());
      if (FrameSizes.size() > 10)
      {
        std::cout << "Last 10 frame sizes: " << std::endl;
        for (int i = FrameSizes.size() - 10; i < FrameSizes.size(); ++i)
        {
          std::cout << FrameSizes[i] << " ";
        }
        FrameSizes.clear();
      }
    }));
  vpx->SetFrameCallback([](Synavis::Frame frame)
    {
      std::cout << "Got frame (" << frame.Width << "/" << frame.Height << ")" << std::endl;
    });
  dc->SetMessageCallback([] ( auto message )
  {
    std::cout << "Got message: " << message << std::endl;
  });

  json Config = { {"SignallingIP","127.0.0.1"}, {"SignallingPort", 8080} };
  dc->SetConfig(Config);
  dc->StartSignalling();

  while (dc->GetState() != Synavis::EConnectionState::CONNECTED)
  {
    std::this_thread::yield();
  }
  dc->PrintCommunicationData();
  dc->SendJSON(json({ {"type","command"},{"name","cam"}, {"camera", "scene"} }));
  dc->SendMouseClick();

  while (Synavis::EConnectionState::CONNECTED == dc->GetState())
  {
    std::this_thread::sleep_for(10ms);
    dc->RequestKeyFrame();
  }
  return EXIT_SUCCESS;
}
