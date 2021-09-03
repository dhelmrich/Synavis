#include "UnrealReceiver.h"
#include<fstream>
#include <chrono>
#include <thread>
#include <sstream>



UnrealReceiver::UnrealReceiver()
{
  
  rtc::Description::Video media("video", rtc::Description::Direction::SendRecv);
  media.addH264Codec(96);
  pc_.addTrack(media);
  vdc_ = pc_.createDataChannel("video");
  vdc_->onOpen([this]()
  {
    std::cout << "Received an open event on the data channel!" << std::endl;
    std::cout << "I have an amount of " << vdc_->availableAmount() << std::endl;
    state_ = EConnectionState::VIDEO;
  });
  vdc_->onMessage([this](auto data)
  {
      if (std::holds_alternative<std::string>(data))
        std::cout << "String message from data channel received: \n" << std::get<std::string>(data) << std::endl;
      else
        std::cout << "Binary message from data channel received, size=" << std::get<rtc::binary>(data).size() << std::endl;
  });
  vdc_->onAvailable([this]()
    {
      std::cout << "Received an available event on the data channel!" << std::endl;
    });
  pc_.onGatheringStateChange([this](auto state) {
    std::cout << "We switched ice gathering state to " << state << std::endl;
  });
  pc_.onDataChannel([this](auto channel){
    std::cout << "PC received a data channel" << std::endl;

  });
  pc_.onStateChange([this](auto state) {
    std::cout << "PC has a state change to " << state << std::endl;
    if (state == rtc::PeerConnection::State::Connected)
    {
      state_ = EConnectionState::CONNECTED;
    }
    });
  pc_.onTrack([this](auto track) {
    std::cout << "PC received a track" << std::endl;
    sess_ = std::make_shared<rtc::RtcpReceivingSession>();
    track->setMediaHandler(sess_);
    track->onMessage([this](auto message){
      std::cout << "Received a message in track" << std::endl;
    });
  });
  pc_.onLocalCandidate([this](auto candidate)
  {
    std::cout << "Candidate!" << std::endl;
  });
  pc_.onLocalDescription([this](auto description)
  {
    std::cout << "Description!" << std::endl;
  });

  ss_.onClosed([this]()
  {
    state_ = EConnectionState::CLOSED;
  });
}

UnrealReceiver::~UnrealReceiver()
{
  pc_.close();
  ss_.close();
}

void UnrealReceiver::RegisterWithSignalling()
{
 std::string address =  "ws://" + config_["PublicIp"].get<std::string>()
  + ":" + std::to_string(config_["HttpPort"].get<unsigned>());
  std::string addresstest = "ws://127.0.0.1:8080/";
  ss_.onOpen([this]()
  {
    std::cout << "WebSocket Open" << std::endl;
  });
  ss_.onError([](auto e)
  {
    std::cout << e << std::endl;
  });
  ss_.onMessage([this](auto message)
  {
    if (std::holds_alternative<rtc::string>(message)) {
      json content = json::parse(std::get<rtc::string>(message));
      std::cout << "Received Message about " << content["type"] << std::endl;

      if(state_ == EConnectionState::STARTUP)
      {
        // we ignore the first two messages
        ++MessagesReceived;
        if (MessagesReceived == 2)
        {
          state_ = EConnectionState::SIGNUP;
        }
      }
      else if(state_ == EConnectionState::OFFERED)
      {
        if (content["type"] == "answer")
        {
          std::cout << "I am parsing the answer." << std::endl;
          std::string sdp = content["sdp"];
          pc_.setRemoteDescription(rtc::Description(sdp,"answer"));
        }
        else if (content["type"] == "iceCandidate")
        {
          std::cout << "I received an ICE candidate!" << std::endl;
          rtc::Candidate candidate(content["candidate"]["candidate"],content["candidate"]["sdpMid"]);
          pc_.addRemoteCandidate(candidate);
          IceCandidatesReceived++;
        }
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

int UnrealReceiver::RunForever()
{
  while (true)
  {
    
    if (state_ == EConnectionState::SIGNUP)
    {
      Offer();
    }
    else if(state_ == EConnectionState::VIDEO)
    {
      vdc_->send("Plz?"); // This crashes the engine, not that I expected anything else tbh.
    }
    else if (state_ == EConnectionState::ERROR)
    {
      std::cout << "Shutting down due to error." << std::endl;
      return EXIT_FAILURE;
    }
    else if (state_ == EConnectionState::CLOSED)
    {
      return EXIT_SUCCESS;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return EXIT_SUCCESS;
}
