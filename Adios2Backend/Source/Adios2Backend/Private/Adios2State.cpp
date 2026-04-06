#include "Adios2State.h"
#include <adios2_c.h>

FAdios2State::FAdios2State()
  : AdiosPtr(nullptr)
  , IOPtr(nullptr)
    , WriterEnginePtr(nullptr)
  , IOPtrReader(nullptr)
    , ReaderEnginePtr(nullptr)
    , WriterEngineTypeStr(TEXT("BP5"))
    , WriterFilenamePrefixStr(TEXT("synavis_output"))
    , WriterTransportType(EAdiosTransport::BPFile)
  , bRunning(false)
  , bInitialized(false)
  , bIsConnecting(false)
  , bReaderRunning(false)
  , Port(9001)
  , Hostname(TEXT("localhost"))
  , ConnectionRetries(10)
  , ConnectionRetryDelayMs(500)
{
}

FAdios2State::~FAdios2State()
{
  StopStreaming();
  StopReader();
}

void FAdios2State::Initialize()
{
  if (bInitialized)
    return;

  UE_LOG(LogTemp, Log, TEXT("FAdios2State::Initialize() - begin"))
  AdiosPtr = adios2_init_serial();
  bInitialized = true;
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::Initialize() - adios2_init_serial() completed, AdiosPtr=%p"), AdiosPtr)
}

void FAdios2State::StartStreaming(const FString& EngineType, const FString& FilenamePrefix)
{
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - begin"))
  if (bRunning)
  {
    UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - already running, returning"))
    return;
  }

  if (!bInitialized)
  {
    UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - calling Initialize()"))
    Initialize();
  }

    WriterFilenamePrefixStr = FilenamePrefix;
    
    if (EngineType == TEXT("sst"))
    {
        WriterTransportType = EAdiosTransport::SST;
    }
    else if (EngineType == TEXT("BP5"))
    {
        WriterTransportType = EAdiosTransport::BPFile;
    }
    else if (EngineType == TEXT("dataserver"))
    {
        WriterTransportType = EAdiosTransport::DataServer;
    }
    else
    {
        WriterTransportType = EAdiosTransport::BPFile;
    }

    FString WriterFilenamePrefix = WriterFilenamePrefixStr;

   UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - declaring IO"))
   IOPtr = adios2_declare_io(AdiosPtr, "AdiosIO");
   UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - adios2_declare_io() returned IOPtr=%p"), IOPtr)

    FString EngineTypeString = GetTransportTypeString();
    const char* engineTypeCStr = TCHAR_TO_UTF8(*EngineTypeString);
    const char* filenamePrefixCStr = TCHAR_TO_UTF8(*WriterFilenamePrefixStr);

    UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - setting engine type to %hs"), engineTypeCStr)
     adios2_error writer_engine_result = adios2_set_engine(IOPtr, engineTypeCStr);
     UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - adios2_set_engine() returned error=%d"), writer_engine_result)

     if (WriterTransportType == EAdiosTransport::SST)
    {
      const char* hostnameCStr = TCHAR_TO_UTF8(*Hostname);
      UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - setting NetworkInterface=%hs, Port=%d"), hostnameCStr, Port)
      adios2_set_parameter(IOPtr, "NetworkInterface", hostnameCStr);
      
      char portStr[16];
      sprintf_s(portStr, "%d", Port);
      adios2_error portResult = adios2_set_parameter(IOPtr, "Port", portStr);
      UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - adios2_set_parameter(Port) returned error=%d"), portResult);
      
      adios2_set_parameter(IOPtr, "TimeoutSec", "10");
      adios2_set_parameter(IOPtr, "RendezvousReaderCount", "1");
      adios2_set_parameter(IOPtr, "InitialNumReaders", "1");
      adios2_set_parameter(IOPtr, "ManagesNetworkStack", "true");
  adios2_set_parameter(IOPtr, "StagingDirectory", TCHAR_TO_UTF8(*(WriterFilenamePrefixStr + TEXT(".staging"))));
  adios2_set_parameter(IOPtr, "DataDirectory", TCHAR_TO_UTF8(*(WriterFilenamePrefixStr + TEXT(".data"))));
    }

  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - defining variables"))
  adios2_variable* dataVar = adios2_define_variable(IOPtr, "data", adios2_type_uint8_t, 0, NULL, NULL, NULL, adios2_constant_dims_false);
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - adios2_define_variable(data) returned var=%p"), dataVar)
  adios2_variable* stringVar = adios2_define_variable(IOPtr, "string", adios2_type_string, 0, NULL, NULL, NULL, adios2_constant_dims_false);
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - adios2_define_variable(string) returned var=%p"), stringVar)
  adios2_variable* jsonVar = adios2_define_variable(IOPtr, "json", adios2_type_string, 0, NULL, NULL, NULL, adios2_constant_dims_false);
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - adios2_define_variable(json) returned var=%p"), jsonVar)

  const char* engineNameCStr = TCHAR_TO_UTF8(*WriterFilenamePrefix);
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - opening engine with name %hs"), engineNameCStr)
  
    WriterEnginePtr = nullptr;
    bIsConnecting = true;
   
    for (int Retry = 0; Retry < ConnectionRetries; Retry++)
    {
      if (Retry > 0)
      {
        UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - retry %d/%d after %dms"), Retry, ConnectionRetries, ConnectionRetryDelayMs)
        FPlatformProcess::Sleep((float)ConnectionRetryDelayMs / 1000.0f);
      }
      
      WriterEnginePtr = adios2_open(IOPtr, engineNameCStr, adios2_mode_write);
      if (WriterEnginePtr)
      {
        UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - adios2_open() succeeded on retry %d"), Retry)
        break;
      }
      else
      {
        UE_LOG(LogTemp, Warning, TEXT("FAdios2State::StartStreaming() - adios2_open() returned NULL, waiting for reader..."))
      }
    }
    
    bIsConnecting = false;
    
    if (!WriterEnginePtr)
    {
      UE_LOG(LogTemp, Error, TEXT("FAdios2State::StartStreaming() - failed to open engine after %d retries"), ConnectionRetries)
      return;
    }
  
  bRunning = true;

   UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - completed, engine=%s, prefix=%s, hostname=%s, port=%d"), 
          *EngineTypeString, *WriterFilenamePrefixStr, *Hostname, Port)
}

