
#include <rtc/rtc.hpp>
#include <json.hpp>

#include <span>
#include <string>
#include <iostream>

#include "MediaReceiver.hpp"
#include "FrameDecodeAV.hpp"

using json = nlohmann::json;
inline constexpr std::byte operator "" _b(unsigned long long i) noexcept
{
  return static_cast<std::byte>(i);
}

inline std::byte operator+(std::byte b, std::byte i) noexcept
{
  return static_cast<std::byte>(static_cast<int>(b) + static_cast<int>(i));
}

inline std::byte operator++(std::byte& b) noexcept
{
  return b = b + 1_b;
}

static Synavis::Logger::LoggerInstance lmain = Synavis::Logger::Get()->LogStarter("main");


int main(int args, char** argv)
{
  using namespace std::chrono_literals;
  auto dc = std::make_shared<Synavis::MediaReceiver>();
  //rtcInitLogger(RTC_LOG_VERBOSE,nullptr);
  // if we have arguments, we check if verbose logging is requested
  Synavis::ELogVerbosity LogVerbosity = Synavis::ELogVerbosity::Verbose;
  Synavis::Logger::Get()->SetVerbosity(LogVerbosity);
  Synavis::Logger::Get()->SetupLogfileRotate("MediaReceiver.log");
  Synavis::ECodec codec = Synavis::ECodec::VP9;
  Synavis::RegisterAvLogCallback(true);
  bool logsdp = false;
  json Config = { {"SignallingIP","127.0.0.1"}, {"SignallingPort", 8080} };

  lmain(Synavis::ELogVerbosity::Info) << "Starting MediaReceiver example with config: " << Config.dump() << std::endl;
  // set the config for the MediaReceiver based on Config
  dc->SetConfig(Config);

  lmain(Synavis::ELogVerbosity::Info) << "Creating MediaReceiver with codec VP9" << std::endl;
  dc->SetCodec(codec);
  dc->SetTakeFirstStep(false);
  //dc->ConfigureRelay("127.0.0.1", 5535);

  lmain(Synavis::ELogVerbosity::Info) << "Setting up frame reception callback and FrameDecode instance" << std::endl;
  std::vector<int> FrameSizes;
  std::shared_ptr<Synavis::FrameDecode> vpx;

  vpx = std::make_shared<Synavis::FrameDecode>(codec, nullptr);
  dc->SetFrameReceptionCallback(vpx->CreateAcceptor([&FrameSizes](Synavis::FrameContent frame)
  {
    FrameSizes.push_back(static_cast<int>(frame.Data.size()));
  }));
  vpx->SetFrameCallback([](Synavis::FrameContent frame)
  {
    lmain(Synavis::ELogVerbosity::Debug) << "Got frame (" << frame.Width << "/" << frame.Height << ")" << std::endl;
  });

  dc->SetMessageCallback([](auto message)
  {
    lmain(Synavis::ELogVerbosity::Debug) << "Got message: " << message << std::endl;
  });

  // lifecycle callbacks and retry/lock behavior (helpful when debugging/connectivity)
  dc->SetOnTrackCloseCallback([]()
  {
    lmain(Synavis::ELogVerbosity::Debug) << "Track closed" << std::endl;
  });
  dc->SetOnClosedCallback([]()
  {
    lmain(Synavis::ELogVerbosity::Debug) << "Data channel closed" << std::endl;
  });

  if(logsdp)
  {
    dc->SetOnTrackOpenCallback([&Config,dc]()
    {
      dc->WriteSDPsToFile(Config["SDPFile"]);
    });
  }
  lmain(Synavis::ELogVerbosity::Debug)
  << "----------------------------------------- Connecting ----------------------------------------------------" << std::endl;

  dc->Initialize();
  dc->StartSignalling();

  std::this_thread::sleep_for(1s);

  dc->LockUntilConnected(2000);

  while (dc->GetState() != Synavis::EConnectionState::CONNECTED)
  {
    std::this_thread::yield();
  }
  lmain(Synavis::ELogVerbosity::Debug)
  << "----------------------------------------- Connected ------------------------------------------------------" << std::endl;

  // Parse remote media descriptions so the FrameDecode instance knows codec/format
  // information before frames arrive. This mirrors extraction.py's ParseDescription
  // handling and helps the decoder accept frames correctly.
  int cnt = dc->NumRemoteMedia();
  lmain(Synavis::ELogVerbosity::Debug) << "NumRemoteMedia=" << cnt << std::endl;
  for (int i = 0; i < cnt; ++i)
  {
    auto desc = dc->RemoteMediaDescription(i);
    lmain(Synavis::ELogVerbosity::Debug) << "RemoteMediaDescription[" << i << "]=" << desc.dump() << std::endl;
    vpx->ParseDescription(desc);
  }
  lmain(Synavis::ELogVerbosity::Debug) << "Called FrameDecode.ParseDescription for all remote media" << std::endl;

  std::this_thread::sleep_for(std::chrono::seconds(2));

  dc->SendJSON(json({ {"type","command"},{"name","cam"}, {"camera", "scene"} }));
  dc->SendJSON(json({ {"type","command"},{"name","start"} }));
  dc->StartStreaming();

  while (Synavis::EConnectionState::CONNECTED == dc->GetState())
  {
    std::this_thread::sleep_for(500ms);
    //dc->SendMouseClick();
    //dc->RequestKeyFrame();
  }
  return EXIT_SUCCESS;
}
