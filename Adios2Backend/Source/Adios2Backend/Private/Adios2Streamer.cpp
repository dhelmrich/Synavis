#include "Adios2Streamer.h"
#include "Adios2State.h"
#include "Engine/World.h"
#include "Misc/OutputDeviceNull.h"

UAdios2Streamer::UAdios2Streamer()
{
  AdiosState = MakeShared<FAdios2State>();
}

UAdios2Streamer::~UAdios2Streamer()
{
  StopStreaming();
}

void UAdios2Streamer::StartStreaming()
{
  if (AdiosState && !AdiosState->IsRunning())
  {
    // Default configuration - now uses SST for cluster/localhost streaming
    FString EngineType = TEXT("sst");
    FString FilenamePrefix = TEXT("synavis_output");
    
    AdiosState->Initialize();
    AdiosState->StartStreaming(EngineType, FilenamePrefix);
    
    UE_LOG(LogTemp, Log, TEXT("UAdios2Streamer: StartStreaming - ADIOS2 initialized with engine=%s, prefix=%s, hostname=%s, port=%d"), 
           *EngineType, *FilenamePrefix, *AdiosState->GetHostname(), AdiosState->GetPort());
  }
}

void UAdios2Streamer::StopStreaming()
{
  if (AdiosState && AdiosState->IsRunning())
  {
    AdiosState->StopStreaming();
    UE_LOG(LogTemp, Log, TEXT("UAdios2Streamer: StopStreaming - ADIOS2 stopped"));
  }
}

int32 UAdios2Streamer::RegisterDataSourceCpp(
  const std::function<void(int32, const TArray<uint8>&)>& OnData,
  const std::function<void(int32, const FString&)>& OnMessage,
  USceneCaptureComponent2D* SceneCapture,
  bool DedicatedChannel,
  bool AcceptsInboundMessages)
{
  UE_LOG(LogTemp, Log, TEXT("UAdios2Streamer: Data source registered with direct callbacks"))

  static int32 NextHandler = 10000;
  int32 Assigned = NextHandler++;
  
  return Assigned;
}

bool UAdios2Streamer::SendBinaryToConnection(int32 HandlerId, int32 ConnectionPlayerID, const TArray<uint8>& Data)
{
  if (AdiosState && AdiosState->IsRunning())
  {
    bool bSuccess = AdiosState->SendData(Data);
    
    UE_LOG(LogTemp, Verbose, TEXT("UAdios2Streamer: SendBinaryToConnection handler=%d player=%d size=%d success=%d"), 
           HandlerId, ConnectionPlayerID, Data.Num(), bSuccess ? 1 : 0);
    
    return bSuccess;
  }
  
  UE_LOG(LogTemp, Warning, TEXT("UAdios2Streamer: SendBinaryToConnection called but not running"));
  return false;
}

void UAdios2Streamer::SetEngineType(const FString& EngineType)
{
  if (AdiosState)
  {
    AdiosState->SetEngineType(EngineType);
    UE_LOG(LogTemp, Log, TEXT("UAdios2Streamer: Engine type set to %s"), *EngineType);
  }
}

void UAdios2Streamer::SetFilenamePrefix(const FString& FilenamePrefix)
{
  if (AdiosState)
  {
    AdiosState->SetFilenamePrefix(FilenamePrefix);
    UE_LOG(LogTemp, Log, TEXT("UAdios2Streamer: Filename prefix set to %s"), *FilenamePrefix);
  }
}

void UAdios2Streamer::SetPort(int32 Port)
{
  if (AdiosState)
  {
    AdiosState->SetPort(Port);
    UE_LOG(LogTemp, Log, TEXT("UAdios2Streamer: Port set to %d"), Port);
  }
}

void UAdios2Streamer::SetHostname(const FString& Hostname)
{
  if (AdiosState)
  {
    AdiosState->SetHostname(Hostname);
    UE_LOG(LogTemp, Log, TEXT("UAdios2Streamer: Hostname set to %s"), *Hostname);
  }
}

FString UAdios2Streamer::GetSignalingConfig() const
{
  if (AdiosState)
  {
    FString Config = FString::Printf(
      TEXT("{\"type\":\"adios2_config\",\"engine\":\"%s\",\"hostname\":\"%s\",\"port\":%d,\"mode\":\"writer\"}"),
      *AdiosState->GetEngineType(),
      *AdiosState->GetHostname(),
      AdiosState->GetPort()
    );
    return Config;
  }
  return TEXT("{}");
}

void UAdios2Streamer::SetDataCallback(const std::function<void(int32, const TArray<uint8>&)>& Callback)
{
  DataCallback = Callback;
  UE_LOG(LogTemp, Log, TEXT("UAdios2Streamer: Data callback set"))
}

void UAdios2Streamer::SetMessageCallback(const std::function<void(int32, const FString&)>& Callback)
{
  MessageCallback = Callback;
  UE_LOG(LogTemp, Log, TEXT("UAdios2Streamer: Message callback set"))
}
