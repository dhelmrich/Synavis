#include "Connector.hpp"

#include "rtc/peerconnection.hpp"
#include "rtc/rtcpsrreporter.hpp"
#include "rtc/rtp.hpp"

#ifdef _WIN32
#elif __linux__
#endif



void Synavis::Connector::SetupApplicationConnection()
{

  // at this point we need the answer from the bridge, potentially, before we set anything up!
  // TODO review the connection graph at this point

  rtc::Description::Video V2A("video",rtc::Description::Direction::SendOnly);
  V2A.addH264Codec(96);
  V2A.addSSRC(42,"video-send");
  this->ToApplication.push_back(pc_->addTrack(V2A));

  rtc::Description::Audio A2A("audio", rtc::Description::Direction::SendOnly);
}

void Synavis::Connector::AwaitSignalling()
{
  // todo: wait for signalling to boot up, use future from onOpen
  //       or a flag of the class set by onopen
}

void Synavis::Connector::OnRemoteInformation(json message)
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
    if (message["type"] == "answer" || message["type"] == "offer")
    {
      std::string sdp = message["sdp"];
      rtc::Description desc(sdp);
      // todo: make sure that extmaps contain a compound endpoint+track id
      auto extensions = message["extensions"];
      for(auto entry : extensions)
      {
        
      }
      for (unsigned i = 0; i < desc.mediaCount(); ++i)
      {
        auto medium = desc.media(i);
        if (std::holds_alternative<rtc::Description::Media*>(medium))
        {
          /*
          // create RTP configuration
          auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, cname, payloadType, H264RtpPacketizer::defaultClockRate);
          // create packetizer
          auto packetizer = make_shared<H264RtpPacketizer>(H264RtpPacketizer::Separator::Length, rtpConfig);
          // create H264 handler
          auto h264Handler = make_shared<H264PacketizationHandler>(packetizer);
          // add RTCP SR handler
          auto srReporter = make_shared<RtcpSrReporter>(rtpConfig);
          h264Handler->addToChain(srReporter);
          // add RTCP NACK handler
          auto nackResponder = make_shared<RtcpNackResponder>();
          h264Handler->addToChain(nackResponder);
          // set handler
          track->setMediaHandler(h264Handler);
          track->onOpen(onOpen);
          auto trackData = make_shared<ClientTrackData>(track, srReporter);
           */
          auto* metadata = std::get<rtc::Description::Media*>(medium);
          // Here we parse the information that is coming from the bridge
          // and we are also setting up our tracks in this manner.
          // we are doing a full init work in this fashion!
          // as this is in the bridge thread, we should probably do it kind of quick
          // However, the bridge waits here anyways.
          auto track = pc_->addTrack(*metadata);

          // all of these tracks are TO APPLICATION and can be initialized as output
          OwningBridge->ConfigureTrackOutput(track, metadata);
          ToApplication.push_back(std::move(track));
        }
        else
        {
          auto app = std::get<rtc::Description::Application*>(medium);
          auto channel = pc_->createDataChannel(app->description());
          channel->onOpen([this]
          {
            
          });
          channel->onMessage([](auto dataormessage)
          {
            
          });
        }
      }
    }
  }
}

void Synavis::Connector::SetReceptionPolicy(EDataReceptionPolicy inPolicy)
{
  this->Policy = inPolicy;
}


void Synavis::Connector::OnGatheringStateChange(rtc::PeerConnection::GatheringState inState)
{
  if(inState == rtc::PeerConnection::GatheringState::Complete)
  {
    // todo: be ready to receive at this point, but events will triggered by the framework anyway
  }
}

void Synavis::Connector::OnTrack(std::shared_ptr<rtc::Track> inTrack)
{
  FromApplication.push_back(std::move(inTrack));
}

void Synavis::Connector::OnLocalDescription(rtc::Description inDescription)
{
}

void Synavis::Connector::OnLocalCandidate(rtc::Candidate inCandidate)
{
}

void Synavis::Connector::OnDataChannel(std::shared_ptr<rtc::DataChannel> inChannel)
{
  // Data Channels in webRTC are generally both directions. This is important
  // since it might imply that a client is either able to consume data or not
  // and subsequently also has an inclination of producing commands
}

Synavis::Connector::Connector() : Adapter()
{

}

void Synavis::Connector::OnChannelPackage(rtc::binary inPackage)
{
}

void Synavis::Connector::OnChannelMessage(std::string inMessage)
{
  // todo: channel message contains string content, needs to be observed, might contain related data
}

std::string Synavis::Connector::GetConnectionString()
{
  return this->config_.dump();
}

Synavis::Connector::~Connector()
{
}

Synavis::Connector::Connector(Connector&& other)
{
}

void Synavis::Connector::StartFrameReception()
{
}
