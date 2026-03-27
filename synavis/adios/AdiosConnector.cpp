#ifdef ADIOS2_AVAILABLE
#include "AdiosConnector.hpp"
#include <adios2.h>

namespace Synavis
{

  static const Logger::LoggerInstance ladios = Logger::Get()->LogStarter("AdiosConnector");

  AdiosConnector::AdiosConnector()
  {
  }

  AdiosConnector::~AdiosConnector()
  {
    StopStreaming();
    StopReader();
  }

  void AdiosConnector::Initialize()
  {
    running_ = true;
    state_ = EConnectionState::STARTUP;
    
    // Initialize ADIOS2
    adios_engine_ = std::make_unique<adios2::ADIOS>();
    ladios(ELogVerbosity::Info) << "ADIOS2 initialized" << std::endl;
  }

  void AdiosConnector::StartStreaming()
  {
    if (streaming_)
      return;
    
    streaming_ = true;
    state_ = EConnectionState::CONNECTED;
    
    CreateEngine();
    StartWorkerThread();
    
    ladios(ELogVerbosity::Info) << "ADIOS2 streaming started" << std::endl;
    
    if (OnConnectedCallback.has_value())
      OnConnectedCallback.value()();
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
    engine_type_ = EngineType;
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
    (void)additional_wait;
    // ADIOS2 doesn't need blocking connect - just wait for streaming state
    while (streaming_ && state_ != EConnectionState::CONNECTED)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  void AdiosConnector::PrintConfiguration() const
  {
    ladios(ELogVerbosity::Info) << "ADIOS2 Configuration:" << std::endl;
    ladios(ELogVerbosity::Info) << "  Engine Type: " << engine_type_ << std::endl;
    ladios(ELogVerbosity::Info) << "  IO Name: " << io_name_ << std::endl;
    ladios(ELogVerbosity::Info) << "  Variable Name: " << variable_name_ << std::endl;
    ladios(ELogVerbosity::Info) << "  Filename Prefix: " << filename_prefix_ << std::endl;
    ladios(ELogVerbosity::Info) << "  Mode: " << static_cast<int>(mode_) << std::endl;
  }

  void AdiosConnector::CreateEngine()
  {
    if (!adios_engine_ || !io_engine_)
      return;
    
    // Create filename based on prefix
    std::string filename = filename_prefix_ + "." + engine_type_;
    
    // Open engine in the specified mode
    current_engine_ = std::make_unique<adios2::Engine>(io_engine_->Open(filename, mode_));
    
    ladios(ELogVerbosity::Info) << "ADIOS2 engine opened with filename: " << filename << std::endl;
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
    if (!adios_engine_ || !io_engine_)
      return;
    
    // Create filename based on prefix for reading
    std::string filename = filename_prefix_ + "." + engine_type_;
    
    // Open reader engine in read mode
    reader_engine_ = std::make_unique<adios2::Engine>(io_engine_->Open(filename, adios2::Mode::Read));
    
    ladios(ELogVerbosity::Info) << "ADIOS2 reader engine opened with filename: " << filename << std::endl;
  }

  void AdiosConnector::AddVariable(const std::string& Name, const std::string& DataType)
  {
    if (!io_engine_)
      return;
    
    variable_types_[Name] = DataType;
    
    // Define variable in IO - actual type will be set when writing
    ladios(ELogVerbosity::Info) << "Added variable: " << Name << " (type: " << DataType << ")" << std::endl;
  }

  bool AdiosConnector::WriteStep()
  {
    if (!current_engine_ || !streaming_)
      return false;
    
    // Begin new step
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
          binary_variables_[msg.name] = io_engine_->DefineVariable<uint8_t>(msg.name);
        }
        
        current_engine_->Put(binary_variables_[msg.name], msg.data.data(), adios2::Mode::Sync);
      }
      else if (msg.type == "float64")
      {
        // Double array
        if (float64_variables_.find(msg.name) == float64_variables_.end())
        {
          float64_variables_[msg.name] = io_engine_->DefineVariable<double>(msg.name);
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
          float32_variables_[msg.name] = io_engine_->DefineVariable<float>(msg.name);
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
          int32_variables_[msg.name] = io_engine_->DefineVariable<int32_t>(msg.name);
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
          string_variables_[msg.name] = io_engine_->DefineVariable<std::string>(msg.name);
        }
        
        current_engine_->Put(string_variables_[msg.name], str_data, adios2::Mode::Sync);
      }
      
      step_messages.pop();
    }
    
    // End the step
    current_engine_->EndStep();
    
    return true;
  }

  void AdiosConnector::Flush()
  {
    if (current_engine_)
    {
      // Perform any pending operations
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
      try {
        reader_engine_->Close();
      } catch (...) {
      }
      reader_engine_.reset();
    }
    
    if (reader_thread_.joinable())
    {
      reader_thread_.join();
    }
    
    reader_running_ = false;
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
          for (const auto& [name, var] : string_variables_)
          {
            try {
              std::string value;
              auto var_copy = var;
              var_copy.SetSelection({{0}, {1}});
              reader_engine_->Get(var_copy, value);
              ReadCallback.value()(value, name);
            } catch (...) {
            }
          }
          
          for (const auto& [name, var] : binary_variables_)
          {
            try {
              auto var_copy = var;
              var_copy.SetSelection({{0}, {1}});
              size_t count = var_copy.Count()[0];
              std::vector<uint8_t> data(count);
              reader_engine_->Get(var_copy, data.data());
              ReadCallback.value()(data, name);
            } catch (...) {
            }
          }
          
          reader_engine_->EndStep();
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
}
#endif
