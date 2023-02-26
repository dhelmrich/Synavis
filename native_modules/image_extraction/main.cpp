#include "DataConnector.hpp"
#include <json.hpp>
#include <iostream>
#include <chrono>

int main()
{
  using namespace std::chrono_literals;
  auto dc = std::make_shared<WebRTCBridge::DataConnector>();
  dc->SetTakeFirstStep(true);
  using json = nlohmann::json;
  json Config = {{"SignallingIP","localhost"}, {"SignallingPort", 8080}};
  std::cout << "Sanity check: " << Config.dump() << std::endl;
  std::cout << "Fetching IP, " << Config["SignallingIP"].get<std::string>() << std::endl;
  dc->SetConfig(Config);
  dc->SetBlock(true);
  dc->StartSignalling();
  while(dc->GetState() != WebRTCBridge::EConnectionState::CONNECTED)
  {
    std::this_thread::yield();
  }
  std::cout << "Found out that we are connected" << std::endl;
  dc->PrintCommunicationData();
  //dc->SendString("test");

  dc->SendString("test");
  
  while(dc->IsRunning())
  {
    std::this_thread::yield();
  }
}