void FAdios2State::StopStreaming()
{
  if (!bRunning && !bIsConnecting)
    return;

  if (WriterEnginePtr)
  {
     adios2_error closeResult = adios2_close(WriterEnginePtr);
     UE_LOG(LogTemp, Log, TEXT("FAdios2State::StopStreaming() - adios2_close() returned error=%d"), closeResult)
     WriterEnginePtr = nullptr;
  }

  if (IOPtr)
  {
    adios2_bool removeResult = adios2_true;
    adios2_error removeIoResult = adios2_remove_io(&removeResult, AdiosPtr, "AdiosIO");
    UE_LOG(LogTemp, Log, TEXT("FAdios2State::StopStreaming() - adios2_remove_io() returned error=%d, result=%d"), removeIoResult, removeResult)
    IOPtr = nullptr;
  }

  bRunning = false;
  bIsConnecting = false;

  UE_LOG(LogTemp, Log, TEXT("ADIOS2 streaming stopped"))
}

void FAdios2State::StartReader()
{
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartReader() - begin"))
  if (bReaderRunning)
  {
    UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartReader() - reader already running, returning"))
    return;
  }

  if (!bInitialized)
  {
    UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartReader() - calling Initialize()"))
    Initialize();
  }

  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartReader() - declaring reader IO"))
  IOPtrReader = adios2_declare_io(AdiosPtr, "AdiosIO_Reader");
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartReader() - adios2_declare_io() returned IOPtrReader=%p"), IOPtrReader)

  FString EngineTypeString = GetTransportTypeString();
  const char* engineTypeCStr = TCHAR_TO_UTF8(*EngineTypeString);
  adios2_error reader_engine_result = adios2_set_engine(IOPtrReader, engineTypeCStr);
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartReader() - adios2_set_engine() returned error=%d"), reader_engine_result)

  if (WriterTransportType == EAdiosTransport::SST)
  {
    const char* hostnameCStr = TCHAR_TO_UTF8(*Hostname);
    UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartReader() - setting NetworkInterface=%hs, Port=%d"), hostnameCStr, Port)
    adios2_set_parameter(IOPtrReader, "NetworkInterface", hostnameCStr);
    
    char portStr[16];
    sprintf_s(portStr, "%d", Port);
    adios2_error portResult = adios2_set_parameter(IOPtrReader, "Port", portStr);
    UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartReader() - adios2_set_parameter(Port) returned error=%d"), portResult);
    
    adios2_set_parameter(IOPtrReader, "TimeoutSec", "10");
    adios2_set_parameter(IOPtrReader, "RendezvousReaderCount", "1");
    adios2_set_parameter(IOPtrReader, "InitialNumReaders", "1");
    adios2_set_parameter(IOPtrReader, "ManagesNetworkStack", "true");
  adios2_set_parameter(IOPtrReader, "StagingDirectory", TCHAR_TO_UTF8(*(WriterFilenamePrefixStr + TEXT(".staging"))));
  adios2_set_parameter(IOPtrReader, "DataDirectory", TCHAR_TO_UTF8(*(WriterFilenamePrefixStr + TEXT(".data"))));
  }

  const char* readerFilenameCStr = TCHAR_TO_UTF8(*WriterFilenamePrefixStr);
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartReader() - opening reader engine with name %hs"), readerFilenameCStr)
  
  ReaderEnginePtr = nullptr;
  
  for (int Retry = 0; Retry < ConnectionRetries; Retry++)
  {
    if (Retry > 0)
    {
      UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartReader() - retry %d/%d after %dms"), Retry, ConnectionRetries, ConnectionRetryDelayMs)
      FPlatformProcess::Sleep((float)ConnectionRetryDelayMs / 1000.0f);
    }
    
    ReaderEnginePtr = adios2_open(IOPtrReader, readerFilenameCStr, adios2_mode_read);
    if (ReaderEnginePtr)
    {
      UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartReader() - adios2_open() succeeded on retry %d"), Retry)
      break;
    }
    else
    {
      UE_LOG(LogTemp, Warning, TEXT("FAdios2State::StartReader() - adios2_open() returned NULL, waiting for writer..."))
    }
  }
  
  if (!ReaderEnginePtr)
  {
    UE_LOG(LogTemp, Error, TEXT("FAdios2State::StartReader() - failed to open reader engine after %d retries"), ConnectionRetries)
    return;
  }
  
  bReaderRunning = true;

  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartReader() - completed, engine=%s, filename=%s"), *EngineTypeString, *WriterFilenamePrefixStr)
}

