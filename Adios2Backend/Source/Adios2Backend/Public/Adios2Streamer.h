#pragma once

#include "CoreMinimal.h"
#include "SynavisCommunicationInterface.h"
#include "Adios2Streamer.generated.h"

// Forward declare ADIOS2 types to avoid dependency in public header
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

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class ADIOS2BACKEND_API UAdios2Streamer : public USynavisCommunicationInterface
{
  GENERATED_BODY()

public:
  UAdios2Streamer();
  virtual ~UAdios2Streamer() override;

  // Implement basic lifecycle operations
  virtual void StartStreaming() override;
  virtual void StopStreaming() override;

  // Data source registration - direct callback registration without handler IDs
  virtual int32 RegisterDataSourceCpp(
    const std::function<void(const TArray<uint8>&)>& OnData,
    const std::function<void(const FString&)>& OnMessage,
    USceneCaptureComponent2D* SceneCapture = nullptr,
    bool DedicatedChannel = false,
    bool AcceptsInboundMessages = true) override;

  virtual bool SendBinaryToConnection(int32 HandlerId, int32 ConnectionPlayerID, const TArray<uint8>& Data) override;

  // Callback registration for receiving data (bidirectional support)
  void SetDataCallback(const std::function<void(const TArray<uint8>&)>& Callback);
  void SetMessageCallback(const std::function<void(const FString&)>& Callback);

  // Configuration methods
  UFUNCTION(BlueprintCallable, Category="ADIOS2|Configuration")
  void SetEngineType(const FString& EngineType);
  
  UFUNCTION(BlueprintCallable, Category="ADIOS2|Configuration")
  void SetFilenamePrefix(const FString& FilenamePrefix);

protected:
  // ADIOS2 state
  TSharedPtr<class FAdios2State> AdiosState;

  // Callbacks for receiving data (bidirectional support)
  std::function<void(const TArray<uint8>&)> DataCallback;
  std::function<void(const FString&)> MessageCallback;
};
