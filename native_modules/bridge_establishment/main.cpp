#include <rtc/rtc.hpp>
#include <json.hpp>

#include <span>
#include <string>
#include <iostream>
#include <thread>

#include "Provider.hpp"
#include "Seeker.hpp"

using namespace std::chrono_literals;
using json = nlohmann::json;

void ProviderMain(const json& Config)
{
  const std::string _pref = "[ProviderThread]: ";
  std::this_thread::sleep_for(2500ms);
  auto BridgeProvider = std::make_shared<WebRTCBridge::Provider>();
  std::cout << _pref  << "Provider Thread started" << std::endl;
  BridgeProvider->UseConfig(Config);
  BridgeProvider->SetTimeoutPolicy(WebRTCBridge::EMessageTimeoutPolicy::None, 10s);
  BridgeProvider->InitConnection();
  if(BridgeProvider->EstablishedConnection(false))
  {
    std::cout << _pref << "Could establish bridge connection" << std::endl;
  }
  else
  {
    std::cout << _pref << "Could not establish bridge connection" << std::endl;
    BridgeProvider->Stop();
    return;
  }
}

void SeekerMain( const json& Config)
{
  const std::string _pref = "[SeekerThread]:   ";
  std::this_thread::sleep_for(2000ms);
  auto BridgeSeeker = std::make_shared<WebRTCBridge::Seeker>();
  std::cout << _pref  << "Seeker Thread started" << std::endl;
  BridgeSeeker->UseConfig(Config);
  BridgeSeeker->SetTimeoutPolicy(WebRTCBridge::EMessageTimeoutPolicy::None, 10s);
  BridgeSeeker->InitConnection();
  if(BridgeSeeker->EstablishedConnection(false))
  {
    std::cout << _pref << "Could establish bridge connection" << std::endl;
  }
  else
  {
    std::cout << _pref << "Could not establish bridge connection" << std::endl;
    BridgeSeeker->Stop();
    return;
  }
}

int main(int args, char** argv)
{
  json Config;
  if (args < 2)
  {
    std::cout << "Please include a configuration, or the required data." << std::endl;
    return 1;
  }
  else if(strcmp(argv[1], "json") == 0 && args >= 3)
  {
    try
    {
      Config = json::parse(argv[2]);
    }
    catch (const std::exception& e)
    {
      std::cerr << e.what() << '\n';
      return 1;
    }
  }
  else
  {
    std::cout << "I could not parse a configuration, or the required data." << std::endl;
    return 1;
  }
  using namespace std::chrono_literals;
  auto ProviderThread = std::async(std::launch::async,ProviderMain, Config);
  std::this_thread::sleep_for(10ms);
  auto SeekerThread = std::async(std::launch::async, SeekerMain, Config);
  while(true)
  {
    std::this_thread::sleep_for(1000ms);
    //std::cout << "[MainThread]: Still running" << std::endl;
    using namespace std::chrono_literals;
    auto status_bridge_thread = ProviderThread.wait_for(0ms);
    auto status_command_thread = SeekerThread.wait_for(0ms);
    if(status_bridge_thread == std::future_status::ready || status_command_thread == std::future_status::ready)
    {
      exit(0);
    }
  }
  //ProviderMain();
  // SeekerMain();
}
