#include "SynavisVp9SendoffHandler.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformProcess.h"
#include "Logging/LogMacros.h"
#include "Async/Async.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"

THIRD_PARTY_INCLUDES_START
#include "rtc/rtc.h"

extern "C" {

#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/buffer.h>
#include <libavutil/mathematics.h>

}

THIRD_PARTY_INCLUDES_END

// Helper: compute an RTP 90kHz timestamp without requiring av_rescale_q at call
// site. Updates state->RtpMultiplier on first use using codec time_base.
static uint32_t ComputeRtpTimestamp(FLibAVEncoderState* State, int64_t PacketPts, AVRational TimeBase)
{
    if (!State) return static_cast<uint32_t>(FPlatformTime::Seconds() * 90000.0);

    // Ensure multiplier is initialized when we have a valid timebase
    if (State->RtpMultiplier == 0.0 && TimeBase.den != 0)
    {
        State->RtpMultiplier = 90000.0 * (double)TimeBase.num / (double)TimeBase.den;
    }

    if (PacketPts != AV_NOPTS_VALUE)
    {
        if (!State->bHasLastPacketPts)
        {
            // First mapping: anchor RTP to wall-clock and store last PTS
            State->LastPacketPts = PacketPts;
            State->LastRtpTs = static_cast<uint32_t>(FPlatformTime::Seconds() * 90000.0);
            State->bHasLastPacketPts = true;
            return State->LastRtpTs;
        }
        double deltaPts = (double)(PacketPts - State->LastPacketPts);
        uint32_t rtpDelta = 0;
        if (State->RtpMultiplier > 0.0)
        {
            rtpDelta = static_cast<uint32_t>(deltaPts * State->RtpMultiplier);
        }
        uint32_t rtp = State->LastRtpTs + rtpDelta;
        State->LastPacketPts = PacketPts;
        State->LastRtpTs = rtp;
        return rtp;
    }

    // No PTS: fall back to incrementing by an estimated per-frame amount
    if (State->RtpMultiplier > 0.0)
    {
        // Assume encoder increments PTS by 1 per frame; add multiplier
        State->LastRtpTs = State->LastRtpTs + static_cast<uint32_t>(State->RtpMultiplier);
        return State->LastRtpTs;
    }

    // Last resort: use wall-clock
    State->LastRtpTs = static_cast<uint32_t>(FPlatformTime::Seconds() * 90000.0);
    return State->LastRtpTs;
}

// Forward-declare readback free ctx used by av_buffer free-callbacks
struct __ReadbackFreeCtx { FRHIGPUTextureReadback* RB; };

// Free callback used by av_buffer_create when wrapping FRHIGPUTextureReadback
static void AvFreeReadbackLocal(void* opaque, uint8_t* data)
{
    __ReadbackFreeCtx* ctx = reinterpret_cast<__ReadbackFreeCtx*>(opaque);
    if (!ctx) return;
    FRHIGPUTextureReadback* RB = ctx->RB;
    ENQUEUE_RENDER_COMMAND(Synavis_FreeReadbackFromAVBuf_Local)([RB](FRHICommandListImmediate& RHICmdList)
    {
        if (RB) { RB->Unlock(); delete RB; }
    });
    delete ctx;
}

FVp9SendoffWorker::FVp9SendoffWorker(USynavisVp9SendoffHandler* InOwner)
    : Owner(InOwner) {}

// Destructor for FLibAVEncoderState (defined here so we can use libav free functions)
FLibAVEncoderState::~FLibAVEncoderState()
{
    FScopeLock guard(&Mutex);
    if (Packet)
    {
        av_packet_free(&Packet);
        Packet = nullptr;
    }
    if (Frame)
    {
        av_frame_free(&Frame);
        Frame = nullptr;
    }
    if (CodecCtx)
    {
        avcodec_free_context(&CodecCtx);
        CodecCtx = nullptr;
    }
}

uint32 FVp9SendoffWorker::Run()
{
    UE_LOG(LogTemp, Warning, TEXT("VP9 Worker: started for owner=%p"), Owner);
    while (!Owner->bShouldExit.Load())
    {
        FEncodedVp9Frame Frame;
        if (Owner->FrameQueue && Owner->FrameQueue->Dequeue(Frame))
        {
            UE_LOG(LogTemp, Verbose, TEXT("VP9 Worker: dequeued frame %d bytes"), Frame.Buffer.Num());
            Owner->ProcessFrame(Frame);
        }
        else
        {
            FPlatformProcess::Sleep(0.001f);
        }
    }
    return 0;
}

