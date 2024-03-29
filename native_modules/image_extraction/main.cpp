#include "DataConnector.hpp"
#include <json.hpp>
#include <iostream>
#include <chrono>
#include <rtc/common.hpp>

#include "MediaReceiver.hpp"

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


void ReportJSON(const json& j)
{
  std::cout << "Received json: ";
  if (j.contains("type"))
  {
    std::cout << j["type"].get<std::string>();
  }
  if (j.contains("chunk"))
  {
    std::cout << " chunk: " << j["chunk"].get<std::string>();
  }
  std::cout << std::endl;
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
      if (arg == "-i" || arg == "--ip")
      {
        if (args < a + 1)
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


  bool bWantData = false;
  json Data;
  std::vector<std::string> Messages;
  std::vector<int> Chunks;
  bool bInitialized = false;

  json Config = { {"SignallingIP","localhost"}, {"SignallingPort", 8080} };
  std::cout << "Sanity check: " << Config.dump() << std::endl;
  std::cout << "Fetching IP, " << Config["SignallingIP"].get<std::string>() << std::endl;
  dc->SetConfig(Config);
  dc->WriteSDPsToFile("C:/Work/gstreamer/target.sdp");
  dc->StartSignalling();
  dc->SetMessageCallback([&bWantData, &Messages, &bInitialized, &Chunks, dc](auto message)
    {
      Messages.push_back(message);
      json j = json::parse(message);
      ReportJSON(j);
      //std::cout << "Received message: " << message << std::endl;
      if (j.contains("chunk"))
      {
        // chunk scheme is number/total
        auto chunk = j["chunk"].get<std::string>();
        auto pos = chunk.find('/');
        if (pos != std::string::npos)
        {
          auto number = std::stoi(chunk.substr(0, pos));
          auto total = std::stoi(chunk.substr(pos + 1));
          if (!bInitialized)
          {
            // fill chunks with numbers from 0 to total
            Chunks.resize(total);
            std::generate(Chunks.begin(), Chunks.end(), [n = 0]() mutable { return n++; });
            bInitialized = true;
          }
          else
          {
            // remove the number from the list
            Chunks.erase(std::remove(Chunks.begin(), Chunks.end(), number), Chunks.end());
          }
          if (Chunks.empty())
          {
            std::cout << "Received all chunks" << std::endl;
            bWantData = true;
          }
          else if (number == total - 1)
          {
            // chunk is missing, requesting scheme is { "type": "receive", "progress": -2, chunk: # }
            std::cout << "Requesting missing chunk: " << Chunks[0] << std::endl;
            json j = { {"type", "receive"}, {"progress", -2}, {"chunk", Chunks[0]} };
            bWantData = false;
            dc->SendJSON(j);
          }
        }
      }
    });
  dc->SetDataCallback([&bWantData, &Data](auto data)
    {
      const char* dataPtr = reinterpret_cast<const char*>(data.data());
      std::string_view dataView(dataPtr, data.size());
      auto lower = std::find(dataView.begin(), dataView.end(), '{');
      auto upper = std::find(dataView.rbegin(), dataView.rend(), '}');
      if (upper == dataView.rend() || lower == dataView.end())
      {
        return;
      }
      dataView = std::string_view(&*lower, std::distance(lower, upper.base()));
      // double check if this might still be a json object
      try
      {
        json j = json::parse(dataView);
        ReportJSON(j);
      }
      catch (...)
      {
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
  dc->SendJSON(json({ {"type", "settings"},{"bRespondWithTiming", true}, {"bLogResponses", true} }));
  dc->SendJSON(json({ {"type","console"}, {"command", "t.maxFPS 10"} }));
  dc->SendJSON(json({ {"type","command"},{"name","cam"}, {"camera", "scene"} }));
  //dc->SendJSON(json({ {"type", "receive"}, {"progress", -1} }));

  //dc->SendString("test");

  //for (int i = 10; i < 11; ++i)
  //{
  //  std::vector<double> TestGeometry(3000 * i);
  //  // fill with increasing numbers
  //  std::generate(TestGeometry.begin(), TestGeometry.end(), [n = 0]() mutable { return n++; });
  //  auto encoded = Synavis::Encode64(TestGeometry);
  //  std::cout << "Encoded: " << encoded.substr(0,40) << std::endl;
  //  dc->SendFloat64Buffer(TestGeometry, "points", "base64");
  //  
  //  while (Messages.size() == 0)
  //  {
  //    std::this_thread::sleep_for(10ms);
  //  }
  //  // remove last message
  //  auto message = Messages.back();
  //  if(json::parse(message)["type"] == "error")
  //  {
  //    std::cout << "Error received: " << message << std::endl;
  //    return EXIT_FAILURE;
  //  }
  //  Messages.clear();
  //  delete [] encoded.data();
  //  std::this_thread::sleep_for(1s);
  //}

  while (Synavis::EConnectionState::CONNECTED == dc->GetState())
  {
    std::this_thread::sleep_for(10ms);
  }
  return EXIT_SUCCESS;
}
