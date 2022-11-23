#include "UnrealReceiver.hpp"
#include <fstream>
#include <chrono>
#include <thread>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <bitset>
#include <rtc/rtc.hpp>

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#elif __linux__
#include <sys/socket.h>
#include <sys/types.h> 
#include <netinet/in.h>

#endif

namespace WebRTCBridge
{


void SetTimeout(double ms, std::function<void(void)> callback, bool invokeself = false)
{
  
}

UnrealReceiver::UnrealReceiver()
{
  const unsigned int bitrate = 3000;
  const unsigned int maxframe = 10000;
  Messages.reserve(maxframe);
  rtcconfig_.maxMessageSize = MAX_RTP_SIZE;
  pc_ = std::make_shared<rtc::PeerConnection>(rtcconfig_);
  //OutputFile.open("input2_"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+".h264");
  Storage.reserve(MAX_RTP_SIZE*100u);
  //media_ = rtc::Description::Video("video", rtc::Description::Direction::RecvOnly);
  //media_.addH264Codec(96);
  //track_ = pc_->addTrack(media_);

  
#ifdef _WIN32
  SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in addr;
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  addr.sin_port = htons(5332);
  addr.sin_family = AF_INET;
#elif __linux__
  int sock, portno;
  socklen_t clilen;
  char buffer[MAX_RTP_SIZE];
  struct sockaddr_in addr, cli_addr;
  int n;
  sock = socket(AF_INET, SOCK_STREAM, 0);
  portno = 5332;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(portno);
  bind(sock, (struct sockaddr *) &addr,
              sizeof(addr));
  
#endif
  
  sess_ = std::make_shared<rtc::RtcpReceivingSession>();
  track_->setMediaHandler(sess_);
  sess_->requestBitrate(bitrate);
  sess_->requestKeyframe();
  //media_.setBitrate(bitrate);

  track_->onMessage([this, sock, addr, maxframe](rtc::message_variant message) {
    if (std::holds_alternative<rtc::binary>(message))
    {
      auto package = std::get<rtc::binary>(message);
      rtc::RTP* package_raw = reinterpret_cast<rtc::RTP*>(package.data());
      SaveRTP rtp = package_raw;

      /**
       * FROM Stackoverflow:
       * In RTP all H264 I-Frames (IDRs) are usualy fragmented.
       * When you receive RTP you first must skip the header
       * (usualy first 12 bytes) and then get to the NAL unit
       * (first payload byte). If the NAL is 28 (1C) then it
       * means that following payload represents one H264 IDR
       * (I-Frame) fragment and that you need to collect all of
       * them to reconstruct H264 IDR (I-Frame). Fragmentation
       * occurs because of the limited MTU, and much larger IDR.
       * One fragment can look like this:
       *
       * Fragment that has START BIT = 1:
       * First byte:  [ 3 NAL UNIT BITS | 5 FRAGMENT TYPE BITS]
       * Second byte: [ START BIT | END BIT | RESERVED BIT | 5 NAL UNIT BITS]
       * Other bytes: [... IDR FRAGMENT DATA...]
       *
       * Other fragments:
       * First byte:  [ 3 NAL UNIT BITS | 5 FRAGMENT TYPE BITS]
       * Other bytes: [... IDR FRAGMENT DATA...]
       *
       * To reconstruct IDR you must collect this info:
       *
       * int fragment_type = Data[0] & 0x1F;
       * int nal_type = Data[1] & 0x1F;
       * int start_bit = Data[1] & 0x80;#
       * int end_bit = Data[1] & 0x40;
       *
       *
       *
       */

      if ( Messages.size() == 0)
      {
        framenumber++;
        //if (framenumber > maxframe)
        //{
        //  state_ = EConnectionState::CLOSED;
        //
        //  std::sort(Messages.begin(),Messages.end());
        //  for (auto m : Messages)
        //  {
        //    OutputFile.write((char*)(m.body.data()),m.body.size());
        //  }
        //  OutputFile.close();
        //  exit(EXIT_SUCCESS);
        //}

        //std::cout << "Frame " << framenumber << "/" << maxframe << "." << std::endl;
      }
      else if (rtp.timestamp != Messages[Messages.size() - 1].timestamp)
      {
        std::sort(Messages.begin(), Messages.end());
        std::vector<std::byte> Data = literalbytes(1,0);
        //std::vector<std::byte> Data;
        bool PackageLoss = false;
        for (unsigned int idx = 0; idx < Messages.size(); ++idx)
        {
          const auto& m = Messages[idx];
          // temp fix for rollover without (uint)(-1) being hit
          //if (idx > 0 && Messages[idx - 1l].sequence != Messages[idx].sequence - 1 && Messages[idx].sequence != 0)
          //{
          //  std::cout << "Frame incomplete: " << Messages[idx - 1l].sequence << " preceeds " << Messages[idx].sequence << std::endl;
          //  PackageLoss = true;
          //  break;
          //}
          //Data.insert(Data.end(),m.body.begin(),m.body.end());
        }
        if(!PackageLoss)
        {
          Storage.push_back(std::move(Data));
          framenumber++;
        }
        Messages.clear();
      }

#ifdef _WIN32
      sendto(sock, reinterpret_cast<const char*>(package.data()), int(package.size()), 0,
        reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr));
#elif __linux__
      write(sock,reinterpret_cast<const void*>(package.data()),package.size());
#endif
      
      Messages.push_back(std::move(rtp));

      //std::cout << package.size() << std::endl;
      //OutputFile.close();
      //OutputFile.open("package_" + std::to_string(framenumber) + ".rtp");
      //framenumber++;
      //if (framenumber >= 50)
      //{
      //  exit(EXIT_SUCCESS);
      //}
      //OutputFile.write((char*)package.data(), package.size());
    }
  });
  rtc::DataChannelInit info;
  info.negotiated = false;
  vdc_ = pc_->createDataChannel("video",info);
  vdc_->onOpen([this]()
  {
    std::cout << "Received an open event on the data channel!" << std::endl;
    std::cout << "I have an amount of " << vdc_->availableAmount() << std::endl;
    std::cout << "I will request the initial settings." << std::endl;
    rtc::message_ptr outmessage;


    //sess_->send(rtc::make_message({(std::byte)(EClientMessageType::InitialSettings)}));

    track_->send(rtc::binary({ (std::byte)(EClientMessageType::QualityControlOwnership) }));
    track_->send(rtc::binary({ std::byte{72},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0} }));

    state_ = EConnectionState::VIDEO;
  });
  vdc_->onMessage([this](auto message)
  {
    if(std::holds_alternative<rtc::binary>(message))
    {
      if(std::get<rtc::binary>(message).size() > 0)
      {
        auto typedata = static_cast<EClientMessageType>(std::get<rtc::binary>(message)[0]);
        std::vector<std::byte> buffer = std::get<rtc::binary>(message);
        if (ReceivingFreezeFrame)
        {
          JPGFrame.insert(JPGFrame.end(), buffer.begin(), buffer.end());
          if (ReceivedFrame())
          {
            std::cout << "I have finished receiving the freezeframe" << std::endl;
            ReceivingFreezeFrame = false;
          }
        }
        else if (typedata == EClientMessageType::QualityControlOwnership)
        {

        }
        else if (typedata == EClientMessageType::FreezeFrame)
        {
          std::cout << "I have started receiving the freeze frame" << std::endl;
          //AnnouncedSize = (std::size_t)*(reinterpret_cast<int32_t*>(buffer[1]));
          //JPGFrame = buffer;
          //if (buffer.size() - 5 >= AnnouncedSize)
          //{
          //  std::cout << "FF was received in one go!" << std::endl;
          //}
          //else
          //{
          //  std::cout << "FF needs more packages!" << std::endl;
          //  ReceivingFreezeFrame = true;
          //}
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
          //std::cout << "VideoEncoderAvgQP!" << std::endl;
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
          std::cout << "ERROR data channel message with " << buffer.size() << " bytes and typebyte " << (uint32_t)typedata << "." << std::endl;
        }
      }
    }
    else
    {
      std::cout << std::get<std::string>(message) << std::endl;
    }
  });
  vdc_->onAvailable([this]()
  {
  });
  pc_->onGatheringStateChange([this](auto state) {
    std::cout << "We switched ice gathering state to " << state << std::endl;
  });
  pc_->onDataChannel([this](auto channel){
    std::cout << "PC received a data channel" << std::endl;

  });
  vdc_->onBufferedAmountLow([](){
  std::cout << "Buffer amount low" << std::endl;
  });
  pc_->onStateChange([this](auto state) {
    std::cout << "PC has a state change to " << state << std::endl;
    if (state == rtc::PeerConnection::State::Connected)
    {
      state_ = EConnectionState::CONNECTED;
    }
  });
  pc_->onTrack([this](auto track) {
    std::cout << "PC received a track" << std::endl;
  });
  pc_->onLocalCandidate([this](auto candidate)
  {
    std::cout << "Candidate!" << std::endl;
  });
  pc_->onLocalDescription([this](auto description)
  {
    std::cout << "Description!" << std::endl;
  });
  ss_.onClosed([this]()
  {
    state_ = EConnectionState::CLOSED;
  });
  sess_->onOutgoing([](rtc::message_ptr message){
    auto typedata = static_cast<EClientMessageType>((*message)[0]);
    //std::cout << "Outgoing with " << message->size() << " bytes and typebyte " << (uint32_t)typedata << "." << std::endl;
  });
  vdc_->onError([this](auto e){
    std::cout << "Video stream received an error message:\n" << e << std::endl;
    state_ = EConnectionState::RTCERROR;
  });
}

