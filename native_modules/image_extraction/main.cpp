#include "DataConnector.hpp"
#include <json.hpp>
#include <iostream>

int main()
{
  auto dc = std::make_shared<WebRTCBridge::DataConnector>();
  dc->SetTakeFirstStep(true);
  using json = nlohmann::json;
  json Config = {{"SignallingIP","localhost"}, {"SignallingPort", 8080}};
  std::cout << "Sanity check: " << Config.dump() << std::endl;
  std::cout << "Fetching IP, " << Config["SignallingIP"].get<std::string>() << std::endl;
  dc->SetConfig(Config);
  dc->StartSignalling();
  while(dc->IsRunning())
  {
    std::this_thread::yield();
  }
}