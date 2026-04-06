#pragma once
#ifndef SYNAVIS_ADIOSCONNECTOR_HPP
#define SYNAVIS_ADIOSCONNECTOR_HPP

#include <json.hpp>
#include <vector>
#include <optional>
#include <functional>
#include <string>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <filesystem>
#include <adios2.h>
#include "adios_export.hpp"
#include "Synavis.hpp"

namespace Synavis
{

enum class EAdiosTransport
{
    SST,
    BPFile,
    DataServer,
    File,
    Null
};

class ADIOS_CONNECTOR_EXPORT AdiosConnector : public std::enable_shared_from_this<AdiosConnector>
{
public:
  using json = nlohmann::json;
  using binary = std::vector<uint8_t>;

  AdiosConnector();
  virtual ~AdiosConnector();

  // Core lifecycle
  virtual void Initialize();
  void ApplyConfiguration();
  virtual void StartStreaming();
  virtual void StopStreaming();
  bool IsRunning() const;
  EConnectionState GetState() const;

   // Configuration
   void SetEngineType(const std::string& EngineType);
   void SetIOName(const std::string& IOName);
   void SetVariableName(const std::string& VariableName);
   void SetMode(::adios2::Mode mode);
    void SetFilenamePrefix(const std::string& FilenamePrefix);

    // Network configuration
    void SetPort(int Port);
    void SetNetworkInterface(const std::string& Interface);
    int GetPort() const { return network_port_; }
    std::string GetNetworkInterface() const { return network_interface_; }
    
    // Connection retry configuration
    void SetConnectionRetries(int retries);
    void SetConnectionRetryDelayMs(int delay_ms);
    int GetConnectionRetries() const { return connection_retries_; }
    int GetConnectionRetryDelayMs() const { return connection_retry_delay_ms_; }

    EAdiosTransport GetTransportType() const { return transport_type_; }
    void SetTransportType(EAdiosTransport TransportType) { transport_type_ = TransportType; }

  // Sending data - mirrors DataConnector interface
  virtual void SendData(const binary& Data);
  virtual void SendString(const std::string& Message);
  virtual void SendJSON(const json& Message);
  bool SendBuffer(const binary& Buffer, const std::string& Name, const std::string& Format = "raw");
  bool SendFloat64Buffer(const std::vector<double>& Buffer, const std::string& Name, const std::string& Format = "raw");
  bool SendFloat32Buffer(const std::vector<float>& Buffer, const std::string& Name, const std::string& Format = "raw");
  bool SendInt32Buffer(const std::vector<int32_t>& Buffer, const std::string& Name, const std::string& Format = "raw");

  // Callbacks
  void SetDataCallback(std::function<void(const binary&)> Callback);
  void SetMessageCallback(std::function<void(const std::string&)> Callback);

  // Connection lifecycle callbacks
  void SetOnConnectedCallback(std::function<void(void)> Callback);
  void SetOnFailedCallback(std::function<void(void)> Callback);
  void SetOnClosedCallback(std::function<void(void)> Callback);

  // Blocking wait for connection
  void LockUntilConnected(unsigned additional_wait = 0);

  // Diagnostic
  void PrintConfiguration() const;

  // ADIOS2-specific functionality
  void AddVariable(const std::string& Name, const std::string& DataType);
  bool WriteStep();
  void Flush();

  // Reader functionality for bidirectional streaming
  void StartReader();
  void StopReader();
  bool ReadStep();
  void SetReadCallback(std::function<void(const std::variant<binary, std::string>&, const std::string&)> Callback);

protected:
  // ADIOS2 engine and IO management
  void CreateEngine();
  void CloseEngine();
  void CreateReaderEngine();

  // Thread management
  void StartWorkerThread();
  void StopWorkerThread();
  void WorkerLoop();
  void ReaderLoop();

  // State
  std::atomic<EConnectionState> state_{EConnectionState::STARTUP};
  std::atomic<bool> running_{false};
  std::atomic<bool> streaming_{false};
  std::atomic<bool> writer_ready_{false};

    // Configuration
     EAdiosTransport transport_type_{EAdiosTransport::BPFile};
    std::string io_name_{"AdiosIO"};
    std::string variable_name_{"SynavisData"};
    std::string filename_prefix_{"synavis_output"};
    adios2::Mode mode_{adios2::Mode::Write};
    
   // Connection retry settings (for SST engine startup timing)
     int connection_retries_{10};
     int connection_retry_delay_ms_{500};
     int reader_startup_timeout_ms_{10000};
     int reader_startup_check_interval_ms_{100};

   // Network configuration (for SST engine)
    std::string network_interface_{"localhost"};
    int network_port_{9001};

  // ADIOS2 objects
  std::unique_ptr<adios2::ADIOS> adios_engine_;
  std::unique_ptr<adios2::IO> io_engine_writer_;
  std::unique_ptr<adios2::Engine> current_engine_;
  std::map<std::string, adios2::Variable<uint8_t>> binary_variables_;
  std::map<std::string, adios2::Variable<double>> float64_variables_;
  std::map<std::string, adios2::Variable<float>> float32_variables_;
  std::map<std::string, adios2::Variable<int32_t>> int32_variables_;
  std::map<std::string, adios2::Variable<std::string>> string_variables_;
  std::map<std::string, std::string> variable_types_;
  
  // Reader-specific IO and engine
  std::unique_ptr<adios2::IO> io_engine_reader_;
  std::unique_ptr<adios2::Engine> reader_engine_;
  std::map<std::string, adios2::Variable<uint8_t>> binary_variables_reader_;
  std::map<std::string, adios2::Variable<double>> float64_variables_reader_;
  std::map<std::string, adios2::Variable<float>> float32_variables_reader_;
  std::map<std::string, adios2::Variable<int32_t>> int32_variables_reader_;
  std::map<std::string, adios2::Variable<std::string>> string_variables_reader_;

  // Callbacks
  std::optional<std::function<void(const binary&)>> DataReceptionCallback;
  std::optional<std::function<void(const std::string&)>> MessageReceptionCallback;
  std::optional<std::function<void(const std::variant<binary, std::string>&, const std::string&)>> ReadCallback;

  std::optional<std::function<void(void)>> OnConnectedCallback;
  std::optional<std::function<void(void)>> OnFailedCallback;
  std::optional<std::function<void(void)>> OnClosedCallback;

  // Thread and synchronization
  std::thread worker_thread_;
  std::thread reader_thread_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::atomic<bool> shutdown_requested_{false};
  std::atomic<bool> reader_running_{false};
  std::atomic<bool> reader_shutdown_{false};
  std::atomic<bool> reader_cleanup_done_{false};

  // Message queue for async processing
  struct QueuedMessage
  {
    binary data;
    std::string type;
    std::string name;
    std::string format;
  };
  std::queue<QueuedMessage> message_queue_;
};

}

#endif
