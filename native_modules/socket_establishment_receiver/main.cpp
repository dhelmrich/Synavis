
#include <rtc/rtc.hpp>
#include <json.hpp>

#include <span>
#include <string>
#include <iostream>

#include "Provider.hpp"

int main()
{
  auto BridgeSocket = std::make_shared<WebRTCBridge::BridgeSocket>();
  BridgeSocket->Address = "localhost";
  BridgeSocket->Port = 51250;
  BridgeSocket->Outgoing = false;
  if(!BridgeSocket->Connect())
  {
    std::cout << "Could not connect." << std::endl;
  }
  else
  {
    std::cout << "Could connect" << std::endl;
  }
  while(true)
  {
    std::this_thread::sleep_for(std::chrono_literals::operator ""ms((unsigned long long)1000u));
    auto siz = BridgeSocket->Receive();
    if(siz > 0)
    {
      std::cout << BridgeSocket->StringData << std::endl;
    }
    else
    {
      std::cout << "Nothing received." << std::endl;
    }
  }
}