void FAdios2State::StopReader()
{
  if (!bReaderRunning)
    return;

   if (ReaderEnginePtr)
   {
     adios2_error closeResult = adios2_close(ReaderEnginePtr);
     UE_LOG(LogTemp, Log, TEXT("FAdios2State::StopReader() - adios2_close() returned error=%d"), closeResult)
     ReaderEnginePtr = nullptr;
   }

  if (IOPtrReader)
  {
    adios2_bool removeResult = adios2_true;
    adios2_error removeIoResult = adios2_remove_io(&removeResult, AdiosPtr, "AdiosIO_Reader");
    UE_LOG(LogTemp, Log, TEXT("FAdios2State::StopReader() - adios2_remove_io() returned error=%d, result=%d"), removeIoResult, removeResult)
    IOPtrReader = nullptr;
  }

  bReaderRunning = false;

  UE_LOG(LogTemp, Log, TEXT("ADIOS2 reader stopped"))
}

bool FAdios2State::SendData(const TArray<uint8>& Data)
{
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendData() - begin, size=%d"), Data.Num())
  if (!bRunning)
  {
    UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendData() - not running"))
    return false;
  }
   if (!WriterEnginePtr)
   {
     UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendData() - WriterEnginePtr is null"))
     return false;
   }
   if (!IOPtr)
   {
     UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendData() - IOPtr is null"))
     return false;
   }

   const char* variableNameCStr = TCHAR_TO_UTF8(*TEXT("data"));
   UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendData() - calling adios2_begin_step"))

   adios2_step_status status;
   adios2_error beginStepResult = adios2_begin_step(WriterEnginePtr, adios2_step_mode_update, 0.0f, &status);
   UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendData() - adios2_begin_step returned error=%d, status=%d"), beginStepResult, status)
   if (beginStepResult != adios2_error_none)
   {
     UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendData() - adios2_begin_step failed"))
     return false;
   }
   UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendData() - adios2_begin_step succeeded, calling adios2_put_by_name"))

   adios2_error putResult = adios2_put_by_name(WriterEnginePtr, variableNameCStr, Data.GetData(), adios2_mode_sync);
   UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendData() - adios2_put_by_name returned error=%d"), putResult)
   if (putResult != adios2_error_none)
   {
     UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendData() - adios2_put_by_name failed"))
     adios2_end_step(WriterEnginePtr);
     return false;
   }
   UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendData() - adios2_put_by_name succeeded, calling adios2_end_step"))

   adios2_error endStepResult = adios2_end_step(WriterEnginePtr);
   UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendData() - adios2_end_step returned error=%d"), endStepResult)
   if (endStepResult != adios2_error_none)
   {
     UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendData() - adios2_end_step failed"))
     return false;
   }

   adios2_error flushResult = adios2_flush(WriterEnginePtr);
   UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendData() - adios2_flush returned error=%d"), flushResult)

  UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendData() - completed successfully"))
  return true;
}

