

#include "rtc/rtc.hpp"
#include "Provider.hpp"


int main()
{
  auto Bridge = std::make_shared<WebRTCBridge::Provider>();
  std::cout << "Testing connection" << std::endl;
  if(Bridge->EstablishedConnection())
  {
    std::cout << "Shallow testing of Bridge Connections Successful" << std::endl;
  }
  else
  {
    std::cout << "Shallow testing of Bridge Connections failed" << std::endl;
  }
}
