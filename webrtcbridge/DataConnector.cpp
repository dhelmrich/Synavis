#include "DataConnector.hpp"

WebRTCBridge::DataConnector::DataConnector()
{
  DataChannel = pc_->createDataChannel("DataConnectionChannel");
}
