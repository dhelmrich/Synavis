#pragma once
#include "CoreMinimal.h"
#include "adios2_c.h"

class FAdios2State
{
public:
  FAdios2State();
  ~FAdios2State();

  void Initialize();
  void StartStreaming(const FString& EngineType, const FString& FilenamePrefix);
  void StopStreaming();

  bool IsRunning() const { return bRunning; }

  bool SendData(const TArray<uint8>& Data);
  bool SendString(const FString& Message);
  bool SendJSON(const FString& JSON);

  void SetEngineType(const FString& EngineType) { EngineTypeStr = EngineType; }
  void SetFilenamePrefix(const FString& FilenamePrefix) { FilenamePrefixStr = FilenamePrefix; }
  void SetPort(int32 InPort) { Port = InPort; }
  void SetHostname(const FString& InHostname) { Hostname = InHostname; }

  int32 GetPort() const { return Port; }
  FString GetHostname() const { return Hostname; }
  FString GetEngineType() const { return EngineTypeStr; }

private:
  adios2_adios* AdiosPtr;
  adios2_io* IOPtr;
  adios2_engine* EnginePtr;

  FString EngineTypeStr;
  FString FilenamePrefixStr;

  bool bRunning;
  bool bInitialized;

  int32 Port;
  FString Hostname;
};
