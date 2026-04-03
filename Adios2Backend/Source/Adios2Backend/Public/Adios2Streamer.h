#pragma once

#include "CoreMinimal.h"
#include "SynavisCommunicationInterface.h"
#include "Adios2Streamer.generated.h"



UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class ADIOS2BACKEND_API UAdios2Streamer : public USynavisCommunicationInterface
{
  GENERATED_BODY()

public:
  UAdios2Streamer();
  virtual ~UAdios2Streamer() override;

  UFUNCTION(BlueprintCallable, Category="ADIOS2|Configuration")
  virtual void StartStreaming() override;
  UFUNCTION(BlueprintCallable, Category="ADIOS2|Configuration")
  virtual void StopStreaming() override;

  virtual int32 RegisterDataSourceCpp(
    const std::function<void(int32, const TArray<uint8>&)>& OnData,
    const std::function<void(int32, const FString&)>& OnMessage,
    USceneCaptureComponent2D* SceneCapture = nullptr,
    bool DedicatedChannel = false,
    bool AcceptsInboundMessages = true) override;

  virtual bool SendBinaryToConnection(int32 HandlerId, int32 ConnectionPlayerID, const TArray<uint8>& Data) override;

  void SetDataCallback(const std::function<void(int32, const TArray<uint8>&)>& Callback);
  void SetMessageCallback(const std::function<void(int32, const FString&)>& Callback);

  UFUNCTION(BlueprintCallable, Category="ADIOS2|Configuration")
  void SetEngineType(const FString& EngineType);
  
  UFUNCTION(BlueprintCallable, Category="ADIOS2|Configuration")
  void SetFilenamePrefix(const FString& FilenamePrefix);

  UFUNCTION(BlueprintCallable, Category="ADIOS2|Network")
  void SetPort(int32 Port);
  
  UFUNCTION(BlueprintCallable, Category="ADIOS2|Network")
  void SetHostname(const FString& Hostname);

  UFUNCTION(BlueprintCallable, Category="ADIOS2|Signaling")
  FString GetSignalingConfig() const;

protected:
  TSharedPtr<class FAdios2State> AdiosState;

  std::function<void(int32, const TArray<uint8>&)> DataCallback;
  std::function<void(int32, const FString&)> MessageCallback;
};
