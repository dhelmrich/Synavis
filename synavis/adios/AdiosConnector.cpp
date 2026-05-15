#ifdef ADIOS2_AVAILABLE
#include "AdiosConnector.hpp"
#include <adios2.h>
#include <filesystem>
#include <stdexcept>
#include <chrono>

namespace Synavis
{

  static const Logger::LoggerInstance ladios = Logger::Get()->LogStarter("AdiosConnector");

  static std::string TransportToEngineString(EAdiosTransport Transport)
  {
    switch (Transport)
    {
      case EAdiosTransport::SST: return "SST";
      case EAdiosTransport::BPFile: return "BP4";
      case EAdiosTransport::DataServer: return "DataServer";
      case EAdiosTransport::File: return "File";
      case EAdiosTransport::Null: return "Null";
      default: return "BP4";
    }
  }

  AdiosConnector::AdiosConnector()
  {
    network_interface_ = "localhost";
    network_port_ = 9001;
    reader_cleanup_done_ = false;
    connection_retries_ = 10;
    connection_retry_delay_ms_ = 500;
  }

  AdiosConnector::~AdiosConnector()
  {
    StopStreaming();

    // Only stop reader if not already cleaned up
    if (!reader_cleanup_done_)
    {
      StopReader();
    }
  }

  void AdiosConnector::Initialize()
  {
    running_ = true;
    state_ = EConnectionState::STARTUP;

    adios_engine_ = std::make_unique<adios2::ADIOS>();

    ladios(ELogVerbosity::Info) << "ADIOS2 initialized" << std::endl;
  }

  void AdiosConnector::ApplyConfiguration()
  {
    if (io_engine_writer_ && io_engine_reader_)
      return;

    const std::string engine_name = TransportToEngineString(transport_type_);
    io_engine_writer_ = std::make_unique<adios2::IO>(adios_engine_->DeclareIO(io_name_));
    io_engine_writer_->SetEngine(engine_name.c_str());

    io_engine_writer_->SetParameter("TimeoutSec", "10");
    io_engine_writer_->SetParameter("NumAggregators", "0");
    io_engine_writer_->SetParameter("FlushStepsCount", "1");
    io_engine_writer_->SetParameter("StatsLevel", "1");
    io_engine_writer_->SetParameter("InitialBufferSize", "16Mb");

    if (transport_type_ == EAdiosTransport::SST)
    {
      io_engine_writer_->SetParameter("NetworkInterface", network_interface_);
      io_engine_writer_->SetParameter("Port", std::to_string(network_port_));
      io_engine_writer_->SetParameter("RendezvousReaderCount", "1");
      io_engine_writer_->SetParameter("InitialNumReaders", "1");
      io_engine_writer_->SetParameter("RegistrationMethod", "File");
      io_engine_writer_->SetParameter("StagingDirectory", filename_prefix_ + ".staging");
      io_engine_writer_->SetParameter("DataDirectory", filename_prefix_ + ".data");
    }

    io_engine_writer_->SetParameter("ManagesNetworkStack", "true");

    io_engine_reader_ = std::make_unique<adios2::IO>(adios_engine_->DeclareIO(io_name_ + "_Reader"));
    io_engine_reader_->SetEngine(engine_name.c_str());

    io_engine_reader_->SetParameter("TimeoutSec", "10");
    io_engine_reader_->SetParameter("NumAggregators", "0");
    io_engine_reader_->SetParameter("FlushStepsCount", "1");
    io_engine_reader_->SetParameter("StatsLevel", "1");
    io_engine_reader_->SetParameter("InitialBufferSize", "16Mb");

    if (transport_type_ == EAdiosTransport::SST)
    {
      io_engine_reader_->SetParameter("NetworkInterface", network_interface_);
      io_engine_reader_->SetParameter("Port", std::to_string(network_port_));
      io_engine_reader_->SetParameter("RendezvousReaderCount", "1");
      io_engine_reader_->SetParameter("InitialNumReaders", "1");
      io_engine_reader_->SetParameter("RegistrationMethod", "File");
      io_engine_reader_->SetParameter("StagingDirectory", filename_prefix_ + ".staging");
      io_engine_reader_->SetParameter("DataDirectory", filename_prefix_ + ".data");
    }

    io_engine_reader_->SetParameter("ManagesNetworkStack", "true");

    ladios(ELogVerbosity::Info) << "ADIOS2 configuration applied" << std::endl;
  }

