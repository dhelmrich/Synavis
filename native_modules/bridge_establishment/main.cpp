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

/**
 * PORT SETUP FOR THE BRIDGE TO TEST ON A WORKSTATION
 * UE Ports: Streamer 5000, Signalling 8080
 * Provider Ports: In: 25552, Out 25553, Data: 25554
 * Seeker Ports: In: 25553, Out 25552, Data: 25554
 * User Ports: Relay: 25555, Signalling: 8080
 */
const json GeneralConfig = {
  {"SignallingIP", "localhost"},
  {"SignallingPort", 8080},
  {"SeekerPort", 25552},
  {"ProviderPort", 25553},
  {"DataPort", 25554},
  {"StreamerPort", 5000},
  {"RelayPort", 25555},
  {"RelayIP", "localhost"},
  {"StreamerIP", "localhost"}
  };



void ProviderMain(const json& Config)
{
  const std::string _pref = "[ProviderThread]: ";
  std::this_thread::sleep_for(2500ms);
  auto BridgeProvider = std::make_shared<Synavis::Provider>();
  std::cout << _pref  << "Provider Thread started" << std::endl;
  BridgeProvider->UseConfig(Config);
  BridgeProvider->SetTimeoutPolicy(Synavis::EMessageTimeoutPolicy::None, 10s);
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

void SeekerMain(const json& Config)
{
  const std::string _pref = "[SeekerThread]:   ";
  std::this_thread::sleep_for(2000ms);
  auto BridgeSeeker = std::make_shared<Synavis::Seeker>();
  // Switch in and out ports

  std::cout << _pref  << "Seeker Thread started" << std::endl;
  BridgeSeeker->UseConfig(Config);
  BridgeSeeker->SetTimeoutPolicy(Synavis::EMessageTimeoutPolicy::None, 10s);
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
  json Config = GeneralConfig;

  Config["LocalPort"] = Config["ProviderPort"];
  Config["RemotePort"] = Config["SeekerPort"];
  Config["LocalAddress"] = "127.0.0.1";
  Config["RemoteAddress"] = "127.0.0.1";

  using namespace std::chrono_literals;
  auto ProviderThread = std::async(std::launch::async,ProviderMain, Config);
  std::this_thread::sleep_for(50ms);
  
  Config["LocalPort"] = Config["SeekerPort"];
  Config["RemotePort"] = Config["ProviderPort"];
  Config["LocalAddress"] = "127.0.0.1";
  Config["RemoteAddress"] = "127.0.0.1";

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
