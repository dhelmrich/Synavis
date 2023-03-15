#include "DataConnector.hpp"
#include <json.hpp>
#include <iostream>
#include <chrono>
#include <rtc/common.hpp>


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

int main()
{
  rtcInitLogger(RTC_LOG_VERBOSE, nullptr);
  std::this_thread::sleep_for(std::chrono::seconds(2));
  using namespace std::chrono_literals;
  auto dc = std::make_shared<WebRTCBridge::DataConnector>();
  dc->SetTakeFirstStep(true);
  using json = nlohmann::json;
  json Config = {{"SignallingIP","localhost"}, {"SignallingPort", 8080}};
  std::cout << "Sanity check: " << Config.dump() << std::endl;
  std::cout << "Fetching IP, " << Config["SignallingIP"].get<std::string>() << std::endl;
  dc->SetConfig(Config);
  dc->StartSignalling();
  dc->SetMessageCallback([](auto message)
    {
      std::cout << "Received message: " << message << std::endl;
    });
  dc->SetDataCallback([](auto data)
    {
      const char* dataPtr = reinterpret_cast<const char*>(data.data());
      std::string dataString(dataPtr, data.size());
      std::cout << "Received data: " << dataString << std::endl;
    });
  while(dc->GetState() != WebRTCBridge::EConnectionState::CONNECTED)
  {
    std::this_thread::yield();
  }
  std::cout << "Found out that we are connected" << std::endl;
  dc->PrintCommunicationData();
  //dc->SendString("test");

  // test all byte values
  for(std::byte b = 0_b; b < 255_b; ++b)
  {
    dc->DataChannelByte = 50_b;
    dc->SendString("whazzup");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return EXIT_SUCCESS;
}
