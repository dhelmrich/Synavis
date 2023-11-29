
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
  // if we have arguments, we check if verbose logging is requested
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
    }
  }
  dc->Initialize();
  dc->SetTakeFirstStep(false);
  dc->SetLogVerbosity(Synavis::ELogVerbosity::Warning);
  auto vpx = std::make_shared<Synavis::FrameDecode>();
  dc->SetDataCallback(vpx->CreateAcceptor([](rtc::binary frame_or_data)
    {
      if(frame_or_data.size() > 10)
        std::cout << "Got frame or data" << std::endl;
    }));
  vpx->SetFrameCallback([](Synavis::Frame frame)
    {
      std::cout << "Got frame (" << frame.Width << "/" << frame.Height << ")" << std::endl;
    });


  json Config = { {"SignallingIP","127.0.0.1"}, {"SignallingPort", 8080} };
  dc->SetConfig(Config);
  dc->StartSignalling();

  while (dc->GetState() != Synavis::EConnectionState::CONNECTED)
  {
    std::this_thread::yield();
  }
  dc->PrintCommunicationData();

  while (Synavis::EConnectionState::CONNECTED == dc->GetState())
  {
    std::this_thread::sleep_for(10ms);
  }
  return EXIT_SUCCESS;
}
