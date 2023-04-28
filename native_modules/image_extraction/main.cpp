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
  auto dc = std::make_shared<WebRTCBridge::DataConnector>();
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
  dc->SetMessageCallback([&bWantData,&Messages](auto message)
    {
      Messages.push_back(message);
      std::cout << "Received message: " << message << std::endl;
      if (bWantData)
      {
        bWantData = false;
      }
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
      if (bWantData)
      {
        bWantData = false;
      }
    });
  while (dc->GetState() != WebRTCBridge::EConnectionState::CONNECTED)
  {
    std::this_thread::yield();
  }
  std::cout << "Found out that we are connected" << std::endl;
  dc->PrintCommunicationData();
  //dc->SendString("test");

  std::this_thread::sleep_for(2s);

  json Command = { {"type","query"} };

  // test all byte values
  dc->DataChannelByte = 50_b;
  std::cout << "Sending command: " << Command.dump() << std::endl;
  dc->SendJSON(Command);
  std::cout << "Waiting for any answer" << std::endl;
  while (Messages.size() == 0)
  {
    std::this_thread::sleep_for(100ms);
  }
  // remove last message
  auto message = Messages.back();
  Messages.clear();
  json j = json::parse(message);
  // get a random element from the array j["data"]
  auto randomElement = j["data"].at(rand() % j["data"].size());
  // send a query for the random element
  json Query = { {"type","query"}, {"object",randomElement} };
  std::string debug_string_rep = Query.dump();
  std::cout << "Sending query: " << debug_string_rep << std::endl;
  dc->SendJSON(Query);
  while (Messages.size() == 0)
  {
    std::this_thread::sleep_for(100ms);
  }
  auto message2 = Messages.back();
  std::cout << "Received message: " << message2 << std::endl;

  return EXIT_SUCCESS;
}
