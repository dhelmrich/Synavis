#include "Adios2Streamer.h"
#include "Adios2State.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Async/Async.h"
#include "Misc/OutputDeviceNull.h"

UAdios2Streamer::UAdios2Streamer()
{
  AdiosState = MakeShared<FAdios2State>();
  PrimaryComponentTick.bCanEverTick = true;
}

UAdios2Streamer::~UAdios2Streamer()
{
  StopStreaming();
}

void UAdios2Streamer::StartStreaming()
{
  if (AdiosState && !AdiosState->IsRunning())
  {
    // Default configuration - now uses BPFile for cluster/localhost streaming
    FString EngineType = TEXT("BP5");
    FString FilenamePrefix = TEXT("synavis_output");
    
    AdiosState->Initialize();
    AdiosState->SetConnectionRetries(ConnectionRetries);
    AdiosState->SetConnectionRetryDelayMs(ConnectionRetryDelayMs);
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
  USceneCaptureComponent2D* InSceneCapture,
  bool DedicatedChannel,
  bool AcceptsInboundMessages)
{
  UE_LOG(LogTemp, Log, TEXT("UAdios2Streamer: Data source registered with direct callbacks"))

  static int32 NextHandler = 10000;
  int32 Assigned = NextHandler++;
  
  this->SceneCapture = InSceneCapture;
  bNeedsRenderThreadSync = true;
  
  UE_LOG(LogTemp, Log, TEXT("UAdios2Streamer: SceneCapture stored for handler=%d"), Assigned);

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

void UAdios2Streamer::SetConnectionRetries(int32 InRetries)
{
  if (AdiosState)
  {
    AdiosState->SetConnectionRetries(InRetries);
    UE_LOG(LogTemp, Log, TEXT("UAdios2Streamer: Connection retries set to %d"), InRetries);
  }
}

void UAdios2Streamer::SetConnectionRetryDelayMs(int32 InDelayMs)
{
  if (AdiosState)
  {
    AdiosState->SetConnectionRetryDelayMs(InDelayMs);
    UE_LOG(LogTemp, Log, TEXT("UAdios2Streamer: Connection retry delay set to %dms"), InDelayMs);
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

void UAdios2Streamer::CaptureFrame()
{
  if (!SceneCapture || !SceneCapture->TextureTarget)
  {
    UE_LOG(LogTemp, Verbose, TEXT("UAdios2Streamer: No valid SceneCapture or TextureTarget to capture from"))
    return;
  }

  UTextureRenderTarget2D* RenderTarget = SceneCapture->TextureTarget;
  if (!RenderTarget)
  {
    UE_LOG(LogTemp, Warning, TEXT("UAdios2Streamer: RenderTarget is null"))
    return;
  }

  UE_LOG(LogTemp, Verbose, TEXT("UAdios2Streamer: Capturing frame from SceneCapture (Width=%d Height=%d)"), RenderTarget->SizeX, RenderTarget->SizeY);

  TPromise<TArray<FColor>> Promise;
  TFuture<TArray<FColor>> Future = Promise.GetFuture();

  AsyncTask(ENamedThreads::GameThread, [WeakRenderTarget = TWeakObjectPtr<UTextureRenderTarget2D>(RenderTarget), Promise = MoveTemp(Promise)]() mutable {
    TArray<FColor> PixelData;

    UTextureRenderTarget2D* RT = WeakRenderTarget.Get();
    if (!RT)
    {
      UE_LOG(LogTemp, Warning, TEXT("UAdios2Streamer: RGB readback skipped because render target is no longer valid"));
      Promise.SetValue(MoveTemp(PixelData));
      return;
    }

    FTextureRenderTargetResource* RTResource = RT->GameThread_GetRenderTargetResource();
    if (!RTResource)
    {
      UE_LOG(LogTemp, Warning, TEXT("UAdios2Streamer: Failed to get render target resource for RGB readback"));
      Promise.SetValue(MoveTemp(PixelData));
      return;
    }

    FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
    ReadFlags.SetLinearToGamma(false);

    if (!RTResource->ReadPixels(PixelData, ReadFlags))
    {
      UE_LOG(LogTemp, Warning, TEXT("UAdios2Streamer: RGB readback failed while reading pixels"));
      PixelData.Reset();
    }

    Promise.SetValue(MoveTemp(PixelData));
  });

  TArray<FColor> PixelColors = Future.Get();
  
  if (PixelColors.Num() == 0)
  {
    UE_LOG(LogTemp, Warning, TEXT("UAdios2Streamer: No pixel data captured"))
    return;
  }

  TArray<uint8> FrameData;
  FrameData.SetNum(PixelColors.Num() * 4);
  
  for (int32 i = 0; i < PixelColors.Num(); ++i)
  {
    FrameData[i * 4 + 0] = PixelColors[i].R;
    FrameData[i * 4 + 1] = PixelColors[i].G;
    FrameData[i * 4 + 2] = PixelColors[i].B;
    FrameData[i * 4 + 3] = PixelColors[i].A;
  }

  UE_LOG(LogTemp, Log, TEXT("UAdios2Streamer: Frame captured, size=%d bytes"), FrameData.Num());

  SendBinaryToConnection(10000, 0, FrameData);
}

void UAdios2Streamer::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
  
  if (!AdiosState || !SceneCapture)
  {
    UE_LOG(LogTemp, Warning, TEXT("UAdios2Streamer: Tick skipped because not running or no SceneCapture"));
    return;
  }
  
  if (!AdiosState->IsRunning() && !AdiosState->IsConnecting())
  {
    UE_LOG(LogTemp, Warning, TEXT("UAdios2Streamer: Tick skipped because not running or no SceneCapture"));
    return;
  }
  
  CaptureFrame();
}


