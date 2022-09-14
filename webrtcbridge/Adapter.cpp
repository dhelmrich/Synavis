#include "Adapter.hpp"

void WebRTCBridge::Adapter::SetupWebRTC()
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
    
    json desc = {{"label",channel->label()},
    { "id",channel->id()},
    {"protocol",channel->protocol()},
    {"maxmessagesize",channel->maxMessageSize()}};
    Bridge->BridgeRun(std::bind(&Bridge::BridgeSynchronize, Bridge, this, desc, false));
    channel->onMessage([this](auto messageorpackage)
    {
      
      if(std::holds_alternative<std::string>(messageorpackage))
      {
        OnChannelMessage(std::get<std::string>(messageorpackage));
      }
      else
      {
        OnPackage(std::get<rtc::binary>(messageorpackage));
      }
    });
    OnDataChannel(channel);
  });
}

std::string WebRTCBridge::Adapter::GetConnectionString()
{
}

std::string WebRTCBridge::Adapter::GenerateSDP()
{
  
}

std::string WebRTCBridge::Adapter::Offer()
{
  return generated_offer_.dump();
}

std::string WebRTCBridge::Adapter::Answer()
{
  return generated_answer_.dump();
}

void WebRTCBridge::Adapter::OnInformation(json message)
{
  
}

std::string WebRTCBridge::Adapter::PushSDP(std::string)
{

}