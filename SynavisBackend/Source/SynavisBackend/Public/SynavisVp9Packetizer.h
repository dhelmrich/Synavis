#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Containers/CircularQueue.h"
#include "RHI.h"

#include "SynavisVp9Packetizer.generated.h"

struct FEncodedVp9Frame
{
    TArray<uint8> Buffer; // moved in, consumed by worker
    uint32 Timestamp90khz;
    uint32 Duration90khz;
};

struct FVp9PacketDesc
{
    uint8* Data;
    uint16 Len;
};

UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class SYNAVISBACKEND_API USynavisVp9Packetizer : public UObject
{
    GENERATED_BODY()

public:
    USynavisVp9Packetizer();

    // Internal initialize (not Blueprint-exposed due to non-Blueprint-safe integer types)
    void Initialize(int32 InPayloadType, uint32 InSSRC, uint16 InMaxPayloadSize);

    // Move-only API: steal the provided buffer (rvalue). Not Blueprint-exposed.
    void SendFrame(TArray<uint8>&& InFrameBuffer, uint32 FrameTimestamp90khz, 
                   uint32 FrameDuration90khz, const TArray<int32>& TracksToSend);

    UFUNCTION(BlueprintCallable, Category="Synavis|VP9")
    void Shutdown();

private:
    int32 PayloadType = 98;
    uint32 SSRC = 12345678U;
    uint16 MaxPayloadSize = 1200;
    TArray<int32> CurrentTracksToSend;

    static constexpr int32 MaxPacketsPerFrame = 32;
    static constexpr int32 PacketBufSize = 1400;
    TArray<uint8> PacketBuffers;
    TArray<FVp9PacketDesc> PacketPool;

    TCircularQueue<FEncodedVp9Frame>* FrameQueue = nullptr;
    class FVp9PacketizerWorker* Worker = nullptr;
    class FRunnableThread* WorkerThread = nullptr;
    TAtomic<bool> bShouldExit{false};
    uint16 SequenceNumber = 0;
    uint8 PictureId = 0;

    void ProcessFrame(const FEncodedVp9Frame& Frame);

    friend class FVp9PacketizerWorker;
};

class FVp9PacketizerWorker : public FRunnable
{
public:
    FVp9PacketizerWorker(USynavisVp9Packetizer* InOwner);
    virtual uint32 Run() override;
    virtual void Stop() override;
private:
    USynavisVp9Packetizer* Owner;
};
