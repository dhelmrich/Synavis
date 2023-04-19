
#include <rtc/rtc.hpp>
#include <json.hpp>

#include <span>
#include <string>
#include <iostream>

#include "Provider.hpp"




int main()
{
  auto BridgeProvider = std::make_shared<WebRTCBridge::Provider>();
  
  std::cout << "Testing connection" << std::endl;
  if (BridgeProvider->EstablishedConnection())
  {
    std::cout << "Shallow testing of Bridge Connections Successful" << std::endl;
  }
  else
  {
    std::cout << "Shallow testing of Bridge Connections failed" << std::endl;
  }
}
