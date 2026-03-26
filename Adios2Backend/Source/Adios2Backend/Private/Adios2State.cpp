#include "Adios2State.h"
#include "Adios2BackendLog.h"
#include <adios2.h>

FAdios2State::FAdios2State()
  : AdiosPtr(nullptr)
  , IOPtr(nullptr)
  , EnginePtr(nullptr)
  , EngineTypeStr(TEXT("sst"))
  , FilenamePrefixStr(TEXT("synavis_output"))
  , bRunning(false)
  , bInitialized(false)
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

  AdiosPtr = new adios2::ADIOS();
  bInitialized = true;

  UE_LOG(LogAdios2Backend, Log, TEXT("ADIOS2 initialized"))
}

void FAdios2State::StartStreaming(const FString& EngineType, const FString& FilenamePrefix)
{
  if (bRunning)
    return;

  if (!bInitialized)
  {
    Initialize();
  }

  EngineTypeStr = EngineType;
  FilenamePrefixStr = FilenamePrefix;

  auto* adios = static_cast<adios2::ADIOS*>(AdiosPtr);

  IOPtr = new adios2::IO(adios->DeclareIO("AdiosIO"));
  auto* io = static_cast<adios2::IO*>(IOPtr);

  auto engineTypeStr = std::string(TCHAR_TO_UTF8(*EngineTypeStr));
  auto filenamePrefixStr = std::string(TCHAR_TO_UTF8(*FilenamePrefixStr));

  adios2::Mode mode = adios2::Mode::Write;

  EnginePtr = new adios2::Engine(io->Open(filenamePrefixStr + "." + engineTypeStr, mode));
  bRunning = true;

  UE_LOG(LogAdios2Backend, Log, TEXT("ADIOS2 streaming started - engine=%s, prefix=%s"), *EngineTypeStr, *FilenamePrefixStr)
}

void FAdios2State::StopStreaming()
{
  if (!bRunning)
    return;

  if (EnginePtr)
  {
    auto* engine = static_cast<adios2::Engine*>(EnginePtr);
    engine->Close();
    delete engine;
    EnginePtr = nullptr;
  }

  if (IOPtr)
  {
    delete static_cast<adios2::IO*>(IOPtr);
    IOPtr = nullptr;
  }

  bRunning = false;

  UE_LOG(LogAdios2Backend, Log, TEXT("ADIOS2 streaming stopped"))
}

bool FAdios2State::SendData(const TArray<uint8>& Data)
{
  if (!bRunning || !EnginePtr || !IOPtr)
    return false;

  auto* io = static_cast<adios2::IO*>(IOPtr);
  auto* engine = static_cast<adios2::Engine*>(EnginePtr);

  static const FString VariableName = TEXT("data");

  if (!engine->BeginStep())
    return false;

  adios2::Variable<uint8_t> variable = io->InquireVariable<uint8_t>(TCHAR_TO_UTF8(*VariableName));
  if (!variable)
  {
    variable = io->DefineVariable<uint8_t>(TCHAR_TO_UTF8(*VariableName));
  }

  std::vector<uint8_t> dataVector(Data.Num());
  std::memcpy(dataVector.data(), Data.GetData(), Data.Num() * sizeof(uint8_t));
  engine->Put(variable, dataVector.data(), adios2::Mode::Sync);

  engine->EndStep();

  return true;
}

bool FAdios2State::SendString(const FString& Message)
{
  if (!bRunning || !EnginePtr || !IOPtr)
    return false;

  auto* io = static_cast<adios2::IO*>(IOPtr);
  auto* engine = static_cast<adios2::Engine*>(EnginePtr);

  static const FString VariableName = TEXT("string");

  if (!engine->BeginStep())
    return false;

  adios2::Variable<std::string> variable = io->InquireVariable<std::string>(TCHAR_TO_UTF8(*VariableName));
  if (!variable)
  {
    variable = io->DefineVariable<std::string>(TCHAR_TO_UTF8(*VariableName));
  }

  std::string messageStr = std::string(TCHAR_TO_UTF8(*Message));
  engine->Put(variable, messageStr.c_str(), adios2::Mode::Sync);

  engine->EndStep();

  return true;
}

bool FAdios2State::SendJSON(const FString& JSON)
{
  if (!bRunning || !EnginePtr || !IOPtr)
    return false;

  auto* io = static_cast<adios2::IO*>(IOPtr);
  auto* engine = static_cast<adios2::Engine*>(EnginePtr);

  static const FString VariableName = TEXT("json");

  if (!engine->BeginStep())
    return false;

  adios2::Variable<std::string> variable = io->InquireVariable<std::string>(TCHAR_TO_UTF8(*VariableName));
  if (!variable)
  {
    variable = io->DefineVariable<std::string>(TCHAR_TO_UTF8(*VariableName));
  }

  std::string jsonStr = std::string(TCHAR_TO_UTF8(*JSON));
  engine->Put(variable, jsonStr.c_str(), adios2::Mode::Sync);

  engine->EndStep();

  return true;
}
