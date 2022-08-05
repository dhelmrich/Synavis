#include "connector.hpp"

#include "rtc/peerconnection.hpp"
#include "rtc/rtcpsrreporter.hpp"
#include "rtc/rtp.hpp"

#ifdef _WIN32
#elif __linux__
#endif

int AC::BridgeSocket::Receive(bool invalidIsFailure)
{
#ifdef _WIN32
  return 0;
#elif __linux__
  return 0;
#endif
}

std::byte* AC::BridgeSocket::Package() 
{
  return reinterpret_cast<std::byte*>(Reception);
}

AC::ApplicationTrack::ApplicationTrack(std::shared_ptr<rtc::Track> inTrack,
      std::shared_ptr<rtc::RtcpSrReporter> inSendReporter)
      : Track(inTrack), SendReporter(inSendReporter) {}

void AC::ApplicationTrack::Send(std::byte* Data, unsigned Length)
{
  auto rtp = reinterpret_cast<rtc::RtpHeader*>(Data);
  rtp->setSsrc(ssrc_);
  Track->send(Data, Length);
}

bool AC::ApplicationTrack::Open()
{
  return Track->isOpen();
}

AC::NoBufferThread::NoBufferThread(std::weak_ptr<ApplicationTrack> inDataDestination,
                                   std::weak_ptr<BridgeSocket> inDataSource)
    : DataDestination(inDataDestination), DataSource(inDataSource)
{
  Thread = std::make_unique<std::thread>(&AC::NoBufferThread::Run,this);
}

void AC::NoBufferThread::Run()
{
  // Consume buffer until close (this should never be empty but we never know)
  auto DataDestinationPtr = DataDestination.lock();
  auto DataSourcePtr = DataSource.lock();
  int Length;
  while((Length = DataSourcePtr->Receive()) > 0)
  {
    if (Length < sizeof(rtc::RtpHeader) || !DataDestinationPtr->Open())
      continue;
    DataDestinationPtr->Send(DataSourcePtr->Package(),Length);
  }
}

std::string AC::Connector::GetConnectionString()
{
  return std::string();
}

std::string AC::Connector::GenerateSDP()
{
  return std::string();
}

void AC::Connector::SetupApplicationConnection()
{
  pc_ = std::make_shared<rtc::PeerConnection>();
  pc_->onStateChange([this](rtc::PeerConnection::State inState)
  {
    
  });
  pc_->onGatheringStateChange([this](rtc::PeerConnection::GatheringState state) {
			if (state == rtc::PeerConnection::GatheringState::Complete) {
				auto description = this->pc_->localDescription();
				json message = {{"type", description->typeString()},
				                {"sdp", std::string(description.value())}};
				std::cout << message << std::endl;
			}
  });

}

void AC::Connector::AwaitSignalling()
{
}

void AC::Connector::OnBridgeInformation(json message)
{
  if (message.find("type") != message.end())
  {
    /*!
     * Message Type: Answer
     *
     * This message type contains the sdp information that UE sends
     * It should have been stripped of anything that is related to the actual
     * device that UE runs on and only contain video/audio/data transmission information
     * that is relevant for our webrtc startup
     */
    if (message["type"] == "answer")
    {
      std::string sdp = message["sdp"];
      rtc::Description desc(sdp);
      BridgePointer->ConfigureUpstream(this, message);
      
    }
  }
}

std::string AC::Connector::PushSDP(std::string)
{
  return std::string();
}


AC::Connector::Connector()
{

}

AC::Connector::~Connector()
{
}

void AC::Connector::StartSignalling(std::string IP, int Port, bool keepAlive, bool useAuthentification)
{
  SignallingConnection = std::make_shared<rtc::WebSocket>();
  std::promise<void> RunGuard;
  auto Notifier = RunGuard.get_future();
  SignallingConnection->onOpen([this, &RunGuard]()
  {
    
  });
  SignallingConnection->onClosed([this, &RunGuard](){});
  SignallingConnection->onError([this, &RunGuard](auto error){});
  SignallingConnection->onMessage([
    this,
    &RunGuard,
    TentativeConnection = std::weak_ptr<rtc::WebSocket>(SignallingConnection)
  ](auto message)
  {
    // without the rtc library types the compiler will get confused as to what this is
    // they types are essentially std::bytes and std::string.
    if(std::holds_alternative<rtc::string>(message))
    {
      json content = json::parse(std::get<rtc::string>(message));
      if(content["type"] == "offer")
      {
        std::cout << "I received an offer and this is the most crucial step in bridge setup!" << std::endl;
        std::string sdp = content["sdp"];

        // we MUST fail if this is not resolved as the sdp description
        // has to be SYNCHRONOUSLY valid on both ends of the bridge!
        BridgePointer->CreateTask(std::bind(&Seeker::BridgeSynchronize, BridgePointer, this, sdp, true));
        auto desc = pc_->localDescription();
        
      }
    }
  });
  std::cout << "Waiting for Signalling Websocket to Connect." << std::endl;
  Notifier.wait();
}

void AC::Connector::StartFrameReception()
{
}
