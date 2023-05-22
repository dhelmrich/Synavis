#include "DataConnector.hpp"
#include <json.hpp>
#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <rtc/common.hpp>


using namespace std::chrono_literals;
using json = nlohmann::json;

void AppMain(std::string role = "receiver")
{
  json Config = { {"SignallingIP","localhost"}, {"SignallingPort", 8080} };
  std::vector <uint64_t> times;
  auto dc = std::make_shared<Synavis::DataConnector>();
  dc->Prefix = "[" + role + "]: ";
  if (role == "sender")
  {
    Config["SignallingPort"] = 8888;
    dc->SetTakeFirstStep(true);
    dc->SetOnIceGatheringFinished([wdc = std::weak_ptr< Synavis::DataConnector>(dc)]()
      {
        auto dc = wdc.lock();
        if (dc)
        {
          
        }
      });
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
    dc->SetOnIceGatheringFinished([&dc]()
      {
        //dc->CommunicateSDPs();
      });
  }
  dc->SetConfig(Config);
  dc->StartSignalling();
  while (dc->GetState() != Synavis::EConnectionState::CONNECTED)
  {
    std::this_thread::yield();
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


// This is the application to check how long it takes to send and receive a message
// over the WebRTC connection. There are two roles: the sender and the receiver.
int main(int args, char** arg)
{
  rtcInitLogger(RTC_LOG_VERBOSE,nullptr);
  auto receiver = std::async(std::launch::async, AppMain, "receiver");
  std::this_thread::sleep_for(1000ms);
  auto sender = std::async(std::launch::async, AppMain, "sender");
  sender.wait();
  receiver.wait();
  return EXIT_SUCCESS;
}