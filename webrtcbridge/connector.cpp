#include "connector.hpp"

#include "rtc/peerconnection.hpp"
#include "rtc/rtcpsrreporter.hpp"
#include "rtc/rtp.hpp"

#ifdef _WIN32
#elif __linux__
#endif



std::string WebRTCBridge::Connector::GetConnectionString()
{
  return std::string();
}

std::string WebRTCBridge::Connector::GenerateSDP()
{
  return std::string();
}

void WebRTCBridge::Connector::SetupApplicationConnection()
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
  pc_->onTrack([this](auto track)
  {
  });
  pc_->onDataChannel([this](auto channel)
  {
    DataFromApplication = channel;
    // this is a submit function that is expedient if we are
    // trying to avoid having output sockets on the receiving end
    // and then collect commands through the bridge instead
    // of having another socket here. It might actually be quite nice
    // TODO review or open socket here
    DataFromApplication->onMessage([this](auto message){
      Bridge->BridgeSubmit(this,message);
    });
  });

  // at this point we need the answer from the bridge, potentially, before we set anything up!
  // TODO review the connection graph at this point

  rtc::Description::Video V2A("video",rtc::Description::Direction::SendOnly);
  V2A.addH264Codec(96);
  V2A.addSSRC(ApplicationTrack::SSRC,"video-send");
  VideoToApplication = std::make_shared<ApplicationTrack>(pc_->addTrack(V2A));

  rtc::Description::Audio A2A("audio", rtc::Description::Direction::SendOnly);
  
}

void WebRTCBridge::Connector::AwaitSignalling()
{
}

void WebRTCBridge::Connector::OnInformation(json message)
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
      Bridge->ConfigureUpstream(this, message);
      for(unsigned i = 0; i < desc.mediaCount(); ++i)
      {
        auto medium = desc.media(i);
        if(std::holds_alternative<rtc::Description::Media*>(medium))
        {
          auto metadata = std::get<rtc::Description::Media*>(medium);
          // Here we parse the information that is coming from the bridge
          // and we are also setting up our tracks in this manner.
          // we are doing a full init work in this fashion!
          // as this is in the bridge thread, we should probably do it kind of quick
          // However, the bridge waits here anyways.
        }
      }
    }
  }
}

std::string WebRTCBridge::Connector::PushSDP(std::string)
{
  return std::string();
}


WebRTCBridge::Connector::Connector()
{

}

WebRTCBridge::Connector::~Connector()
{
}


void WebRTCBridge::Connector::StartFrameReception()
{
}
