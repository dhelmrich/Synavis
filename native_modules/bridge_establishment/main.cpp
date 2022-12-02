
#include <rtc/rtc.hpp>
#include <json.hpp>

#include <span>
#include <string>
#include <iostream>
#include <thread>

#include "Provider.hpp"
#include "Seeker.hpp"

using namespace std::chrono_literals;

void ProviderMain()
{
  const std::string _pref = "[ProviderThread]: ";
  std::this_thread::sleep_for(2000ms);
  auto BridgeProvider = std::make_shared<WebRTCBridge::Provider>();
  std::cout << _pref  << "Provider Thread started" << std::endl;
  nlohmann::json Config{
  {
    {"LocalPort", 51250},
    {"RemotePort",51250},
    {"LocalAddress","localhost"},
    {"RemoteAddress","localhost"}
  }};
  BridgeProvider->UseConfig(Config);
  BridgeProvider->InitConnection();
  if(BridgeProvider->EstablishedConnection(false))
  {
    std::cout << _pref << "Could establish bridge connection" << std::endl;
  }
}

void SeekerMain()
{
  const std::string _pref = "[SeekerThread]:   ";
  std::this_thread::sleep_for(2000ms);
  auto BridgeSeeker = std::make_shared<WebRTCBridge::Seeker>();
  std::cout << _pref  << "Seeker Thread started" << std::endl;
  nlohmann::json Config{
  {
    {"LocalPort", 51250},
    {"RemotePort",51250},
    {"LocalAddress","localhost"},
    {"RemoteAddress","localhost"}
  }};
  BridgeSeeker->UseConfig(Config);
  BridgeSeeker->InitConnection();
  if(BridgeSeeker->EstablishedConnection(false))
  {
    std::cout << _pref << "Could establish bridge connection" << std::endl;
  }
}


int main()
{
  using namespace std::chrono_literals;
  auto ProviderThread = std::async(std::launch::async,ProviderMain);
  std::this_thread::sleep_for(10ms);
  auto SeekerThread = std::async(std::launch::async, SeekerMain);
  while(true)
  {
    std::this_thread::sleep_for(1000ms);
    std::cout << "[MainThread]: Still running" << std::endl;
    using namespace std::chrono_literals;
    auto status_bridge_thread = ProviderThread.wait_for(0ms);
    auto status_command_thread = SeekerThread.wait_for(0ms);
    if(status_bridge_thread == std::future_status::ready || status_command_thread == std::future_status::ready)
    {
      exit(-1);
    }
  }
  
}
