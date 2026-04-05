#include "Adios2State.h"
#include <adios2_c.h>

FAdios2State::FAdios2State()
  : AdiosPtr(nullptr)
  , IOPtr(nullptr)
  , EnginePtr(nullptr)
  , EngineTypeStr(TEXT("sst"))
  , FilenamePrefixStr(TEXT("synavis_output"))
  , bRunning(false)
  , bInitialized(false)
  , Port(9001)
  , Hostname(TEXT("localhost"))
{
}

FAdios2State::~FAdios2State()
{
  StopStreaming();
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

  EngineTypeStr = EngineType;
  FilenamePrefixStr = FilenamePrefix;

  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - declaring IO"))
  IOPtr = adios2_declare_io(AdiosPtr, "AdiosIO");
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - adios2_declare_io() returned IOPtr=%p"), IOPtr)

  const char* engineTypeCStr = TCHAR_TO_UTF8(*EngineTypeStr);
  const char* filenamePrefixCStr = TCHAR_TO_UTF8(*FilenamePrefixStr);

  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - setting engine type to %hs"), engineTypeCStr)
  adios2_error engineResult = adios2_set_engine(IOPtr, engineTypeCStr);
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - adios2_set_engine() returned error=%d"), engineResult)

  if (EngineTypeStr == TEXT("sst"))
  {
    const char* hostnameCStr = TCHAR_TO_UTF8(*Hostname);
    UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - setting NetworkInterface=%hs, Port=%d"), hostnameCStr, Port)
    adios2_set_parameter(IOPtr, "NetworkInterface", hostnameCStr);
    
    char portStr[16];
    sprintf_s(portStr, "%d", Port);
    adios2_error portResult = adios2_set_parameter(IOPtr, "Port", portStr);
    UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - adios2_set_parameter(Port) returned error=%d"), portResult)
  }

  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - defining variables"))
  adios2_variable* dataVar = adios2_define_variable(IOPtr, "data", adios2_type_uint8_t, 0, NULL, NULL, NULL, adios2_constant_dims_false);
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - adios2_define_variable(data) returned var=%p"), dataVar)
  adios2_variable* stringVar = adios2_define_variable(IOPtr, "string", adios2_type_string, 0, NULL, NULL, NULL, adios2_constant_dims_false);
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - adios2_define_variable(string) returned var=%p"), stringVar)
  adios2_variable* jsonVar = adios2_define_variable(IOPtr, "json", adios2_type_string, 0, NULL, NULL, NULL, adios2_constant_dims_false);
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - adios2_define_variable(json) returned var=%p"), jsonVar)

  const char* engineNameCStr = TCHAR_TO_UTF8(*FilenamePrefixStr);
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - opening engine with name %hs"), engineNameCStr)
  EnginePtr = adios2_open(IOPtr, engineNameCStr, adios2_mode_write);
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - adios2_open() returned EnginePtr=%p"), EnginePtr)
  bRunning = true;

  UE_LOG(LogTemp, Log, TEXT("FAdios2State::StartStreaming() - completed, engine=%s, prefix=%s, hostname=%s, port=%d"), 
         *EngineTypeStr, *FilenamePrefixStr, *Hostname, Port)
}

void FAdios2State::StopStreaming()
{
  if (!bRunning)
    return;

  if (EnginePtr)
  {
    adios2_error closeResult = adios2_close(EnginePtr);
    UE_LOG(LogTemp, Log, TEXT("FAdios2State::StopStreaming() - adios2_close() returned error=%d"), closeResult)
    EnginePtr = nullptr;
  }

  if (IOPtr)
  {
    adios2_bool removeResult = adios2_true;
    adios2_error removeIoResult = adios2_remove_io(&removeResult, AdiosPtr, "AdiosIO");
    UE_LOG(LogTemp, Log, TEXT("FAdios2State::StopStreaming() - adios2_remove_io() returned error=%d, result=%d"), removeIoResult, removeResult)
    IOPtr = nullptr;
  }

  bRunning = false;

  UE_LOG(LogTemp, Log, TEXT("ADIOS2 streaming stopped"))
}

bool FAdios2State::SendData(const TArray<uint8>& Data)
{
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendData() - begin, size=%d"), Data.Num())
  if (!bRunning)
  {
    UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendData() - not running"))
    return false;
  }
  if (!EnginePtr)
  {
    UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendData() - EnginePtr is null"))
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
  adios2_error beginStepResult = adios2_begin_step(EnginePtr, adios2_step_mode_update, 0.0f, &status);
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendData() - adios2_begin_step returned error=%d, status=%d"), beginStepResult, status)
  if (beginStepResult != adios2_error_none)
  {
    UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendData() - adios2_begin_step failed"))
    return false;
  }
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendData() - adios2_begin_step succeeded, calling adios2_put_by_name"))

  adios2_error putResult = adios2_put_by_name(EnginePtr, variableNameCStr, Data.GetData(), adios2_mode_sync);
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendData() - adios2_put_by_name returned error=%d"), putResult)
  if (putResult != adios2_error_none)
  {
    UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendData() - adios2_put_by_name failed"))
    adios2_end_step(EnginePtr);
    return false;
  }
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendData() - adios2_put_by_name succeeded, calling adios2_end_step"))

  adios2_error endStepResult = adios2_end_step(EnginePtr);
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendData() - adios2_end_step returned error=%d"), endStepResult)
  if (endStepResult != adios2_error_none)
  {
    UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendData() - adios2_end_step failed"))
    return false;
  }

  adios2_error flushResult = adios2_flush(EnginePtr);
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
  if (!EnginePtr)
  {
    UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendString() - EnginePtr is null"))
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
  if (adios2_begin_step(EnginePtr, adios2_step_mode_update, 0.0f, &status) != adios2_error_none)
  {
    UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendString() - adios2_begin_step failed"))
    return false;
  }

  if (adios2_put_by_name(EnginePtr, variableNameCStr, messageCStr, adios2_mode_sync) != adios2_error_none)
  {
    UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendString() - adios2_put_by_name failed"))
    adios2_end_step(EnginePtr);
    return false;
  }

  if (adios2_end_step(EnginePtr) != adios2_error_none)
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
  if (!EnginePtr)
  {
    UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendJSON() - EnginePtr is null"))
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
  adios2_error beginStepResult = adios2_begin_step(EnginePtr, adios2_step_mode_update, 0.0f, &status);
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendJSON() - adios2_begin_step returned error=%d, status=%d"), beginStepResult, status)
  if (beginStepResult != adios2_error_none)
  {
    UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendJSON() - adios2_begin_step failed"))
    return false;
  }

  adios2_error putResult = adios2_put_by_name(EnginePtr, variableNameCStr, jsonCStr, adios2_mode_sync);
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendJSON() - adios2_put_by_name returned error=%d"), putResult)
  if (putResult != adios2_error_none)
  {
    UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendJSON() - adios2_put_by_name failed"))
    adios2_end_step(EnginePtr);
    return false;
  }

  adios2_error endStepResult = adios2_end_step(EnginePtr);
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendJSON() - adios2_end_step returned error=%d"), endStepResult)
  if (endStepResult != adios2_error_none)
  {
    UE_LOG(LogTemp, Warning, TEXT("FAdios2State::SendJSON() - adios2_end_step failed"))
    return false;
  }

  adios2_error flushResult = adios2_flush(EnginePtr);
  UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendJSON() - adios2_flush returned error=%d"), flushResult)

  UE_LOG(LogTemp, Log, TEXT("FAdios2State::SendJSON() - completed successfully"))
  return true;
}
