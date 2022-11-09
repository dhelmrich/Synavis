#include "DataConnector.hpp"

WebRTCBridge::DataConnector::DataConnector()
{
  pc_ = std::make_shared<rtc::PeerConnection>(rtcconfig_);
  DataChannel = pc_->createDataChannel("DataConnectionChannel");
  DataChannel->onOpen([this]()
    {

    });
  DataChannel->onMessage([this](auto messageordata)
    {
      if (std::holds_alternative<rtc::binary>(messageordata))
      {
        auto data = std::get<rtc::binary>(messageordata);
      }
      else
      {
        auto message = std::get<std::string>(messageordata);
      }
    });
}

WebRTCBridge::DataConnector::~DataConnector()
{
  pc_->close();
  ss_.close();
}