  void AdiosConnector::StartStreaming()
  {
    if (streaming_)
      return;

    streaming_ = true;

    CreateEngine();
    StartWorkerThread();

    ladios(ELogVerbosity::Info) << "ADIOS2 streaming started" << std::endl;
  }

  void AdiosConnector::StopStreaming()
  {
    if (!streaming_)
      return;

    streaming_ = false;
    shutdown_requested_ = true;

    queue_cv_.notify_all();

    if (worker_thread_.joinable())
      worker_thread_.join();

    CloseEngine();

    state_ = EConnectionState::CLOSED;

    ladios(ELogVerbosity::Info) << "ADIOS2 streaming stopped" << std::endl;

    if (OnClosedCallback.has_value())
      OnClosedCallback.value()();
  }

  bool AdiosConnector::IsRunning() const
  {
    return running_;
  }

  EConnectionState AdiosConnector::GetState() const
  {
    return state_;
  }

  void AdiosConnector::SetEngineType(const std::string& EngineType)
  {
    if (EngineType == "SST")
    {
      transport_type_ = EAdiosTransport::SST;
    }
    else if (EngineType == "BP4")
    {
      transport_type_ = EAdiosTransport::BPFile;
    }
    else if (EngineType == "DataServer")
    {
      transport_type_ = EAdiosTransport::DataServer;
    }
    else if (EngineType == "File")
    {
      transport_type_ = EAdiosTransport::File;
    }
    else if (EngineType == "Null")
    {
      transport_type_ = EAdiosTransport::Null;
    }
    else
    {
      transport_type_ = EAdiosTransport::BPFile;
    }
  }
    else if (EngineType == "BP5")
    {
      transport_type_ = EAdiosTransport::BPFile;
    }
    else if (EngineType == "DataServer")
    {
      transport_type_ = EAdiosTransport::DataServer;
    }
    else if (EngineType == "File")
    {
      transport_type_ = EAdiosTransport::File;
    }
    else if (EngineType == "Null")
    {
      transport_type_ = EAdiosTransport::Null;
    }
    else
    {
      transport_type_ = EAdiosTransport::BPFile;
    }
  }

  void AdiosConnector::SetIOName(const std::string& IOName)
  {
    io_name_ = IOName;
  }

  void AdiosConnector::SetVariableName(const std::string& VariableName)
  {
    variable_name_ = VariableName;
  }

  void AdiosConnector::SetMode(adios2::Mode mode)
  {
    mode_ = mode;
  }

  void AdiosConnector::SetFilenamePrefix(const std::string& FilenamePrefix)
  {
    filename_prefix_ = FilenamePrefix;
  }

  void AdiosConnector::SetPort(int Port)
  {
    network_port_ = Port;
  }

  void AdiosConnector::SetNetworkInterface(const std::string& Interface)
  {
    network_interface_ = Interface;
  }

  void AdiosConnector::SetConnectionRetries(int retries)
  {
    connection_retries_ = retries;
  }

  void AdiosConnector::SetConnectionRetryDelayMs(int delay_ms)
  {
    connection_retry_delay_ms_ = delay_ms;
  }

  void AdiosConnector::SendData(const binary& Data)
  {
    if (!streaming_ || !current_engine_)
      return;

    QueuedMessage msg;
    msg.data = Data;
    msg.type = "data";
    msg.name = variable_name_;
    msg.format = "raw";

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      message_queue_.push(std::move(msg));
    }

    queue_cv_.notify_one();
  }

  void AdiosConnector::SendString(const std::string& Message)
  {
    if (!streaming_ || !current_engine_)
      return;

    QueuedMessage msg;
    msg.data = binary(Message.begin(), Message.end());
    msg.type = "string";
    msg.name = variable_name_ + "_string";
    msg.format = "text";

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      message_queue_.push(std::move(msg));
    }

    queue_cv_.notify_one();
  }

  void AdiosConnector::SendJSON(const json& Message)
  {
    if (!streaming_ || !current_engine_)
      return;

    std::string json_str = Message.dump();
    QueuedMessage msg;
    msg.data = binary(json_str.begin(), json_str.end());
    msg.type = "json";
    msg.name = variable_name_ + "_json";
    msg.format = "json";

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      message_queue_.push(std::move(msg));
    }

    queue_cv_.notify_one();
  }

  bool AdiosConnector::SendBuffer(const binary& Buffer, const std::string& Name, const std::string& Format)
  {
    if (!streaming_ || !current_engine_)
      return false;

    QueuedMessage msg;
    msg.data = Buffer;
    msg.type = "buffer";
    msg.name = Name;
    msg.format = Format;

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      message_queue_.push(std::move(msg));
    }

    queue_cv_.notify_one();
    return true;
  }

  bool AdiosConnector::SendFloat64Buffer(const std::vector<double>& Buffer, const std::string& Name, const std::string& Format)
  {
    if (!streaming_ || !current_engine_)
      return false;

    // Convert to bytes for queueing
    binary data(Buffer.size() * sizeof(double));
    std::memcpy(data.data(), Buffer.data(), data.size());

    QueuedMessage msg;
    msg.data = std::move(data);
    msg.type = "float64";
    msg.name = Name;
    msg.format = Format;

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      message_queue_.push(std::move(msg));
    }

    queue_cv_.notify_one();
    return true;
  }

  bool AdiosConnector::SendFloat32Buffer(const std::vector<float>& Buffer, const std::string& Name, const std::string& Format)
  {
    if (!streaming_ || !current_engine_)
      return false;

    // Convert to bytes for queueing
    binary data(Buffer.size() * sizeof(float));
    std::memcpy(data.data(), Buffer.data(), data.size());

    QueuedMessage msg;
    msg.data = std::move(data);
    msg.type = "float32";
    msg.name = Name;
    msg.format = Format;

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      message_queue_.push(std::move(msg));
    }

    queue_cv_.notify_one();
    return true;
  }

  bool AdiosConnector::SendInt32Buffer(const std::vector<int32_t>& Buffer, const std::string& Name, const std::string& Format)
  {
    if (!streaming_ || !current_engine_)
      return false;

    // Convert to bytes for queueing
    binary data(Buffer.size() * sizeof(int32_t));
    std::memcpy(data.data(), Buffer.data(), data.size());

    QueuedMessage msg;
    msg.data = std::move(data);
    msg.type = "int32";
    msg.name = Name;
    msg.format = Format;

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      message_queue_.push(std::move(msg));
    }

    queue_cv_.notify_one();
    return true;
  }

  void AdiosConnector::SetDataCallback(std::function<void(const binary&)> Callback)
  {
    DataReceptionCallback = Callback;
  }

  void AdiosConnector::SetMessageCallback(std::function<void(const std::string&)> Callback)
  {
    MessageReceptionCallback = Callback;
  }

  void AdiosConnector::SetReadCallback(std::function<void(const std::variant<binary, std::string>&, const std::string&)> Callback)
  {
    ReadCallback = Callback;
  }

  void AdiosConnector::SetOnConnectedCallback(std::function<void(void)> Callback)
  {
    OnConnectedCallback = Callback;
  }

  void AdiosConnector::SetOnFailedCallback(std::function<void(void)> Callback)
  {
    OnFailedCallback = Callback;
  }

  void AdiosConnector::SetOnClosedCallback(std::function<void(void)> Callback)
  {
    OnClosedCallback = Callback;
  }

  void AdiosConnector::LockUntilConnected(unsigned additional_wait)
  {
    ladios(ELogVerbosity::Info) << "Waiting for connection (timeout=" << additional_wait << "ms)..." << std::endl;

    unsigned elapsed = 0;
    const unsigned check_interval = 50;

    while (state_ != EConnectionState::CONNECTED && state_ != EConnectionState::FAILED)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(check_interval));
      elapsed += check_interval;

      if (additional_wait > 0 && elapsed >= additional_wait)
      {
        ladios(ELogVerbosity::Error) << "LockUntilConnected timeout after " << elapsed << "ms, state=" << static_cast<int>(state_.load()) << std::endl;
        state_ = EConnectionState::FAILED;
        if (OnFailedCallback.has_value())
          OnFailedCallback.value()();
        return;
      }
    }

    if (state_ == EConnectionState::CONNECTED && additional_wait > 0)
      std::this_thread::sleep_for(std::chrono::milliseconds(additional_wait));

    ladios(ELogVerbosity::Info) << "Connection status: state=" << static_cast<int>(state_.load()) << ", elapsed=" << elapsed << "ms" << std::endl;
  }

  void AdiosConnector::PrintConfiguration() const
  {
    ladios(ELogVerbosity::Info) << "ADIOS2 Configuration:" << std::endl;
    ladios(ELogVerbosity::Info) << "  Engine Type: " << TransportToEngineString(transport_type_) << std::endl;
    ladios(ELogVerbosity::Info) << "  IO Name: " << io_name_ << std::endl;
    ladios(ELogVerbosity::Info) << "  Variable Name: " << variable_name_ << std::endl;
    ladios(ELogVerbosity::Info) << "  Filename Prefix: " << filename_prefix_ << std::endl;
    ladios(ELogVerbosity::Info) << "  Mode: " << static_cast<int>(mode_) << std::endl;
    ladios(ELogVerbosity::Info) << "  Network Interface: " << network_interface_ << std::endl;
    ladios(ELogVerbosity::Info) << "  Network Port: " << network_port_ << std::endl;
  }

  void AdiosConnector::CreateEngine()
  {
    if (!adios_engine_ || !io_engine_writer_)
      return;

    ladios(ELogVerbosity::Info) << "Initializing engine for writer" << std::endl;
    ladios(ELogVerbosity::Info) << "Engine type from IO: " << io_engine_writer_->EngineType() << std::endl;

    if (transport_type_ == EAdiosTransport::SST)
    {
      io_engine_writer_->SetParameter("NetworkInterface", network_interface_);
      io_engine_writer_->SetParameter("Port", std::to_string(network_port_));
      io_engine_writer_->SetParameter("RendezvousReaderCount", "1");
      io_engine_writer_->SetParameter("InitialNumReaders", "1");
      io_engine_writer_->SetParameter("RegistrationMethod", "File");
      io_engine_writer_->SetParameter("StagingDirectory", filename_prefix_ + ".staging");
      io_engine_writer_->SetParameter("DataDirectory", filename_prefix_ + ".data");
    }
    else if (transport_type_ == EAdiosTransport::BPFile)
    {
      io_engine_writer_->SetParameter("NumAggregators", "0");
      io_engine_writer_->SetParameter("FlushStepsCount", "1");
      io_engine_writer_->SetParameter("StatsLevel", "1");
      io_engine_writer_->SetParameter("InitialBufferSize", "16Mb");
      io_engine_writer_->SetParameter("BufferGrowthFactor", "1.05");
    }

    for (const auto& [name, type] : variable_types_)
    {
      if (type == "uint8_t")
      {
        binary_variables_[name] = io_engine_writer_->DefineVariable<uint8_t>(name);
        binary_variables_reader_[name] = io_engine_reader_->DefineVariable<uint8_t>(name);
      }
      else if (type == "float64" || type == "double")
      {
        float64_variables_[name] = io_engine_writer_->DefineVariable<double>(name);
        float64_variables_reader_[name] = io_engine_reader_->DefineVariable<double>(name);
      }
      else if (type == "float32" || type == "float")
      {
        float32_variables_[name] = io_engine_writer_->DefineVariable<float>(name);
        float32_variables_reader_[name] = io_engine_reader_->DefineVariable<float>(name);
      }
      else if (type == "int32" || type == "int")
      {
        int32_variables_[name] = io_engine_writer_->DefineVariable<int32_t>(name);
        int32_variables_reader_[name] = io_engine_reader_->DefineVariable<int32_t>(name);
      }
      else if (type == "string")
      {
        string_variables_[name] = io_engine_writer_->DefineVariable<std::string>(name);
        string_variables_reader_[name] = io_engine_reader_->DefineVariable<std::string>(name);
      }
      ladios(ELogVerbosity::Info) << "Defined variable '" << name << "' with type '" << type << "'" << std::endl;
    }

    std::string writer_filename = filename_prefix_;

    ladios(ELogVerbosity::Info) << "Attempting to open writer engine with retry logic (retries=" << connection_retries_
      << ", delay_ms=" << connection_retry_delay_ms_ << ")" << std::endl;

    bool engine_opened = false;
    for (int attempt = 1; attempt <= connection_retries_; ++attempt)
    {
      try
      {
        current_engine_ = std::make_unique<adios2::Engine>(io_engine_writer_->Open(writer_filename, mode_));
        engine_opened = true;
        ladios(ELogVerbosity::Info) << "Successfully opened writer engine with engine type: " << io_engine_writer_->EngineType() << std::endl;
        break;
      }
      catch (const std::exception& e)
      {
        ladios(ELogVerbosity::Warning) << "Engine open attempt " << attempt << " failed: " << e.what() << std::endl;

        if (attempt < connection_retries_)
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(connection_retry_delay_ms_));
        }
      }
    }

    if (!engine_opened)
    {
      ladios(ELogVerbosity::Error) << "Failed to open ADIOS2 engine after " << connection_retries_ << " attempts" << std::endl;
      state_ = EConnectionState::FAILED;
      if (OnFailedCallback.has_value())
        OnFailedCallback.value()();
      return;
    }

    ladios(ELogVerbosity::Info) << "ADIOS2 writer engine opened with filename: " << writer_filename << std::endl;

    state_ = EConnectionState::CONNECTED;
    writer_ready_ = true;

    if (OnConnectedCallback.has_value())
      OnConnectedCallback.value()();
  }

  void AdiosConnector::CloseEngine()
  {
    if (current_engine_)
    {
      current_engine_->Close();
      current_engine_.reset();
      ladios(ELogVerbosity::Info) << "ADIOS2 engine closed" << std::endl;
    }
  }

  void AdiosConnector::CreateReaderEngine()
  {
    if (!adios_engine_ || !io_engine_reader_)
      return;

    ladios(ELogVerbosity::Info) << "Initializing engine for reader" << std::endl;
    ladios(ELogVerbosity::Info) << "Engine type from IO: " << io_engine_reader_->EngineType() << std::endl;

    io_engine_reader_->SetParameter("TimeoutSec", "10");
    io_engine_reader_->SetParameter("RendezvousReaderCount", "1");
    io_engine_reader_->SetParameter("InitialNumReaders", "1");
    io_engine_reader_->SetParameter("ManagesNetworkStack", "true");
    io_engine_reader_->SetParameter("NumAggregators", "0");
    io_engine_reader_->SetParameter("FlushStepsCount", "1");
    io_engine_reader_->SetParameter("StatsLevel", "1");
    io_engine_reader_->SetParameter("InitialBufferSize", "16Mb");

    if (transport_type_ == EAdiosTransport::SST)
    {
      io_engine_reader_->SetParameter("NetworkInterface", network_interface_);
      io_engine_reader_->SetParameter("Port", std::to_string(network_port_));
      io_engine_reader_->SetParameter("StagingDirectory", filename_prefix_ + ".staging");
      io_engine_reader_->SetParameter("DataDirectory", filename_prefix_ + ".data");
    }
    else if (transport_type_ == EAdiosTransport::BPFile)
    {
      io_engine_reader_->SetParameter("StreamReader", "Off");
    }

    std::string reader_filename = filename_prefix_;

    ladios(ELogVerbosity::Info) << "Attempting to open reader engine with retry logic (retries=" << connection_retries_
      << ", delay_ms=" << connection_retry_delay_ms_ << ")" << std::endl;

    bool engine_opened = false;
    for (int attempt = 1; attempt <= connection_retries_; ++attempt)
    {
      try
      {
        reader_engine_ = std::make_unique<adios2::Engine>(io_engine_reader_->Open(reader_filename, adios2::Mode::Read));
        engine_opened = true;
        ladios(ELogVerbosity::Info) << "Successfully opened reader engine with engine type: " << io_engine_reader_->EngineType() << std::endl;
        break;
      }
      catch (const std::exception& e)
      {
        ladios(ELogVerbosity::Warning) << "Reader engine open attempt " << attempt << " failed: " << e.what() << std::endl;

        if (attempt < connection_retries_)
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(connection_retry_delay_ms_));
        }
      }
    }

    if (!engine_opened)
    {
      ladios(ELogVerbosity::Error) << "Failed to open ADIOS2 reader engine after " << connection_retries_ << " attempts" << std::endl;
      return;
    }

    ladios(ELogVerbosity::Info) << "ADIOS2 reader engine opened with filename: " << reader_filename << std::endl;
  }

  void AdiosConnector::AddVariable(const std::string& Name, const std::string& DataType)
  {
    if (!io_engine_writer_)
      return;

    variable_types_[Name] = DataType;

    // Define variable in IO - actual type will be set when writing
    ladios(ELogVerbosity::Info) << "Added variable: " << Name << " (type: " << DataType << ")" << std::endl;
  }

  bool AdiosConnector::WriteStep()
  {
    if (!current_engine_ || !streaming_)
      return false;

    auto status = current_engine_->BeginStep();
    if (status != adios2::StepStatus::OK)
    {
      ladios(ELogVerbosity::Warning) << "BeginStep failed: " << static_cast<int>(status) << std::endl;
      return false;
    }

    // Process all queued messages for this step
    std::queue<QueuedMessage> step_messages;

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      while (!message_queue_.empty())
      {
        step_messages.push(std::move(message_queue_.front()));
        message_queue_.pop();
      }
    }

    // Write each message as a variable
    while (!step_messages.empty())
    {
      const auto& msg = step_messages.front();

      if (msg.type == "data" || msg.type == "buffer")
      {
        // Binary data - write as uint8_t array
        if (binary_variables_.find(msg.name) == binary_variables_.end())
        {
          binary_variables_[msg.name] = io_engine_writer_->DefineVariable<uint8_t>(msg.name);
        }

        std::vector<uint8_t> data(msg.data.begin(), msg.data.end());
        current_engine_->Put(binary_variables_[msg.name], data.data(), adios2::Mode::Sync);
      }
      else if (msg.type == "float64")
      {
        // Double array
        if (float64_variables_.find(msg.name) == float64_variables_.end())
        {
          float64_variables_[msg.name] = io_engine_writer_->DefineVariable<double>(msg.name);
        }

        std::vector<double> data(msg.data.size() / sizeof(double));
        std::memcpy(data.data(), msg.data.data(), msg.data.size());
        current_engine_->Put(float64_variables_[msg.name], data.data(), adios2::Mode::Sync);
      }
      else if (msg.type == "float32")
      {
        // Float array
        if (float32_variables_.find(msg.name) == float32_variables_.end())
        {
          float32_variables_[msg.name] = io_engine_writer_->DefineVariable<float>(msg.name);
        }

        std::vector<float> data(msg.data.size() / sizeof(float));
        std::memcpy(data.data(), msg.data.data(), msg.data.size());
        current_engine_->Put(float32_variables_[msg.name], data.data(), adios2::Mode::Sync);
      }
      else if (msg.type == "int32")
      {
        // Int32 array
        if (int32_variables_.find(msg.name) == int32_variables_.end())
        {
          int32_variables_[msg.name] = io_engine_writer_->DefineVariable<int32_t>(msg.name);
        }

        std::vector<int32_t> data(msg.data.size() / sizeof(int32_t));
        std::memcpy(data.data(), msg.data.data(), msg.data.size());
        current_engine_->Put(int32_variables_[msg.name], data.data(), adios2::Mode::Sync);
      }
      else if (msg.type == "string" || msg.type == "json")
      {
        // String data
        std::string str_data(msg.data.begin(), msg.data.end());

        if (string_variables_.find(msg.name) == string_variables_.end())
        {
          string_variables_[msg.name] = io_engine_writer_->DefineVariable<std::string>(msg.name);
        }

        current_engine_->Put(string_variables_[msg.name], str_data, adios2::Mode::Sync);
      }

      step_messages.pop();
    }

    current_engine_->EndStep();

    return true;
  }

  void AdiosConnector::Flush()
  {
    if (current_engine_)
    {
      current_engine_->PerformPuts();
    }
  }

  void AdiosConnector::StartWorkerThread()
  {
    shutdown_requested_ = false;
    worker_thread_ = std::thread(&AdiosConnector::WorkerLoop, this);
  }

  void AdiosConnector::StopWorkerThread()
  {
    shutdown_requested_ = true;
    queue_cv_.notify_all();
  }

  void AdiosConnector::WorkerLoop()
  {
    while (!shutdown_requested_)
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);

      queue_cv_.wait_for(lock, std::chrono::milliseconds(100), [this]
        {
          return !message_queue_.empty() || shutdown_requested_;
        });

      if (!message_queue_.empty() && current_engine_)
      {
        lock.unlock();
        WriteStep();
      }
    }

    // Final flush
    if (current_engine_)
    {
      WriteStep();
    }
  }

  void AdiosConnector::StartReader()
  {
    reader_running_ = true;
    reader_shutdown_ = false;
    
    ladios(ELogVerbosity::Info) << "Waiting for writer engine to be ready before starting reader..." << std::endl;
    
    unsigned wait_time = 0;
    const unsigned check_interval = reader_startup_check_interval_ms_;
    const unsigned max_wait_time = reader_startup_timeout_ms_;
    
    while (!writer_ready_ && wait_time < max_wait_time)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(check_interval));
      wait_time += check_interval;
    }
    
    if (!writer_ready_)
    {
      ladios(ELogVerbosity::Error) << "Writer engine not ready after " << max_wait_time << "ms, aborting reader startup" << std::endl;
      reader_running_ = false;
      return;
    }
    
    ladios(ELogVerbosity::Info) << "Writer engine ready after " << wait_time << "ms, starting reader..." << std::endl;
    CreateReaderEngine();
    
    reader_thread_ = std::thread(&AdiosConnector::ReaderLoop, this);
    ladios(ELogVerbosity::Info) << "ADIOS2 reader started" << std::endl;
  }

  void AdiosConnector::StopReader()
  {
    if (!reader_running_)
      return;

    reader_shutdown_ = true;

    if (reader_engine_)
    {
      reader_engine_->Close();
      reader_engine_.reset();
    }

    if (reader_thread_.joinable())
    {
      reader_thread_.join();
    }

    reader_running_ = false;
    reader_cleanup_done_ = true;
    ladios(ELogVerbosity::Info) << "ADIOS2 reader stopped" << std::endl;
  }

  bool AdiosConnector::ReadStep()
  {
    if (!reader_engine_)
      return false;

    auto status = reader_engine_->BeginStep();
    if (status == adios2::StepStatus::OK)
    {
      return true;
    }
    else if (status == adios2::StepStatus::EndOfStream)
    {
      reader_engine_->EndStep();
      return false;
    }
    return false;
  }

  void AdiosConnector::ReaderLoop()
  {
    while (!reader_shutdown_ && reader_running_)
    {
      if (ReadStep())
      {
        if (ReadCallback.has_value())
        {
          for (const auto& [name, var] : string_variables_reader_)
          {
            if (!reader_engine_)
              continue;
            std::string value;
            auto var_copy = var;
            var_copy.SetSelection({ {0}, {1} });
            reader_engine_->Get(var_copy, value);
            ReadCallback.value()(value, name);
          }

          for (const auto& [name, var] : binary_variables_reader_)
          {
            if (!reader_engine_)
              continue;
            auto var_copy = var;
            var_copy.SetSelection({ {0}, {1} });
            size_t count = var_copy.Count()[0];
            std::vector<uint8_t> data(count);
            reader_engine_->Get(var_copy, data.data());
            ReadCallback.value()(data, name);
          }

          reader_engine_->EndStep();
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
}
#endif
