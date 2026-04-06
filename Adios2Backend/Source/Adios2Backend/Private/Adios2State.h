#pragma once
#include "CoreMinimal.h"
#include "adios2_c.h"

UENUM(BlueprintType)
enum class EAdiosTransport : uint8
{
    SST,
    BPFile,
    DataServer,
    File,
    Null
};

class FAdios2State
{
public:
  FAdios2State();
  ~FAdios2State();

  void Initialize();
  void StartStreaming(const FString& EngineType, const FString& FilenamePrefix);
  void StopStreaming();
  void StartReader();
  void StopReader();

  bool IsRunning() const { return bRunning; }
  bool IsConnecting() const { return bIsConnecting; }
  bool IsReaderRunning() const { return bReaderRunning; }

  bool SendData(const TArray<uint8>& Data);
  bool SendString(const FString& Message);
  bool SendJSON(const FString& JSON);

   void SetEngineType(const FString& EngineType) { WriterEngineTypeStr = EngineType; }
   void SetFilenamePrefix(const FString& FilenamePrefix) { WriterFilenamePrefixStr = FilenamePrefix; }
  void SetPort(int32 InPort) { Port = InPort; }
  void SetHostname(const FString& InHostname) { Hostname = InHostname; }
  void SetConnectionRetries(int32 InRetries) { ConnectionRetries = InRetries; }
  void SetConnectionRetryDelayMs(int32 InDelayMs) { ConnectionRetryDelayMs = InDelayMs; }

   int32 GetPort() const { return Port; }
  FString GetHostname() const { return Hostname; }
   FString GetEngineType() const { return WriterEngineTypeStr; }

  void SetTransportType(EAdiosTransport InType) { WriterTransportType = InType; }
  EAdiosTransport GetTransportType() const { return WriterTransportType; }

private:
  adios2_adios* AdiosPtr;
  adios2_io* IOPtr;
   adios2_engine* WriterEnginePtr;
  adios2_io* IOPtrReader;
   adios2_engine* ReaderEnginePtr;

   FString WriterEngineTypeStr;
   FString WriterFilenamePrefixStr;

  bool bRunning;
  bool bInitialized;
  bool bIsConnecting;
  bool bReaderRunning;

  int32 Port;
  FString Hostname;

  int32 ConnectionRetries;
  int32 ConnectionRetryDelayMs;

  EAdiosTransport WriterTransportType;

  FString GetTransportTypeString() const
  {
      switch (WriterTransportType)
      {
           case EAdiosTransport::SST: return TEXT("SST");
            case EAdiosTransport::BPFile: return TEXT("BP5");
           case EAdiosTransport::DataServer: return TEXT("DataServer");
           case EAdiosTransport::File: return TEXT("File");
           case EAdiosTransport::Null: return TEXT("Null");
            default: return TEXT("BP5");
      }
  }
};
