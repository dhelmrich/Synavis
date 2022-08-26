#include "connector.hpp"

#include "rtc/peerconnection.hpp"
#include "rtc/rtcpsrreporter.hpp"
#include "rtc/rtp.hpp"

#ifdef _WIN32
#elif __linux__
#endif



void WebRTCBridge::Connector::SetupApplicationConnection()
{

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


void WebRTCBridge::Connector::OnGatheringStateChange(rtc::PeerConnection::GatheringState inState)
{
}

void WebRTCBridge::Connector::OnTrack(std::shared_ptr<rtc::Track> inTrack)
{
}

void WebRTCBridge::Connector::OnLocalDescription(rtc::Description inDescription)
{
}

void WebRTCBridge::Connector::OnLocalCandidate(rtc::Candidate inCandidate)
{
}

void WebRTCBridge::Connector::OnDataChannel(std::shared_ptr<rtc::DataChannel> inChannel)
{
  Adapter::OnDataChannel(inChannel);
}

WebRTCBridge::Connector::Connector() : Adapter()
{

}

void WebRTCBridge::Connector::OnPackage(rtc::binary inPackage)
{
}

void WebRTCBridge::Connector::OnChannelMessage(std::string inMessage)
{
}

WebRTCBridge::Connector::~Connector()
{
}


void WebRTCBridge::Connector::StartFrameReception()
{
}
