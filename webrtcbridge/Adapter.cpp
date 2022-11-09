#include "Adapter.hpp"

void WebRTCBridge::Adapter::SetupWebRTC()
{
  pc_ = std::make_shared<rtc::PeerConnection>();
  
  pc_->onStateChange([this](rtc::PeerConnection::State inState)
  {
    // state change should trigger failure if not accomplished connection
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
    // TODO i need the ID either during SDP sync or at this moment
    track->onMessage([this,track](auto messageorpackage)
    {
      Bridge->BridgeSubmit(this, track, messageorpackage);
      if (std::holds_alternative<std::string>(messageorpackage))
      {
        json message = { {"id",this->ID},{"message",std::get<std::string>(messageorpackage)} };
        this->OnChannelMessage(message);
      }
      else
      {
        auto package = std::get<rtc::binary>(messageorpackage);
      }
    });
    OnTrack(track);
  }); 
  pc_->onDataChannel([this](std::shared_ptr<rtc::DataChannel> channel)
  {
    json desc = {{"label",channel->label()},
    { "id",channel->id()},
    {"protocol",channel->protocol()},
    {"maxmessagesize",channel->maxMessageSize()}};

    Bridge->CreateTask(std::bind(&Bridge::BridgeSynchronize, Bridge.get(), this, desc, false));

    channel->onMessage([this](auto messageorpackage)
    {
      if(std::holds_alternative<std::string>(messageorpackage))
      {
        OnChannelMessage(std::get<std::string>(messageorpackage));
      }
      else
      {
        OnChannelPackage(std::get<rtc::binary>(messageorpackage));
      }
    });
    OnDataChannel(channel);
  });
  pc_->onLocalCandidate([this](auto candidate)
    {

    });
}

void WebRTCBridge::Adapter::CheckBridgeExtention(const std::string& SDP)
{
  
}

std::string WebRTCBridge::Adapter::GenerateSDP()
{
  auto sdp = pc_->localDescription();
  std::string description{};
  if(sdp.has_value())
  {
    description = sdp->generateSdp("\n");
  }
  return description;
}

std::string WebRTCBridge::Adapter::Offer()
{
  return generated_offer_.dump();
}

std::string WebRTCBridge::Adapter::Answer()
{
  return generated_answer_.dump();
}

void WebRTCBridge::Adapter::OnRemoteInformation(json message)
{
 
}


std::string WebRTCBridge::Adapter::PushSDP(std::string SDP)
{
  pc_->setRemoteDescription(SDP);
  if(pc_->localDescription().has_value())
  {
    return *pc_->localDescription();
  }
  else
    return "";
}

rtc::PeerConnection* WebRTCBridge::Adapter::GetPeerConnection()
{
    return pc_.get();
}

void WebRTCBridge::Adapter::SetID(WebRTCBridge::Bridge* Instigator, uint32_t ID)
{
  if(Bridge && Bridge->EstablishedConnection(true))
  {
    this->ID = ID;
  }
}
