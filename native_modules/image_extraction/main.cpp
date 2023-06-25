#include "DataConnector.hpp"
#include <json.hpp>
#include <iostream>
#include <chrono>
#include <rtc/common.hpp>

#include "MediaReceiver.hpp"


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

int main(int args, char** argv)
{
  auto dc = std::make_shared<Synavis::MediaReceiver>();
  // if we have arguments, we check if verbose logging is requested
  if (args > 1)
  {
    for (int a = 1; a < args; ++a)
    {
      std::string arg = argv[a];
      if (arg == "-v" || arg == "--verbose")
      {
        std::cout << "Verbose logging enabled" << std::endl;
         rtcInitLogger(RTC_LOG_VERBOSE, nullptr);
      }
      if(arg == "-i" || arg == "--ip")
      {
        if(args < a + 1)
        {
          std::cout << "No IP address provided" << std::endl;
          return -1;
        }
        std::cout << "Setting IP to " << argv[a + 1] << std::endl;
        dc->IP = argv[a + 1];
      }
    }
  }
  std::this_thread::sleep_for(std::chrono::seconds(2));
  using namespace std::chrono_literals;
  dc->Initialize();
  dc->ConfigureRelay("127.0.0.1", 53326);
  dc->SetTakeFirstStep(false);
  dc->SetLogVerbosity(Synavis::ELogVerbosity::Debug);
  using json = nlohmann::json;
  

  bool bWantData = false;
  json Data;
  std::vector<std::string> Messages;


  json Config = { {"SignallingIP","localhost"}, {"SignallingPort", 8080} };
  std::cout << "Sanity check: " << Config.dump() << std::endl;
  std::cout << "Fetching IP, " << Config["SignallingIP"].get<std::string>() << std::endl;
  dc->SetConfig(Config);
  dc->WriteSDPsToFile("C:/Work/gstreamer/target.sdp");
  dc->StartSignalling();
  dc->SetMessageCallback([&bWantData, &Messages](auto message)
    {
      Messages.push_back(message);
      std::cout << "Received message: " << message << std::endl;
    });
  dc->SetDataCallback([&bWantData, &Data](auto data)
    {
      const char* dataPtr = reinterpret_cast<const char*>(data.data());
      std::string_view dataView(dataPtr, data.size());
      // double check if this might still be a json object
      try
      {
        json j = json::parse(dataView);
        std::cout << "Received json." << std::endl;
      }
      catch (...)
      {
        std::cout << "Received data: " << dataView << std::endl;
      }
    });
  dc->SetFrameReceptionCallback([](auto frame)
  {
    //std::cout << "Received frame: " << frame.size() << std::endl;
  });
  while (dc->GetState() != Synavis::EConnectionState::CONNECTED)
  {
    std::this_thread::yield();
  }
  std::cout << "Found out that we are connected" << std::endl;
  dc->PrintCommunicationData();
  dc->SendJSON(json({{"type", "settings"},{"bRespondWithTiming", true}}));
  dc->SendJSON(json({{"type","console"}, {"command", "t.maxFPS 10"}}));
  dc->SendJSON(json({{"type","command"},{"name","cam"}, {"camera", "scene"}}));
  dc->SendJSON(json({ {"type","command"}, {"name" , "RawData"}, {"framecapturetime" , 2.0} }));
  dc->SendJSON(json({{"type", "query"}}));
  
  //dc->SendString("test");

  while(Synavis::EConnectionState::CONNECTED == dc->GetState())
  {
    std::this_thread::sleep_for(10ms);
  }

  // for (int i = 10; i < 11; ++i)
  // {
  //   std::vector<double> TestGeometry(3000 * i);
  //   // fill with increasing numbers
  //   std::generate(TestGeometry.begin(), TestGeometry.end(), [n = 0]() mutable { return n++; });
  //   auto encoded = Synavis::Encode64(TestGeometry);
  //   std::cout << "Encoded: " << encoded << std::endl;
  //   dc->SendFloat64Buffer(TestGeometry, "points", "base64");
  //   
  //   while (Messages.size() == 0)
  //   {
  //     std::this_thread::sleep_for(10ms);
  //   }
  //   // remove last message
  //   auto message = Messages.back();
  //   if(json::parse(message)["type"] == "error")
  //   {
  //     std::cout << "Error received: " << message << std::endl;
  //     return EXIT_FAILURE;
  //   }
  //   Messages.clear();
  //   delete [] encoded.data();
  //   std::this_thread::sleep_for(1s);
  // }
  return EXIT_SUCCESS;
}
