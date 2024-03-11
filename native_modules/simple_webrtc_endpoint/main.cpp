#include "DataConnector.hpp"
#include <json.hpp>
#include <iostream>
#include <chrono>
#include <vector>
#include <rtc/common.hpp>

// This is the application to check how long it takes to send and receive a message
// over the WebRTC connection. There are two roles: the sender and the receiver.
int main(int args, char** arg)
{
  //rtcInitLogger(RTC_LOG_VERBOSE, nullptr);
  using namespace std::chrono_literals;
  using json = nlohmann::json;
  json Config = { {"SignallingIP","localhost"}, {"SignallingPort", 8080} };
  std::vector <uint64_t> times;
  std::string role;
  if (args < 2)
  {
    role = "receiver";
  }
  else
  {
    role = arg[1];
  }
  auto dc = std::make_shared<Synavis::DataConnector>();
  if (role == "sender")
  {
    Config["SignallingPort"] = 8888;
    dc->SetTakeFirstStep(true);
  }
  else if (role == "receiver")
  {
    dc->SetMessageCallback([&times](std::string m)
    {
      json msg = json::parse(m);
      // calculate the time difference between the current time and the timestamp
      const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      const auto diff = now - msg["timestamp"].get<uint64_t>();
      times.push_back(diff);
    });
  }
  dc->SetConfig(Config);
  dc->Initialize();
  dc->StartSignalling();
  dc->LockUntilConnected(2000);
  if (dc->GetState() != Synavis::EConnectionState::CONNECTED)
  {
    std::cout << "Connection failed" << std::endl;
  }
  if (role == "sender")
  {
    while (dc->GetState() == Synavis::EConnectionState::CONNECTED)
    {
      // prepare json message containing the unix timestamp
      json msg = { {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()} };
      dc->SendString(msg.dump());
    }
  }
}