bool FAdios2State::SendString(const FString& Message)
{
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendString() - begin, message=%s"), *Message)
  if (!bRunning)
  {
    UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendString() - not running"))
    return false;
  }
   if (!WriterEnginePtr)
   {
     UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendString() - WriterEnginePtr is null"))
     return false;
   }
   if (!IOPtr)
   {
     UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendString() - IOPtr is null"))
     return false;
   }

   const char* variableNameCStr = TCHAR_TO_UTF8(*TEXT("string"));
   const char* messageCStr = TCHAR_TO_UTF8(*Message);

   adios2_step_status status;
   if (adios2_begin_step(WriterEnginePtr, adios2_step_mode_update, 0.0f, &status) != adios2_error_none)
   {
     UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendString() - adios2_begin_step failed"))
     return false;
   }

   if (adios2_put_by_name(WriterEnginePtr, variableNameCStr, messageCStr, adios2_mode_sync) != adios2_error_none)
   {
     UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendString() - adios2_put_by_name failed"))
     adios2_end_step(WriterEnginePtr);
     return false;
   }

   if (adios2_end_step(WriterEnginePtr) != adios2_error_none)
   {
     UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendString() - adios2_end_step failed"))
     return false;
   }

  UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendString() - completed successfully"))
  return true;
}

bool FAdios2State::SendJSON(const FString& JSON)
{
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendJSON() - begin, json=%s"), *JSON)
  if (!bRunning)
  {
    UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendJSON() - not running"))
    return false;
  }
   if (!WriterEnginePtr)
   {
     UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendJSON() - WriterEnginePtr is null"))
     return false;
   }
   if (!IOPtr)
   {
     UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendJSON() - IOPtr is null"))
     return false;
   }

   const char* variableNameCStr = TCHAR_TO_UTF8(*TEXT("json"));
   const char* jsonCStr = TCHAR_TO_UTF8(*JSON);

   adios2_step_status status;
   adios2_error beginStepResult = adios2_begin_step(WriterEnginePtr, adios2_step_mode_update, 0.0f, &status);
   UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendJSON() - adios2_begin_step returned error=%d, status=%d"), beginStepResult, status)
   if (beginStepResult != adios2_error_none)
   {
     UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendJSON() - adios2_begin_step failed"))
     return false;
   }

   adios2_error putResult = adios2_put_by_name(WriterEnginePtr, variableNameCStr, jsonCStr, adios2_mode_sync);
   UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendJSON() - adios2_put_by_name returned error=%d"), putResult)
   if (putResult != adios2_error_none)
   {
     UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendJSON() - adios2_put_by_name failed"))
     adios2_end_step(WriterEnginePtr);
     return false;
   }

   adios2_error endStepResult = adios2_end_step(WriterEnginePtr);
   UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendJSON() - adios2_end_step returned error=%d"), endStepResult)
   if (endStepResult != adios2_error_none)
   {
     UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendJSON() - adios2_end_step failed"))
     return false;
   }

   adios2_error flushResult = adios2_flush(WriterEnginePtr);
   UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendJSON() - adios2_flush returned error=%d"), flushResult)

  UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendJSON() - completed successfully"))
  return true;
}
