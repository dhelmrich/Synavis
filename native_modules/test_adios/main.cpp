#include <iostream>
#include <string>
#include <vector>
#include <span>
#include <thread>
#include <chrono>

#include "Synavis.hpp"
#include "adios/AdiosConnector.hpp"

static Synavis::Logger::LoggerInstance ltest = Synavis::Logger::Get()->LogStarter("AdiosTest");


int main()
{
  ltest(Synavis::ELogVerbosity::Info) << "=== ADIOS2 Connection Test ===" << std::endl;
  
  auto connector = std::make_shared<Synavis::AdiosConnector>();
  
  ltest(Synavis::ELogVerbosity::Info) << "Initializing..." << std::endl;
  connector->Initialize();
  
  ltest(Synavis::ELogVerbosity::Info) << "Setting configuration..." << std::endl;
  connector->SetEngineType("SST");
  connector->SetIOName("AdiosIO");
  connector->SetVariableName("SynavisData");
  connector->SetFilenamePrefix("synavis_output");
  connector->SetNetworkInterface("localhost");
  connector->SetPort(9001);
  
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