void FVp9SendoffWorker::Stop()
{
    Owner->bShouldExit = true;
}

USynavisVp9SendoffHandler::USynavisVp9SendoffHandler()
{
    PacketBuffers.SetNum(MaxPacketsPerFrame * PacketBufSize);
    PacketPool.SetNum(MaxPacketsPerFrame);
    FrameQueue = new TCircularQueue<FEncodedVp9Frame>(128);
}

FLibAVEncoderState* USynavisVp9SendoffHandler::GetOrCreateLibAVEncoderState()
{
    if (!InternalLibAVState)
    {
        InternalLibAVState = new FLibAVEncoderState();
        InternalLibAVState->Codec = avcodec_find_encoder(AV_CODEC_ID_VP9);
    }
    return InternalLibAVState;
}

void USynavisVp9SendoffHandler::Initialize(int32 InPayloadType, uint32 InSSRC, uint16 InMaxPayloadSize)
{
    PayloadType = InPayloadType;
    MaxPayloadSize = InMaxPayloadSize;
    if (InSSRC == 0)
    {
        static TAtomic<uint32> NextSSRC(12345678U);
        SSRC = NextSSRC++;
    }
    else
    {
        SSRC = InSSRC;
    }
    SequenceNumber = FMath::RandRange(0, 65535);
    PictureId = 0;
    bShouldExit = false;
    if (!Worker)
    {
        Worker = new FVp9SendoffWorker(this);
        WorkerThread = FRunnableThread::Create(Worker, TEXT("SynavisVp9SendoffHandlerWorker"));
        UE_LOG(LogTemp, Warning, TEXT("VP9 Sendoff: worker thread created, owner=%p thread=%p"), this, WorkerThread);
    }
}

void USynavisVp9SendoffHandler::SendFrame(TArray<uint8>&& InFrameBuffer, uint32 FrameTimestamp90khz, uint32 FrameDuration90khz, const TArray<int32>& TracksToSend)
{
    FEncodedVp9Frame Frame;
    Frame.Buffer = MoveTemp(InFrameBuffer);
    Frame.Timestamp90khz = FrameTimestamp90khz;
    Frame.Duration90khz = FrameDuration90khz;
    CurrentTracksToSend = TracksToSend;
    if (!FrameQueue || !FrameQueue->Enqueue(MoveTemp(Frame)))
    {
        UE_LOG(LogTemp, Warning, TEXT("VP9 Queue full, drop frame %d bytes"), Frame.Buffer.Num());
    }
    else
    {
        UE_LOG(LogTemp, Verbose, TEXT("VP9 SendFrame: enqueued frame %d bytes tracks=%d"), Frame.Buffer.Num(), TracksToSend.Num());
    }
}

void USynavisVp9SendoffHandler::Shutdown()
{
    bShouldExit = true;
    if (WorkerThread)
    {
        WorkerThread->WaitForCompletion();
        delete WorkerThread;
        WorkerThread = nullptr;
    }
    if (Worker)
    {
        delete Worker;
        Worker = nullptr;
    }
    if (FrameQueue)
    {
        delete FrameQueue;
        FrameQueue = nullptr;
    }
}

static FORCEINLINE void WriteBE16_local(uint8* Buf, uint16 Val)
{
    Buf[0] = (Val >> 8) & 0xFF;
    Buf[1] = Val & 0xFF;
}
static FORCEINLINE void WriteBE32_local(uint8* Buf, uint32 Val)
{
    Buf[0] = (Val >> 24) & 0xFF;
    Buf[1] = (Val >> 16) & 0xFF;
    Buf[2] = (Val >> 8) & 0xFF;
    Buf[3] = Val & 0xFF;
}

