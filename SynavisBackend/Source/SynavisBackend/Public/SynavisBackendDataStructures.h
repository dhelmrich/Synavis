#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "SynavisBackendDataStructures.generated.h"

UENUM(BlueprintType)
enum class ESynavisBackendState : uint8
{
  Offline     UMETA(DisplayName = "Offline"),
  Connecting  UMETA(DisplayName = "Connecting"),
  Connected   UMETA(DisplayName = "Connected"),
  Failure     UMETA(DisplayName = "Failure"),
};

UENUM(BlueprintType)
enum class EBackendSourcePolicy : uint8
{
  RemainStatic      UMETA(DisplayName = "Static: Do not accept connection updates"),
  DynamicOptional   UMETA(DisplayName = "Dynamic Optional: Attempt renegotiation but ignore failures"),
  DynamicMandatory  UMETA(DisplayName = "Dynamic Mandatory: Force renegotiation on updates"),
};

DECLARE_DYNAMIC_DELEGATE_OneParam(FBackendDataHandler, const TArray<uint8>&, Data);
DECLARE_DYNAMIC_DELEGATE_OneParam(FBackendMessageHandler, FString, Message);

USTRUCT(BlueprintType)
struct FBackendHandler
{
  GENERATED_BODY()

  USceneCaptureComponent2D* Video = nullptr;
  bool WantsDedicatedChannel = false;
  bool AcceptsInboundMessages = true;

  TMap<int32, int32> VideoTracksByConnection;

  int MediaDesc = 0;

  FBackendDataHandler DataHandler;
  FBackendMessageHandler MsgHandler;

  uint32 HandlerID = 0;

  friend FORCEINLINE uint32 GetTypeHash(const FBackendHandler& H)
  {
    return H.HandlerID;
  }

  friend FORCEINLINE bool operator==(const FBackendHandler& A, const FBackendHandler& B)
  {
    return A.HandlerID == B.HandlerID;
  }
};

USTRUCT(BlueprintType)
struct FBackendConnection
{
  GENERATED_BODY()

  int PeerConnection = 0;
  int Packetizer = 0;
  int DataChannel = 0;

  uint32 MaxMessageSize = 0;

  TMap<uint32, int32> TracksByHandler;
  TMap<int32, uint32> HandlersByChannel;

  int ConnectionID = 0;
  bool bStreaming = false;
  bool PendingNegotiation = false;
  ESynavisBackendState State = ESynavisBackendState::Offline;

  TArray<TArray<ANSICHAR>> PersistentTrackUtf8;
};
