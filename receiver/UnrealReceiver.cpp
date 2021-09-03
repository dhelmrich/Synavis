#include "UnrealReceiver.h"
#include<fstream>
#include <chrono>
#include <thread>
#include <sstream>


UnrealReceiver::UnrealReceiver()
{
  dc_ = pc_.createDataChannel("video");
}

UnrealReceiver::~UnrealReceiver()
{

}

void UnrealReceiver::RegisterWithSignalling()
{
  unsigned int MessagesReceived = 0;
 std::string address =  "ws://" + config_["PublicIp"].get<std::string>()
  + ":" + std::to_string(config_["HttpPort"].get<unsigned>());
  std::string addresstest = "ws://127.0.0.1:8080/";
  ss_.onOpen([this]()
  {
    std::cout << "WebSocket Open" << std::endl;
  });
  ss_.onError([](std::string e)
  {
    std::cout << e << std::endl;
  });
  ss_.onMessage([this,&MessagesReceived](std::variant<rtc::binary,rtc::string> message)
  {
    if (std::holds_alternative<rtc::string>(message)) {
      json content = json::parse(std::get<rtc::string>(message));
      std::cout << "Received Message about " << content["type"] << std::endl;

      switch (state_)
      {
      case EConnectionState::STARTUP:
        // we ignore the first two messages
        ++MessagesReceived;
        if (MessagesReceived == 2)
        {
          state_ = EConnectionState::SIGNUP;
        }
        break;
      case EConnectionState::SIGNUP:
        break;
      case EConnectionState::OFFERED:
        if (content["type"] == "answer")
        {
          std::cout << "I am parsing the answer." << std::endl;
          std::string sdp = content["sdp"];
          pc_.setRemoteDescription(rtc::Description(sdp,"answer"));
        }
        else if (content["type"] == "iceCandidate")
        {
          std::cout << "I received an ICE candidate!" << std::endl;
          std::string candidatestring = content["candidate"]["candidate"];
          std::stringstream candidatedump(candidatestring);

          
        }
        break;
      case EConnectionState::CONNECTED:
        break;
      case EConnectionState::VIDEO:
        break;
      case EConnectionState::CLOSED:
        break;
      case EConnectionState::ERROR:
      default:
        break;
      }

      // 
    }
  });
  ss_.onClosed([]()
  {
    std::cout << "Websocket closed" << std::endl;
  });
  ss_.open(addresstest);
  while(!ss_.isOpen())
  {
    std::cout << "Not open." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void UnrealReceiver::UseConfig(std::string filename)
{
  std::ifstream f(filename);
  if(!f.good())
    return;
  config_ = json::parse(f);
}

void UnrealReceiver::Offer()
{
  std::cout << "Offering." << std::endl;
  auto testdescr = pc_.localDescription();
  if (testdescr.has_value())
  {
    std::cout << "We have a description value" << std::endl;
    rtc::Description desc = *testdescr;
    auto sdp = desc.generateSdp("\n");
    std::cout << sdp << std::endl;
    json outmessage = {{"type","offer"},{"sdp",sdp}};
    ss_.send(outmessage.dump());
  }
  else
  {
    std::cout << "Description is empty. Aborting." << std::endl;
    state_ = EConnectionState::ERROR;
  }
  state_ = EConnectionState::OFFERED;
}
