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

int main(int args, char** argv)
{
  // if we have arguments, we check if verbose logging is requested
  if (args > 1)
  {
    std::string arg = argv[1];
    if (arg == "-v")
    {
      //rtcInitLogger(RTC_LOG_VERBOSE, nullptr);
    }
  }
  std::this_thread::sleep_for(std::chrono::seconds(2));
  using namespace std::chrono_literals;
  auto dc = std::make_shared<Synavis::DataConnector>();
  dc->SetTakeFirstStep(true);
  using json = nlohmann::json;


  bool bWantData = false;
  json Data;
  std::vector<std::string> Messages;


  json Config = { {"SignallingIP","localhost"}, {"SignallingPort", 8080} };
  std::cout << "Sanity check: " << Config.dump() << std::endl;
  std::cout << "Fetching IP, " << Config["SignallingIP"].get<std::string>() << std::endl;
  dc->SetConfig(Config);
  dc->StartSignalling();
  dc->SetMessageCallback([&bWantData, &Messages](auto message)
    {
      Messages.push_back(message);
      std::cout << "Received message: " << message << std::endl;
    });
  dc->SetDataCallback([&bWantData, &Data](auto data)
    {
      const char* dataPtr = reinterpret_cast<const char*>(data.data());
      std::string_view dataView(dataPtr, data.size());
      // double check if this might still be a json object
      try
      {
        json j = json::parse(dataView);
        std::cout << "Received json." << std::endl;
      }
      catch (...)
      {
        std::cout << "Received data: " << dataView << std::endl;
      }
    });
  while (dc->GetState() != Synavis::EConnectionState::CONNECTED)
  {
    std::this_thread::yield();
  }
  std::cout << "Found out that we are connected" << std::endl;
  dc->PrintCommunicationData();
  //dc->SendString("test");

  std::this_thread::sleep_for(1s);

  for (int i = 10; i < 11; ++i)
  {
    std::vector<double> TestGeometry(3000 * i);
    // fill with increasing numbers
    std::generate(TestGeometry.begin(), TestGeometry.end(), [n = 0]() mutable { return n++; });
    auto encoded = Synavis::Encode64(TestGeometry);
    std::cout << "Encoded: " << encoded << std::endl;
    dc->SendFloat64Buffer(TestGeometry, "points", "base64");
    
    while (Messages.size() == 0)
    {
      std::this_thread::sleep_for(10ms);
    }
    // remove last message
    auto message = Messages.back();
    if(json::parse(message)["type"] == "error")
    {
      std::cout << "Error received: " << message << std::endl;
      return EXIT_FAILURE;
    }
    Messages.clear();
    delete [] encoded.data();
    std::this_thread::sleep_for(1s);
  }
  return EXIT_SUCCESS;
}
