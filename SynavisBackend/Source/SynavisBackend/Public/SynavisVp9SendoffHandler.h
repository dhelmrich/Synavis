#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Containers/CircularQueue.h"
#include "RHI.h"

// Forward-declare libav types to avoid pulling third-party headers into a public
// header (include libav headers in the .cpp where available).
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct AVCodec;
struct AVRational;


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

#include "SynavisVp9SendoffHandler.generated.h"

struct FLibAVEncoderState
{
    AVCodecContext* CodecCtx = nullptr;
    AVFrame* Frame = nullptr;
    AVPacket* Packet = nullptr;
    const AVCodec* Codec = nullptr;
    int Width = 0;
    int Height = 0;
    FCriticalSection Mutex;
    // Timestamping state: keep a small, local RTP timestamp generator so
    // we don't depend on calling into libav just to rescale timestamps.
    // This uses codec time_base (set when codec is opened) to compute
    // a multiplier mapping packet PTS -> RTP 90kHz clock.
    uint64_t FrameCounter = 0;
    uint32_t LastRtpTs = 0;
    int64_t LastPacketPts = 0;
    bool bHasLastPacketPts = false;
    double RtpMultiplier = 0.0; // multiplier: RTP90kHz = pts * RtpMultiplier

    FLibAVEncoderState() {}
    ~FLibAVEncoderState();
};

UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class SYNAVISBACKEND_API USynavisVp9SendoffHandler : public UObject
{
    GENERATED_BODY()

public:
    USynavisVp9SendoffHandler();

    // Internal initialize (not Blueprint-exposed due to non-Blueprint-safe integer types)
    void Initialize(int32 InPayloadType, uint32 InSSRC, uint16 InMaxPayloadSize);

    // Move-only API: steal the provided buffer (rvalue). Not Blueprint-exposed.
    void SendFrame(TArray<uint8>&& InFrameBuffer, uint32 FrameTimestamp90khz,
                   uint32 FrameDuration90khz, const TArray<int32>& TracksToSend);

    UFUNCTION(BlueprintCallable, Category="Synavis|VP9")
    void Shutdown();

    // Return (and lazily create) the internal libav encoder state used by this handler.
    FLibAVEncoderState* GetOrCreateLibAVEncoderState();

    // New non-blocking entry point: enqueue a GPU readback for encode+send.
    // This will offload waiting, locking and encoding to worker threads.
    void EnqueueReadbackNonBlocking(class FRHIGPUTextureReadback* ReadbackY,
                                    class FRHIGPUTextureReadback* ReadbackU,
                                    class FRHIGPUTextureReadback* ReadbackV,
                                    int Width, int Height,
                                    const TArray<int32>& TargetTracks,
                                    FLibAVEncoderState* LibAVState);

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
    class FVp9SendoffWorker* Worker = nullptr;
    class FRunnableThread* WorkerThread = nullptr;
    TAtomic<bool> bShouldExit{false};
    uint16 SequenceNumber = 0;
    uint8 PictureId = 0;

    void ProcessFrame(const FEncodedVp9Frame& Frame);

    friend class FVp9SendoffWorker;

    // Internal encoder state owned by this handler
    FLibAVEncoderState* InternalLibAVState = nullptr;
};

class FVp9SendoffWorker : public FRunnable
{
public:
    FVp9SendoffWorker(USynavisVp9SendoffHandler* InOwner);
    virtual uint32 Run() override;
    virtual void Stop() override;
private:
    USynavisVp9SendoffHandler* Owner;
};
