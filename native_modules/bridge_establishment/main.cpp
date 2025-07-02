#include <rtc/rtc.hpp>
#include <json.hpp>

#include <span>
#include <string>
#include <iostream>
#include <thread>

#include "Provider.hpp"
#include "Seeker.hpp"

using namespace std::chrono_literals;
using json = nlohmann::json;

static auto lmain = Synavis::Logger::Get()->LogStarter("Main");
static auto timeout_policy = Synavis::EMessageTimeoutPolicy::Critical;
static auto verbosity = Synavis::ELogVerbosity::Verbose;
static auto timeout_delay = 10s;

/**
 * PORT SETUP FOR THE BRIDGE TO TEST ON A WORKSTATION
 * UE Ports: Streamer 5000, Signalling 8080
 * Provider Ports: In: 25552, Out 25553, Data: 25554
 * Seeker Ports: In: 25553, Out 25552, Data: 25554
 * User Ports: Relay: 25555, Signalling: 8080
 */
static json GeneralConfig = {
  {"SignallingIP", "localhost"},
  {"SignallingPort", 8080},
  {"SeekerPort", 25552},
  {"ProviderPort", 25553},
  {"DataPort", 25554},
  {"StreamerPort", 5000},
  {"RelayPort", 25555},
  {"RelayIP", "localhost"},
  {"StreamerIP", "localhost"}
  };

  static auto lseeker = Synavis::Logger::Get()->LogStarter("Seeker");
  static auto lprovider = Synavis::Logger::Get()->LogStarter("Provider");


void ProviderMain(const json& Config)
{
  auto BridgeProvider = std::make_shared<Synavis::Provider>();
  lprovider(Synavis::ELogVerbosity::Info) << "Provider Thread started" << std::endl;
  BridgeProvider->UseConfig(Config);
  BridgeProvider->SetTimeoutPolicy(timeout_policy, timeout_delay);

  // Retry loop for connection
  while (true) {
    lprovider(Synavis::ELogVerbosity::Debug) << "Calling InitConnection()" << std::endl;
    BridgeProvider->InitConnection();
    lprovider(Synavis::ELogVerbosity::Debug) << "InitConnection() returned, checking EstablishedConnection(false)" << std::endl;
    if (BridgeProvider->EstablishedConnection(false)) {
      lprovider(Synavis::ELogVerbosity::Info) << "Could establish bridge connection" << std::endl;
      break;
    } else {
      lprovider(Synavis::ELogVerbosity::Warning) << "Could not establish bridge connection, retrying in 1s..." << std::endl;
      std::this_thread::sleep_for(1000ms);
    }
  }
  lprovider(Synavis::ELogVerbosity::Info) << "Provider Thread ended" << std::endl;
  BridgeProvider->Stop(); // Ensure all internal threads are stopped
}

void SeekerMain(const json& Config)
{
  auto BridgeSeeker = std::make_shared<Synavis::Seeker>();
  lseeker(Synavis::ELogVerbosity::Info) << "Seeker Thread started" << std::endl;
  BridgeSeeker->UseConfig(Config);
  BridgeSeeker->SetTimeoutPolicy(timeout_policy, timeout_delay);

  // Retry loop for connection
  while (true) {
    lseeker(Synavis::ELogVerbosity::Debug) << "Calling InitConnection()" << std::endl;
    BridgeSeeker->InitConnection();
    lseeker(Synavis::ELogVerbosity::Debug) << "InitConnection() returned, checking EstablishedConnection(false)" << std::endl;
    if (BridgeSeeker->EstablishedConnection(false)) {
      lseeker(Synavis::ELogVerbosity::Info) << "Could establish bridge connection" << std::endl;
      break;
    } else {
      lseeker(Synavis::ELogVerbosity::Warning) << "Could not establish bridge connection, retrying in 1s..." << std::endl;
      std::this_thread::sleep_for(1000ms);
    }
  }
  lseeker(Synavis::ELogVerbosity::Info) << "Seeker Thread ended" << std::endl;
  BridgeSeeker->Stop(); // Ensure all internal threads are stopped
}

