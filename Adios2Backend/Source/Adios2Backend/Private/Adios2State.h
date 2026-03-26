#pragma once
#include "CoreMinimal.h"

// Forward declare ADIOS2 types
namespace adios2
{
class ADIOS;
class IO;
class Engine;
}

// Forward declare Synavis AdiosConnector
namespace Synavis
{
class AdiosConnector;
}

// Internal state for ADIOS2 streaming
class FAdios2State
{
public:
  FAdios2State();
  ~FAdios2State();

  // Initialize ADIOS2
  void Initialize();

  // Start/stop streaming
  void StartStreaming(const FString& EngineType, const FString& FilenamePrefix);
  void StopStreaming();

  // Check if running
  bool IsRunning() const { return bRunning; }

  // Send data via ADIOS2
  bool SendData(const TArray<uint8>& Data);
  bool SendString(const FString& Message);
  bool SendJSON(const FString& JSON);

  // Set configuration
  void SetEngineType(const FString& EngineType) { EngineTypeStr = EngineType; }
  void SetFilenamePrefix(const FString& FilenamePrefix) { FilenamePrefixStr = FilenamePrefix; }

private:
  // ADIOS2 pointers (using PIMPL pattern to avoid including adios2.h here)
  void* AdiosPtr;
  void* IOPtr;
  void* EnginePtr;

  // Configuration
  FString EngineTypeStr;
  FString FilenamePrefixStr;

  // State
  bool bRunning;
  bool bInitialized;
};
