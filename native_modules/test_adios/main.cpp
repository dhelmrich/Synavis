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

  
  // Callback storage
  std::vector<std::string> received_messages;
  std::vector<size_t> received_data_sizes;
  std::vector<size_t> received_frame_sizes;
  
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
    connector->SetTransportType(Synavis::EAdiosTransport::BP4);  // Match adios2_coupling.py: BP4 for file-based coupling
  }
  connector->SetIOName("AdiosIO");
  connector->SetVariableName("SynavisData");
  connector->SetFilenamePrefix("synavis_output");
  
  // BP4 does not use network interface/port parameters, but set them for completeness
  connector->SetNetworkInterface("localhost");
  connector->SetPort(9001);
  
  ltest(Synavis::ELogVerbosity::Info) << "Applying configuration..." << std::endl;
  connector->ApplyConfiguration();

  connector->GetIOEngineWriter()->SetParameter("BurstBufferVerbose", "2"); 
  
  ltest(Synavis::ELogVerbosity::Info) << "Adding variable..." << std::endl;
  connector->AddVariable("data", "uint8_t");
  
  // Set up callbacks (matching adios2_coupling.py)
  ltest(Synavis::ELogVerbosity::Info) << "Setting up callbacks..." << std::endl;
  
  // Frame callback (equivalent to frame_callback in Python)
  connector->SetReadCallback([&received_frame_sizes](const std::variant<Synavis::AdiosConnector::binary, std::string>& data, const std::string& name) {
    if (std::holds_alternative<Synavis::AdiosConnector::binary>(data)) {
      const auto& binary_data = std::get<Synavis::AdiosConnector::binary>(data);
      ltest(Synavis::ELogVerbosity::Verbose) << "Received frame of type binary with size " << binary_data.size() << " bytes" << std::endl;
      received_frame_sizes.push_back(binary_data.size());
    } else {
      ltest(Synavis::ELogVerbosity::Verbose) << "Received frame of type string: " << std::get<std::string>(data) << std::endl;
    }
  });
  
  // Data callback (equivalent to data_callback in Python)
  connector->SetDataCallback([&received_data_sizes](const Synavis::AdiosConnector::binary& data) {
    ltest(Synavis::ELogVerbosity::Verbose) << "Received raw data packet of length " << data.size() << std::endl;
    received_data_sizes.push_back(data.size());
  });
  
  // Message callback (equivalent to message_callback in Python)
  connector->SetMessageCallback([&received_messages](const std::string& msg) {
    ltest(Synavis::ELogVerbosity::Verbose) << "Received message: " << msg << std::endl;
    received_messages.push_back(msg);
  });
  
  // Connection lifecycle callbacks
  connector->SetOnConnectedCallback([]() {
    ltest(Synavis::ELogVerbosity::Info) << "ADIOS2 BP4 streaming started (connected)" << std::endl;
  });
  
  connector->SetOnClosedCallback([]() {
    ltest(Synavis::ELogVerbosity::Info) << "ADIOS2 BP4 streaming stopped (closed)" << std::endl;
  });
  
  connector->SetOnFailedCallback([]() {
    ltest(Synavis::ELogVerbosity::Error) << "ADIOS2 connection failed" << std::endl;
  });
  
  ltest(Synavis::ELogVerbosity::Info) << "Starting ADIOS connection..." << std::endl;
  connector->StartStreaming();
  
  ltest(Synavis::ELogVerbosity::Info) << "Waiting for connection (2000ms)..." << std::endl;
  connector->LockUntilConnected(5000);
  
  // Send initial JSON commands (matching adios2_coupling.py)
  ltest(Synavis::ELogVerbosity::Info) << "Sending initial JSON commands..." << std::endl;
  std::vector<Synavis::AdiosConnector::json> initial_commands = {
    {{"type", "query"}},
    {{"type", "command"}, {"name", "start"}}
  };

  // double check that we are open
  connector->IsRunning();
  
  for (const auto& cmd : initial_commands) {
    ltest(Synavis::ELogVerbosity::Info) << "Sending JSON command: " << cmd.dump() << std::endl;
    connector->SendJSON(cmd);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  
  ltest(Synavis::ELogVerbosity::Info) << "Test passed - no crash during connection setup" << std::endl;
  
  // Wait a bit to see if crash happens later and to receive any callbacks
  ltest(Synavis::ELogVerbosity::Info) << "Waiting for callbacks (2s)..." << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(2));
  
  // Print summary of received data
  ltest(Synavis::ELogVerbosity::Info) << "=== Callback Summary ===" << std::endl;
  ltest(Synavis::ELogVerbosity::Info) << "Messages received: " << received_messages.size() << std::endl;
  ltest(Synavis::ELogVerbosity::Info) << "Data packets received: " << received_data_sizes.size() << std::endl;
  ltest(Synavis::ELogVerbosity::Info) << "Frames received: " << received_frame_sizes.size() << std::endl;
  
  ltest(Synavis::ELogVerbosity::Info) << "Stopping..." << std::endl;
  connector->StopReader();
  connector->StopStreaming();
  
  ltest(Synavis::ELogVerbosity::Info) << "Done." << std::endl;
  
  return 0;
}