int main(int args, char** argv)
{
  json ProviderConfig = GeneralConfig;
  json SeekerConfig   = GeneralConfig;

  Synavis::Logger::Get()->SetVerbosity(verbosity);

  lmain(Synavis::ELogVerbosity::Info) << "Reading command line arguments..." << std::endl;

  // CommandLineParser
  auto CommandLineParser = Synavis::CommandLineParser(args, argv);
  // option --first to determine which thread to start first
  auto first = 0;
  if (CommandLineParser.HasArgument("first"))
  {
    lmain(Synavis::ELogVerbosity::Info) << "Parsing --first argument..." << std::endl;
    auto param_first = CommandLineParser.GetArgument("first");
    if (param_first == "provider")
      first = 1; // Start Provider first
    else if (param_first == "seeker")
      first = 2; // Start Seeker first
    else
      lmain(Synavis::ELogVerbosity::Warning) << "Unknown argument for --first: " << param_first << std::endl;
  }

  if (CommandLineParser.HasArgument("verbosity"))
  {
    lmain(Synavis::ELogVerbosity::Info) << "Parsing --verbosity argument..." << std::endl;
    auto param_verbosity = CommandLineParser.GetArgument("verbosity");
    if (param_verbosity == "debug")
      verbosity = Synavis::ELogVerbosity::Debug;
    else if (param_verbosity == "info")
      verbosity = Synavis::ELogVerbosity::Info;
    else if (param_verbosity == "warning")
      verbosity = Synavis::ELogVerbosity::Warning;
    else if (param_verbosity == "error")
      verbosity = Synavis::ELogVerbosity::Error;
    else
      lmain(Synavis::ELogVerbosity::Warning) << "Unknown argument for --verbosity: " << param_verbosity << std::endl;
  }


  ProviderConfig["LocalPort"] = ProviderConfig["ProviderPort"];
  ProviderConfig["RemotePort"] = ProviderConfig["SeekerPort"];
  ProviderConfig["LocalAddress"] = "127.0.0.1";
  ProviderConfig["RemoteAddress"] = "127.0.0.1";

  SeekerConfig["LocalPort"] = SeekerConfig["SeekerPort"];
  SeekerConfig["RemotePort"] = SeekerConfig["ProviderPort"];
  SeekerConfig["LocalAddress"] = "127.0.0.1";
  SeekerConfig["RemoteAddress"] = "127.0.0.1";

  lmain(Synavis::ELogVerbosity::Info) << "Starting threads based on --first argument: " << first << std::endl;

  // Start threads based on the --first argument
  std::future<void> ProviderThread;
  std::future<void> SeekerThread;

  if (first == 1) {
    lmain(Synavis::ELogVerbosity::Info) << "Starting Provider first, then Seeker" << std::endl;
    // Start Provider first, then Seeker
    ProviderThread = std::async(std::launch::async, ProviderMain, ProviderConfig);
    // Small delay to ensure Provider starts first
    std::this_thread::sleep_for(1000ms);
    SeekerThread = std::async(std::launch::async, SeekerMain, SeekerConfig);
  } else if (first == 2) {
    lmain(Synavis::ELogVerbosity::Info) << "Starting Seeker first, then Provider" << std::endl;
    // Start Seeker first, then Provider
    SeekerThread = std::async(std::launch::async, SeekerMain, SeekerConfig);
    std::this_thread::sleep_for(1000ms);
    ProviderThread = std::async(std::launch::async, ProviderMain, ProviderConfig);
  } else {
    // Start both threads immediately, order does not matter
    lmain(Synavis::ELogVerbosity::Info) << "Starting both Provider and Seeker threads" << std::endl;
    ProviderThread = std::async(std::launch::async, ProviderMain, ProviderConfig);
    SeekerThread   = std::async(std::launch::async, SeekerMain, SeekerConfig);
  }

  while (true)
  {
    std::this_thread::sleep_for(1000ms);
    auto status_provider = ProviderThread.wait_for(1ms);
    auto status_seeker   = SeekerThread.wait_for(1ms);
    if (status_provider == std::future_status::ready && status_seeker == std::future_status::ready)
      break;
    lmain(Synavis::ELogVerbosity::Info) << "Main Thread: Still running" << std::endl;
  }
  // Ensure all threads are joined before exiting
  ProviderThread.get();
  SeekerThread.get();
  lmain(Synavis::ELogVerbosity::Info) << "Main Thread: Exiting cleanly" << std::endl;
}
