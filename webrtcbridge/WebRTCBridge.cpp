#include "WebRTCBridge.hpp"

#include <fstream>

int WebRTCBridge::BridgeSocket::Receive(bool invalidIsFailure)
{
#ifdef _WIN32
  return 0;
#elif __linux__
  return 0;
#endif
}

WebRTCBridge::ApplicationTrack::ApplicationTrack(std::shared_ptr<rtc::Track> inTrack)
      : Track(inTrack) {}
      
void WebRTCBridge::ApplicationTrack::Send(std::byte* Data, unsigned Length)
{
  auto rtp = reinterpret_cast<rtc::RtpHeader*>(Data);
  rtp->setSsrc(SSRC);
  Track->send(Data, Length);
}

void WebRTCBridge::ApplicationTrack::ConfigureOutput(std::shared_ptr<rtc::RtcpSrReporter> inReporter)
{
}

void WebRTCBridge::ApplicationTrack::ConfigureIn()
{
}

bool WebRTCBridge::ApplicationTrack::Open()
{
  return Track->isOpen();
}

WebRTCBridge::NoBufferThread::NoBufferThread(std::weak_ptr<ApplicationTrack> inDataDestination,
                                   std::weak_ptr<BridgeSocket> inDataSource)
    : DataDestination(inDataDestination), DataSource(inDataSource)
{
  Thread = std::make_unique<std::thread>(&WebRTCBridge::NoBufferThread::Run,this);
}

void WebRTCBridge::NoBufferThread::Run()
{
  // Consume buffer until close (this should never be empty but we never know)
  auto DataDestinationPtr = DataDestination.lock();
  auto DataSourcePtr = DataSource.lock();
  int Length;
  while((Length = DataSourcePtr->Receive()) > 0)
  {
    if (Length < sizeof(rtc::RtpHeader) || !DataDestinationPtr->Open())
      continue;
    // This is a roundabout reinterpret_cast without having to actually do one
    DataDestinationPtr->Send(DataSourcePtr->BinaryData.data(),Length);
  }
}

void WebRTCBridge::Bridge::UseConfig(std::string filename)
{
  std::ifstream file(filename);
  auto fileConfig = json::parse(file);
  bool complete = true;
  for(auto key : Config)
    if(fileConfig.find(key) == fileConfig.end())
      complete = false;
}