UnrealReceiver::~UnrealReceiver()
{
  //OutputFile.write((char*)Storage.data(), Storage.size());
  pc_->close();
  ss_.close();
  //OutputFile.close();
}

void UnrealReceiver::RegisterWithSignalling()
{
  if(!configinit)
  return;
  std::cout << "Registering with signalling" << std::endl;
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
          answersdp_ = sdp;
          //media_.parseSdpLine(sdp);
          //std::cout << sdp << std::endl;
          pc_->setRemoteDescription(rtc::Description(sdp,"answer"));
        }
        else if (content["type"] == "iceCandidate")
        {
          std::cout << "I received an ICE candidate!" << std::endl;
          //std::cout << content.dump() << std::endl;
          rtc::Candidate candidate(content["candidate"]["candidate"],content["candidate"]["sdpMid"]);
          pc_->addRemoteCandidate(candidate);
          IceCandidatesReceived++;
        }
      }
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
  std::cout << "Fetching config file " << filename << std::endl;
  std::ifstream f(filename);
  if(!f.good())
    return;
  config_ = json::parse(f);
  f.close();
  configinit = true;
}

void UnrealReceiver::Offer()
{
  std::cout << "Offering." << std::endl;
  auto testdescr = pc_->localDescription();
  if (testdescr.has_value())
  {
    std::cout << "We have a description value" << std::endl;
    rtc::Description desc = *testdescr;
    auto sdp = desc.generateSdp("\n");
    json outmessage = {{"type","offer"},{"sdp",sdp}};
    ss_.send(outmessage.dump());
  }
  else
  {
    std::cout << "Description is empty. Aborting." << std::endl;
    state_ = EConnectionState::RTCERROR;
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
      return 0;
    }
    else if(state_ == EConnectionState::VIDEO)
    {
      
    }
    else if (state_ == EConnectionState::RTCERROR)
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

std::string UnrealReceiver::SessionDescriptionProtocol()
{
  auto testdescr = pc_->localDescription();
  if(testdescr.has_value())
  {
    rtc::Description desc = *testdescr;
    auto sdp = desc.generateSdp("\n");
    return sdp;
  }
  else
  {
    return "";
  }
  //return answersdp_;
}

void UnrealReceiver::SetDataCallback(const std::function<void(std::vector<std::vector<unsigned char>>)>& DataCallback)
{
  std::cout << "Data Callback with pointer to " << &DataCallback << std::endl;
  DataCallback_ = DataCallback;
}

std::vector<std::vector<unsigned char>> UnrealReceiver::EmptyCache()
{
  auto size = Storage.size();
  std::vector<std::vector<unsigned char>> Data(size, std::vector<unsigned char>());
  for (unsigned int i = 0; i < size; ++i)
  {
    const std::vector<std::byte>& frame = Storage[i];
    Data[i].assign(reinterpret_cast<const unsigned char*>(frame.data()), reinterpret_cast<const unsigned char*>(frame.data())+frame.size());
  }
  Storage.clear();
  return Data;
}

}