static int BuildRtpVp9Packet_local(uint8* OutBuf, int Remaining, const uint8* FrameData, bool IsFirst, bool IsLast, uint16 SeqNum, uint32 Timestamp, uint32 SSRC, uint8 PayloadType, uint16 MaxPayloadSize)
{
    OutBuf[0] = 0x80; // V=2, P=0, X=0, CC=0
    OutBuf[1] = PayloadType & 0x7F;
    if (IsLast) OutBuf[1] |= 0x80;
    WriteBE16_local(OutBuf + 2, SeqNum);
    WriteBE32_local(OutBuf + 4, Timestamp);
    WriteBE32_local(OutBuf + 8, SSRC);
    OutBuf[12] = 0;
    if (IsFirst) OutBuf[12] |= 0x08;
    if (IsLast) OutBuf[12] |= 0x04;
    int PayloadSize = FMath::Min(Remaining, (int)MaxPayloadSize - 1);
    FMemory::Memcpy(OutBuf + 13, FrameData, PayloadSize);
    return 13 + PayloadSize;
}

void USynavisVp9SendoffHandler::ProcessFrame(const FEncodedVp9Frame& Frame)
{
    int32 PacketIdx = 0;
    uint32 FrameSize = Frame.Buffer.Num();
    const uint8* FrameData = Frame.Buffer.GetData();
    int32 Remaining = FrameSize;
    bool IsFirst = true;
    // Snapshot starting sequence for logging
    uint16_t StartSequence = static_cast<uint16_t>(SequenceNumber);
    while (Remaining > 0 && PacketIdx < MaxPacketsPerFrame)
    {
        bool IsLast = (Remaining <= (MaxPayloadSize - 1));
        uint8* PacketBuf = &PacketBuffers[PacketIdx * PacketBufSize];
        int Written = BuildRtpVp9Packet_local(PacketBuf, Remaining, FrameData, IsFirst, IsLast, SequenceNumber, Frame.Timestamp90khz, SSRC, PayloadType, MaxPayloadSize);
        PacketPool[PacketIdx] = {PacketBuf, static_cast<uint16>(Written)};
        int PayloadSize = FMath::Min(Remaining, (int)MaxPayloadSize - 1);
        Remaining -= PayloadSize;
        FrameData += PayloadSize;
        IsFirst = false;
        SequenceNumber++;
        PacketIdx++;
    }
    UE_LOG(LogTemp, Verbose, TEXT("VP9 Packetized %d bytes into %d RTP packets (TS=%u)"), FrameSize, PacketIdx, Frame.Timestamp90khz);
    UE_LOG(LogTemp, Warning, TEXT("VP9 RTP send: PT=%d SSRC=%u startSeq=%u TS=%u packets=%d"), PayloadType, SSRC, StartSequence, Frame.Timestamp90khz, PacketIdx);
    for (int32 i = 0; i < PacketIdx; ++i)
    {
        const FVp9PacketDesc& Packet = PacketPool[i];
        uint16_t SeqForPacket = static_cast<uint16_t>(StartSequence + i);
        for (int32 Track : CurrentTracksToSend)
        {
            if (rtcIsOpen(Track))
            {
                UE_LOG(LogTemp, Verbose, TEXT("VP9 Send: PT=%d SSRC=%u Seq=%u Len=%d Track=%d TS=%u"), PayloadType, SSRC, SeqForPacket, Packet.Len, Track, Frame.Timestamp90khz);
                rtcSendMessage(Track, reinterpret_cast<const char*>(Packet.Data), Packet.Len);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("VP9: Track %d closed, drop packet Seq=%u"), Track, SeqForPacket);
            }
        }
    }
    PictureId++;
}

