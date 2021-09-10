#include "UnrealReceiver.h"
#include <fstream>
#include <chrono>
#include <thread>
#include <sstream>
#include <algorithm>

UnrealReceiver::UnrealReceiver()
{
  OutputFile.open("Data.bin");
  Storage.reserve(1e6);
  media_ = rtc::Description::Video("video", rtc::Description::Direction::RecvOnly);
  media_.addH264Codec(96);
  track_ = pc_.addTrack(media_);
  
  sess_ = std::make_shared<rtc::RtcpReceivingSession>();
  track_->setMediaHandler(sess_);
  sess_->requestBitrate(90000);
  sess_->requestKeyframe();
  //media_.addVideoCodec(96,"mpeg");
  media_.setBitrate(90000);
  track_->onMessage([this](rtc::message_variant message) {
    if (std::holds_alternative<std::string>(message))
    {
      std::cout << "String message from data channel received: \n"
                << std::get<std::string>(message) << std::endl;
    }
    else
    {
      auto typedata = static_cast<EClientMessageType>(std::get<rtc::binary>(message)[0]);
      std::vector<std::byte> buffer = std::get<rtc::binary>(message);
      if (ReceivingFreezeFrame)
      {

        // this is a workaround, because the compiler is not able to
        // std::vector::insert the rtc::binary into itself, presumably
        // because namespace wrapping and using directives.
        //TRANSFORM(std::byte,message, JPGFrame);
        JPGFrame.insert(JPGFrame.end(), buffer.begin(),buffer.end());
        if (ReceivedFrame())
        {
          std::cout << "I have finished receiving the freezeframe" << std::endl;
          ReceivingFreezeFrame = false;
        }
      }
      else if(typedata == EClientMessageType::QualityControlOwnership)
      {

      }
      else if (typedata == EClientMessageType::FreezeFrame)
      {
        std::cout << "I have started receiving the freeze frame" << std::endl;
        AnnouncedSize = (std::size_t)*(reinterpret_cast<int32_t*>(buffer[1]));
        JPGFrame = buffer;
        if (buffer.size() - 5 >= AnnouncedSize)
        {
          std::cout << "FF was received in one go!" << std::endl;
        }
        else
        {
          std::cout << "FF needs more packages!" << std::endl;
          ReceivingFreezeFrame = true;
        }
      }
      else if (typedata == EClientMessageType::Command)
      {
        std::cout << "Command!" << std::endl;
      }
      else if (typedata == EClientMessageType::UnfreezeFrame)
      {
        std::cout << "UnfreezeFrame!" << std::endl;
      }
      else if (typedata == EClientMessageType::VideoEncoderAvgQP)
      {
        std::cout << "VideoEncoderAvgQP!" << std::endl;
      }
      else if (typedata == EClientMessageType::LatencyTest)
      {
        std::cout << "LatencyTest!" << std::endl;
      }
      else if (typedata == EClientMessageType::InitialSettings)
      {
        std::cout << "InitialSettings!" << std::endl;
      }
      else if (typedata == EClientMessageType::Response)
      {
        std::cout << "Response!" << std::endl;
      }
      else
      {
        std::cout << "Other message with " << buffer.size() << " bytes and typebyte " << (uint32_t)typedata << "." << std::endl;
        //std::move(buffer.begin(),buffer.end(),Storage.end());
        Storage.insert(Storage.end(),buffer.begin(),buffer.end());
        if (Storage.size() > 1e5)
        {
          std::cout << "Writing buffer to file" << std::endl;
          OutputFile.write(reinterpret_cast<char*>(Storage.data()),Storage.size()*sizeof(std::byte));
          std::cout << "Clearing buffer" << std::endl;
          Storage.clear();
        }
      }
    }
    });
  vdc_ = pc_.createDataChannel("video");
  vdc_->onOpen([this]()
  {
    std::cout << "Received an open event on the data channel!" << std::endl;
    std::cout << "I have an amount of " << vdc_->availableAmount() << std::endl;
    std::cout << "I will request the initial settings." << std::endl;
    rtc::message_ptr outmessage;


    //sess_->send(rtc::make_message({(std::byte)(EClientMessageType::InitialSettings)}));

    track_->send(rtc::binary({ (std::byte)(EClientMessageType::QualityControlOwnership) }));
    

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
  vdc_->onBufferedAmountLow([](){
  std::cout << "Buffer amount low" << std::endl;
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
  sess_->onOutgoing([](rtc::message_ptr message){
    auto typedata = static_cast<EClientMessageType>((*message)[0]);
    std::cout << "Outgoing with " << message->size() << " bytes and typebyte " << (uint32_t)typedata << "." << std::endl;
  });
  
  vdc_->onError([this](auto e){
    std::cout << "Video stream received an error message:\n" << e << std::endl;
    state_ = EConnectionState::ERROR;
  });
}

UnrealReceiver::~UnrealReceiver()
{
  pc_.close();
  ss_.close();
  OutputFile.close();
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
          media_.parseSdpLine(sdp);
          //std::cout << sdp << std::endl;
          pc_.setRemoteDescription(rtc::Description(sdp,"answer"));
        }
        else if (content["type"] == "iceCandidate")
        {
          std::cout << "I received an ICE candidate!" << std::endl;
          //std::cout << content.dump() << std::endl;
          rtc::Candidate candidate(content["candidate"]["candidate"],content["candidate"]["sdpMid"]);
          pc_.addRemoteCandidate(candidate);
          IceCandidatesReceived++;
        }
      }
      // 
    }
  });
  ss_.onClosed([this]()
  {
    std::cout << "Websocket closed" << std::endl;
    state_ = EConnectionState::CLOSED;
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
