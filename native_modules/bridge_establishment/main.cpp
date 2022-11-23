
#include <rtc/rtc.hpp>
#include <json.hpp>

#include <span>
#include <string>
#include <iostream>

#include "Provider.hpp"
#include "Seeker.hpp"

void ProviderMain()
{
  auto BridgeProvider = std::make_shared<WebRTCBridge::Provider>();
  std::cout << "Provider Thread started" << std::endl;
}

void SeekerMain()
{

  auto BridgeSeeker = std::make_shared<WebRTCBridge::Seeker>();
  std::cout << "Seeker Thread started" << std::endl;
}


int main()
{
  using namespace std::chrono_literals;
  auto ProviderThread = std::async(std::launch::async,ProviderMain);
  std::this_thread::sleep_for(100ms);
  auto SeekerThread = std::async(std::launch::async, SeekerMain);
}
