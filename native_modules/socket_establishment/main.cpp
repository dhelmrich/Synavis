
#include <rtc/rtc.hpp>
#include <json.hpp>

#include <span>
#include <string>
#include <iostream>
#include <future>
#include <thread>

#include "Provider.hpp"

void Sender()
{
  using namespace std::chrono_literals;
  auto BridgeSocket = std::make_shared<WebRTCBridge::BridgeSocket>();
  BridgeSocket->Address = "127.0.0.1";
  BridgeSocket->Port = 81;
  BridgeSocket->Outgoing = true;
  if(!BridgeSocket->Connect())
  {
    std::cout << "[Sender Thread]: Could not connect." << std::endl;
  }
  else
  {
    std::cout << "[Sender Thread]: Could connect" << std::endl;
  }
  while(true)
  {
    std::this_thread::sleep_for(1000ms);
    if(!BridgeSocket->Send("Hello!"))
    {
      std::cout << "[Sender Thread]: What? " << BridgeSocket->What() << "!" << std::endl;
    }
    std::cout << "[Sender Thread]: Send Hello!" << std::endl;
  }
}

void Receiver()
{
  using namespace std::chrono_literals;
  auto BridgeSocket = std::make_shared<WebRTCBridge::BridgeSocket>();
  BridgeSocket->Address = "127.0.0.1";
  BridgeSocket->Port = 81;
  BridgeSocket->Outgoing = false;
  if(!BridgeSocket->Connect())
  {
    std::cout << "[Receiver Thread]: Could not connect." << std::endl;
  }
  else
  {
    std::cout << "[Receiver Thread]: Could connect" << std::endl;
  }
  while(true)
  {
    std::this_thread::sleep_for(50ms);
    //std::cout << "[Receiver Thread]: Going to peek now!" << std::endl;
    auto siz = BridgeSocket->Peek();
    //std::cout << "[Receiver Thread]: BridgeSocket returned " << siz << std::endl;
    if(siz > 0)
    {
      std::cout << "[Receiver Thread]: Stringview is " << BridgeSocket->StringData.size() << " large and contains " << BridgeSocket->StringData << std::endl;
    }
  }
}

int main()
{
  using namespace std::chrono_literals;
  auto ReceiverThread = std::async(std::launch::async,Receiver);
  std::cout << "Receiver Thread started" << std::endl;
  std::this_thread::sleep_for(100ms);
  auto SenderThread = std::async(std::launch::async, Sender);
  std::cout << "Sender Thread started" << std::endl;
}
