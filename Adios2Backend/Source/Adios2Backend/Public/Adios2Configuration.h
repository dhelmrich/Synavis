// Adios2Configuration.h
// ======================
// ADIOS2 autodiscovery configuration via signaling server
// Maintains backward compatibility with existing WebRTC signalling

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Adios2Configuration.generated.h"

// ADIOS2 transport configuration
USTRUCT(BlueprintType)
struct ADIOS2BACKEND_API FAdios2TransportConfig
{
  GENERATED_BODY()

public:
  // Transport type: "bpfile", "sst", "dataserver", etc.
  UPROPERTY(BlueprintReadWrite, Category="ADIOS2|Transport")
  FString EngineType;

  // Output file prefix
  UPROPERTY(BlueprintReadWrite, Category="ADIOS2|Transport")
  FString FilenamePrefix;

  // Network configuration
  UPROPERTY(BlueprintReadWrite, Category="ADIOS2|Transport")
  FString Hostname;

  UPROPERTY(BlueprintReadWrite, Category="ADIOS2|Transport")
  int32 Port;

  // SST-specific configuration
  UPROPERTY(BlueprintReadWrite, Category="ADIOS2|Transport|SST")
  FString SSTNetworkInterface;

  UPROPERTY(BlueprintReadWrite, Category="ADIOS2|Transport|SST")
  int32 SSTReaderTimeout;

  // Mode: "writer" (UE) or "reader" (external)
  UPROPERTY(BlueprintReadWrite, Category="ADIOS2|Transport")
  FString Mode;

  FAdios2TransportConfig()
    : EngineType(TEXT("sst"))
    , FilenamePrefix(TEXT("synavis_output"))
    , Hostname(TEXT("localhost"))
    , Port(9001)
    , SSTNetworkInterface(TEXT(""))
    , SSTReaderTimeout(30)
    , Mode(TEXT("writer"))
  {}
};

// Signaling message types for ADIOS2 negotiation
USTRUCT(BlueprintType)
struct ADIOS2BACKEND_API FAdios2SignalingMessage
{
  GENERATED_BODY()

public:
  UPROPERTY(BlueprintReadWrite, Category="ADIOS2|Signaling")
  FString Type;

  // Message payload
  UPROPERTY(BlueprintReadWrite, Category="ADIOS2|Signaling")
  FAdios2TransportConfig Config;

  // Connection identifier
  UPROPERTY(BlueprintReadWrite, Category="ADIOS2|Signaling")
  FString ConnectionId;

  // Role identifier
  UPROPERTY(BlueprintReadWrite, Category="ADIOS2|Signaling")
  FString Role;

  FAdios2SignalingMessage()
    : Type(TEXT("adios2_config"))
    , ConnectionId(TEXT(""))
    , Role(TEXT("ue"))
  {}
};