// Non-blocking enqueue: runs encoding and sending on an async thread so game thread is not blocked.
void USynavisVp9SendoffHandler::EnqueueReadbackNonBlocking(FRHIGPUTextureReadback* ReadbackY, FRHIGPUTextureReadback* ReadbackU, FRHIGPUTextureReadback* ReadbackV, int Width, int Height, const TArray<int32>& TargetTracks, FLibAVEncoderState* LibAVState)
{
    if (!ReadbackY || !ReadbackU || !ReadbackV || !LibAVState)
    {
        UE_LOG(LogTemp, Warning, TEXT("VP9 EnqueueReadbackNonBlocking: invalid args ReadbackY=%p ReadbackU=%p ReadbackV=%p LibAVState=%p"), ReadbackY, ReadbackU, ReadbackV, LibAVState);
        return;
    }
    // Capture by value necessary params; run on worker thread
    TArray<int32> Tracks = TargetTracks;
    Async(EAsyncExecution::Thread, [ReadbackY, ReadbackU, ReadbackV, Width, Height, Tracks, LibAVState, this]() mutable
    {
        UE_LOG(LogTemp, Verbose, TEXT("VP9 Enqueue: async start Width=%d Height=%d tracks=%d LibAVState=%p"), Width, Height, Tracks.Num(), LibAVState);
        // Poll for readiness in background thread (short timeout)
        const double TimeoutSeconds = 0.5;
        double StartTime = FPlatformTime::Seconds();
        while (FPlatformTime::Seconds() - StartTime < TimeoutSeconds)
        {
            if (ReadbackY->IsReady() && ReadbackU->IsReady() && ReadbackV->IsReady()) break;
            FPlatformProcess::Sleep(0.001f);
        }
        if (!ReadbackY->IsReady() || !ReadbackU->IsReady() || !ReadbackV->IsReady())
        {
            UE_LOG(LogTemp, Warning, TEXT("VP9 Enqueue: readback timeout or not ready Y=%d U=%d V=%d"), ReadbackY->IsReady(), ReadbackU->IsReady(), ReadbackV->IsReady());
            return;
        }

        int YRowPitch = 0; void* YPtr = ReadbackY->Lock(YRowPitch);
        if (!YPtr) { UE_LOG(LogTemp, Warning, TEXT("VP9 Enqueue: Lock Y failed")); ReadbackY->Unlock(); ReadbackU->Unlock(); ReadbackV->Unlock(); return; }
        int URowPitch = 0; void* UPtr = ReadbackU->Lock(URowPitch);
        if (!UPtr) { UE_LOG(LogTemp, Warning, TEXT("VP9 Enqueue: Lock U failed")); ReadbackY->Unlock(); ReadbackU->Unlock(); ReadbackV->Unlock(); return; }
        int VRowPitch = 0; void* VPtr = ReadbackV->Lock(VRowPitch);
        if (!VPtr) { UE_LOG(LogTemp, Warning, TEXT("VP9 Enqueue: Lock V failed")); ReadbackY->Unlock(); ReadbackU->Unlock(); ReadbackV->Unlock(); return; }

        int YSize = YRowPitch * Height;
        int HalfHeight = (Height + 1) / 2;
        int USize = URowPitch * HalfHeight;
        int VSize = VRowPitch * HalfHeight;

        struct ReadbackFreeCtx { FRHIGPUTextureReadback* RB; };
        ReadbackFreeCtx* ctxY = new ReadbackFreeCtx{ ReadbackY };
        ReadbackFreeCtx* ctxU = new ReadbackFreeCtx{ ReadbackU };
        ReadbackFreeCtx* ctxV = new ReadbackFreeCtx{ ReadbackV };

        AVBufferRef* bufY = av_buffer_create(static_cast<uint8_t*>(YPtr), static_cast<int>(YSize), AvFreeReadbackLocal, ctxY, 0);
        AVBufferRef* bufU = av_buffer_create(static_cast<uint8_t*>(UPtr), static_cast<int>(USize), AvFreeReadbackLocal, ctxU, 0);
        AVBufferRef* bufV = av_buffer_create(static_cast<uint8_t*>(VPtr), static_cast<int>(VSize), AvFreeReadbackLocal, ctxV, 0);

        if (!bufY || !bufU || !bufV)
        {
            UE_LOG(LogTemp, Warning, TEXT("VP9 Enqueue: av_buffer_create failed bufY=%p bufU=%p bufV=%p"), bufY, bufU, bufV);
            if (bufY) av_buffer_unref(&bufY);
            if (bufU) av_buffer_unref(&bufU);
            if (bufV) av_buffer_unref(&bufV);
            return;
        }

        FScopeLock guard(&LibAVState->Mutex);
        if (!LibAVState->CodecCtx)
        {
            if (!LibAVState->Codec)
            {
                av_buffer_unref(&bufY);
                av_buffer_unref(&bufU);
                av_buffer_unref(&bufV);
                return;
            }
            LibAVState->CodecCtx = avcodec_alloc_context3(LibAVState->Codec);
            if (!LibAVState->CodecCtx)
            {
                av_buffer_unref(&bufY);
                av_buffer_unref(&bufU);
                av_buffer_unref(&bufV);
                return;
            }
            LibAVState->CodecCtx->width = Width;
            LibAVState->CodecCtx->height = Height;
            LibAVState->CodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
            LibAVState->CodecCtx->time_base = AVRational{1, 30};
            if (!LibAVState->Packet) LibAVState->Packet = av_packet_alloc();
            int openRc = avcodec_open2(LibAVState->CodecCtx, LibAVState->Codec, nullptr);
            if (openRc < 0)
            {
                UE_LOG(LogTemp, Warning, TEXT("VP9 Enqueue: avcodec_open2 failed rc=%d"), openRc);
                avcodec_free_context(&LibAVState->CodecCtx);
                av_buffer_unref(&bufY);
                av_buffer_unref(&bufU);
                av_buffer_unref(&bufV);
                return;
            }
            LibAVState->Width = Width;
            LibAVState->Height = Height;
        }

        // Reuse the persistent AVFrame allocated in FLibAVEncoderState when possible.
        // SynavisStreamer registers a video source and initializes LibAVState->Frame;
        // allocate only if it's missing to avoid per-frame allocations.
        AVFrame* frame = LibAVState->Frame;
        if (!frame)
        {
            LibAVState->Frame = av_frame_alloc();
            frame = LibAVState->Frame;
            if (!frame)
            {
                UE_LOG(LogTemp, Warning, TEXT("VP9 Enqueue: av_frame_alloc failed"));
                av_buffer_unref(&bufY);
                av_buffer_unref(&bufU);
                av_buffer_unref(&bufV);
                return;
            }
        }
        frame->format = AV_PIX_FMT_YUV420P;
        frame->width = Width; frame->height = Height;

        AVBufferRef* refY = av_buffer_ref(bufY);
        AVBufferRef* refU = av_buffer_ref(bufU);
        AVBufferRef* refV = av_buffer_ref(bufV);
        av_buffer_unref(&bufY);
        av_buffer_unref(&bufU);
        av_buffer_unref(&bufV);

        if (!refY || !refU || !refV)
        {
            if (refY) av_buffer_unref(&refY);
            if (refU) av_buffer_unref(&refU);
            if (refV) av_buffer_unref(&refV);
            av_frame_free(&frame);
            return;
        }

        frame->buf[0] = refY;
        frame->buf[1] = refU;
        frame->buf[2] = refV;
        frame->data[0] = refY->data; frame->linesize[0] = YRowPitch;
        frame->data[1] = refU->data; frame->linesize[1] = URowPitch;
        frame->data[2] = refV->data; frame->linesize[2] = VRowPitch;

        int ret = avcodec_send_frame(LibAVState->CodecCtx, frame);
        if (ret < 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("VP9 Enqueue: avcodec_send_frame failed rc=%d"), ret);
            // Do not free the persistent AVFrame; just unreference buffers
            av_frame_unref(frame);
        }
        else
        {
            int recvCount = 0;
            while ((ret = avcodec_receive_packet(LibAVState->CodecCtx, LibAVState->Packet)) >= 0)
            {
                recvCount++;
                size_t sz = static_cast<size_t>(LibAVState->Packet->size);
                UE_LOG(LogTemp, Verbose, TEXT("VP9 Enqueue: received packet size=%zu pts=%lld"), sz, LibAVState->Packet->pts);
                if (sz == 0)
                {
                    av_packet_unref(LibAVState->Packet);
                    continue;
                }
                TArray<uint8> Vp9Buffer;
                Vp9Buffer.Append(reinterpret_cast<uint8*>(LibAVState->Packet->data), LibAVState->Packet->size);
                uint32_t rtpTs = 0;
                // Compute RTP timestamp using local timestamp state in LibAVState.
                // This avoids requiring av_rescale_q at every callsite and
                // yields stable timestamps even when encoders skip PTS.
                rtpTs = ComputeRtpTimestamp(LibAVState, LibAVState->Packet->pts, LibAVState->CodecCtx->time_base);
                UE_LOG(LogTemp, Verbose, TEXT("VP9 Enqueue: sending encoded frame %d bytes TS=%u"), Vp9Buffer.Num(), rtpTs);
                // Use this object's SendFrame to queue packetization and send
                this->SendFrame(MoveTemp(Vp9Buffer), rtpTs, 90000/30, Tracks);
                av_packet_unref(LibAVState->Packet);
            }
            if (recvCount == 0)
            {
                UE_LOG(LogTemp, Verbose, TEXT("VP9 Enqueue: no packets produced by encoder"));
            }
            // After encoding, release the per-frame buffer refs but keep the AVFrame
            av_frame_unref(frame);
        }
    });
}
