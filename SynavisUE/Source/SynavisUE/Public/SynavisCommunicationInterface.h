// SynavisCommunicationInterface.h
// Abstract communication interface for SynavisUE to decouple UE plugin from backends.
//
// Purpose:
// - Provide a stable interface that SynavisUE code (e.g., SynavisDrone) can call
//   without depending on a specific backend implementation such as USynavisStreamer.
// - Contain only declarations and documentation mapping existing `USynavisStreamer`
//   methods that SynavisUE currently invokes. No backend implementation is included.
//
// Migration notes (what to change in SynavisUE to use this interface):
// - Change any pointers of type `USynavisStreamer*` to `USynavisCommunicationInterface*`.
// - Include this header instead of SynavisStreamer.h in SynavisUE code.
// - Backends (e.g., the existing `USynavisStreamer` or a future `UAdios2Backend`)
//   should subclass `USynavisCommunicationInterface` and implement the virtual methods.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include <functional>
#include "SynavisCommunicationInterface.generated.h"

class USceneCaptureComponent2D;

UCLASS(Abstract, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class SYNAVISUE_API USynavisCommunicationInterface : public UActorComponent
{
  GENERATED_BODY()

public:
  USynavisCommunicationInterface() {}
  virtual ~USynavisCommunicationInterface() override {}

  // Lifecycle: start/stop global streaming (may be no-op for some backends)
  virtual void StartStreaming() { ensureMsgf(false, TEXT("StartStreaming not implemented")); }
  virtual void StopStreaming() { ensureMsgf(false, TEXT("StopStreaming not implemented")); }

  // Signalling / negotiation helpers used by SynavisUE
  virtual void StartSignalling() { ensureMsgf(false, TEXT("StartSignalling not implemented")); }
  virtual void StartConnectionNegotiation() { ensureMsgf(false, TEXT("StartConnectionNegotiation not implemented")); }

  // Data source registration API. Mirrors USynavisStreamer surface used by SynavisUE.
  // Implementations should return a stable handler id (positive) or 0 on failure.
  UFUNCTION(BlueprintCallable, Category = "Streaming|Data")
  virtual int RegisterDataSource(
    const TArray<uint8>& DataHandler,
    const FString& MsgHandler,
    USceneCaptureComponent2D* SceneCapture = nullptr,
    bool DedicatedChannel = false,
    bool AcceptsInboundMessages = true)
  {
    ensureMsgf(false, TEXT("RegisterDataSource (Blueprint) is not implemented in this interface"));
    return 0;
  }

  // C++ registration APIs used by SynavisDrone.cpp and other C++ callers
  // Callbacks receive the Connection ID as first parameter for multi-connection support
  virtual int32 RegisterDataSourceCpp(
    const std::function<void(int32, const TArray<uint8>&)>& OnData,
    const std::function<void(int32, const FString&)>& OnMessage,
    USceneCaptureComponent2D* SceneCapture = nullptr,
    bool DedicatedChannel = false,
    bool AcceptsInboundMessages = true)
  {
    ensureMsgf(false, TEXT("RegisterDataSourceCpp not implemented"));
    return 0;
  }

  virtual int32 RegisterVideoSourceCpp(USceneCaptureComponent2D* SceneCapture, bool DedicatedChannel = false, bool AcceptsInboundMessages = false)
  {
    ensureMsgf(false, TEXT("RegisterVideoSourceCpp not implemented"));
    return 0;
  }

  UFUNCTION(BlueprintCallable, Category = "Streaming|Data")
  virtual int RegisterVideoSource(USceneCaptureComponent2D* SceneCapture, bool DedicatedChannel = false, bool AcceptsInboundMessages = false)
  {
    ensureMsgf(false, TEXT("RegisterVideoSource (Blueprint) not implemented"));
    return 0;
  }

  virtual void UnregisterDataSource(int32 HandlerId) { ensureMsgf(false, TEXT("UnregisterDataSource not implemented")); }

  // Send data/text to a connection via the handler's channel. Return success.
  virtual bool SendTextToConnection(int32 HandlerId, int32 ConnectionPlayerID, const FString& Text) { ensureMsgf(false, TEXT("SendTextToConnection not implemented")); return false; }
  virtual bool SendBinaryToConnection(int32 HandlerId, int32 ConnectionPlayerID, const TArray<uint8>& Data) { ensureMsgf(false, TEXT("SendBinaryToConnection not implemented")); return false; }

  virtual bool BroadcastText(int32 HandlerId, const FString& Text) { ensureMsgf(false, TEXT("BroadcastText not implemented")); return false; }
  virtual bool BroadcastBinary(int32 HandlerId, const TArray<uint8>& Data) { ensureMsgf(false, TEXT("BroadcastBinary not implemented")); return false; }

  virtual bool SendTextViaSystemChannel(const FString& Text) { ensureMsgf(false, TEXT("SendTextViaSystemChannel not implemented")); return false; }
  virtual bool SendBinaryViaSystemChannel(const TArray<uint8>& Data) { ensureMsgf(false, TEXT("SendBinaryViaSystemChannel not implemented")); return false; }

  // Low-level frame send (raw/encoded bytes) - used by video path in SynavisUE
  virtual void SendFrameBytes(const TArray<uint8>& Bytes, const FString& Name, const FString& Format, int32 TargetTrackId) { ensureMsgf(false, TEXT("SendFrameBytes not implemented")); }

protected:
  // Add any protected helpers or documented extension points here.
};
