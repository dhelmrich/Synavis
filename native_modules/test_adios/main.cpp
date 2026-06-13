#include <iostream>
#include <string>
#include <vector>
#include <span>
#include <thread>
#include <chrono>

#include "Synavis.hpp"
#include "adios/AdiosConnector.hpp"

static Synavis::Logger::LoggerInstance ltest = Synavis::Logger::Get()->LogStarter("AdiosTest");

int main(int argc, char** argv)
{
  Synavis::Logger::Get()->SetVerbosity(Synavis::ELogVerbosity::Verbose);
  Synavis::Logger::Get()->SetupLogfileRotate("adios_test.log");
  ltest(Synavis::ELogVerbosity::Info) << "=== ADIOS2 Connection Test ===" << std::endl;
  
  Synavis::CommandLineParser parser(argc, argv);
  bool useSST = parser.HasArgument("sst");
  
  auto connector = std::make_shared<Synavis::AdiosConnector>();
  
  ltest(Synavis::ELogVerbosity::Info) << "Initializing..." << std::endl;
  connector->Initialize();
  
  ltest(Synavis::ELogVerbosity::Info) << "Setting configuration..." << std::endl;
  if (useSST)
  {
    connector->SetTransportType(Synavis::EAdiosTransport::SST);
  }
  else
  {
    connector->SetTransportType(Synavis::EAdiosTransport::BP5);  // Default to BP5 for real-time streaming
  }
  connector->SetIOName("AdiosIO");
  connector->SetVariableName("SynavisData");
  connector->SetFilenamePrefix("synavis_output");
  connector->SetNetworkInterface("localhost");
  connector->SetPort(9001);
  
  ltest(Synavis::ELogVerbosity::Info) << "Applying configuration..." << std::endl;
  connector->ApplyConfiguration();
  
  ltest(Synavis::ELogVerbosity::Info) << "Adding variable..." << std::endl;
  connector->AddVariable("data", "uint8_t");
  
  ltest(Synavis::ELogVerbosity::Info) << "Starting streaming..." << std::endl;
  connector->StartStreaming();
  
  ltest(Synavis::ELogVerbosity::Info) << "Waiting for connection (2000ms)..." << std::endl;
  connector->LockUntilConnected(2000);
  
  ltest(Synavis::ELogVerbosity::Info) << "Starting reader..." << std::endl;
  connector->StartReader();
  
  ltest(Synavis::ELogVerbosity::Info) << "Test passed - no crash during connection setup" << std::endl;
  
  // Wait a bit to see if crash happens later
  std::this_thread::sleep_for(std::chrono::seconds(2));
  
  ltest(Synavis::ELogVerbosity::Info) << "Stopping..." << std::endl;
  connector->StopReader();
  connector->StopStreaming();
  
  ltest(Synavis::ELogVerbosity::Info) << "Done." << std::endl;
  
  return 0;
}
