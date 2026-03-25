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
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/buffer.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>

}
THIRD_PARTY_INCLUDES_END

// guard for system includes


#include <atomic>
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/PlatformFile.h"


// LogHexVerbose as expected below:
static void LogHexVerbose(const char* Data, int32 Size, const TCHAR* Prefix)
{
  if (Size <= 0 || !Data) return;
  FString HexStr;
  for (int32 i = 0; i < Size; ++i)
  {
    HexStr += FString::Printf(TEXT("%02X "), static_cast<uint8>(Data[i]));
  }
  UE_LOG(LogTemp, Verbose, TEXT("%s: %s"), Prefix, *HexStr);
}

// Small helper to format FFmpeg errors succinctly
static void LogAvError(int Ret, const TCHAR* Msg)
{
        if (Ret >= 0) return;
        char Err[128] = {0};
        av_strerror(Ret, Err, sizeof(Err));
        UE_LOG(LogTemp, Error, TEXT("%s: %S (%d)"), Msg, Err, Ret);
}

// Small helper returning a short hex string for the first N bytes of a buffer
static FString DumpFirstBytes(const uint8* Data, int32 Count)
{
        if (!Data || Count <= 0) return FString(TEXT(""));
        FString S;
        for (int i = 0; i < Count; ++i) S += FString::Printf(TEXT("%02X"), Data[i]);
        return S;
}

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
  while (Owner && !Owner->bShouldExit.Load())
  {
    FEncodedVp9Frame Frame;
    if (Owner && Owner->FrameQueue && Owner->FrameQueue->Dequeue(Frame))
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

void USynavisVp9SendoffHandler::RegisterTrackSsrc(int32 TrackId, uint32 Ssrc)
{
        FScopeLock guard(&TrackSsrcMutex);
        TrackSsrcMap.Add(TrackId, Ssrc);
        UE_LOG(LogTemp, Verbose, TEXT("VP9 Sendoff: registered SSRC %u for track %d"), Ssrc, TrackId);
}

void USynavisVp9SendoffHandler::UnregisterTrack(int32 TrackId)
{
        FScopeLock guard(&TrackSsrcMutex);
        TrackSsrcMap.Remove(TrackId);
        UE_LOG(LogTemp, Verbose, TEXT("VP9 Sendoff: unregistered track %d"), TrackId);
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

void USynavisVp9SendoffHandler::SetPayloadType(int32 NewPayloadType)
{
    if (NewPayloadType <= 0) return;
    if (PayloadType == NewPayloadType) return;
    UE_LOG(LogTemp, Log, TEXT("VP9 Sendoff: updating payload type from %d to %d"), PayloadType, NewPayloadType);
    PayloadType = NewPayloadType;
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

    // According to RFC9628, Sequence number is big endian
    uint16_t SeqNet = static_cast<uint16_t>(SeqNum);
#if PLATFORM_LITTLE_ENDIAN
    SeqNet = BYTESWAP_ORDER16(SeqNet);
#endif
    FMemory::Memcpy(OutBuf + 2, &SeqNet, sizeof(SeqNet));
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
    if( Remaining > 0 )
    {
        UE_LOG(LogTemp, Warning, TEXT("VP9 Frame too large to packetize: %d bytes remaining after %d packets"), Remaining, PacketIdx);
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
                // Determine the SSRC to stamp for this track. Use per-track mapping
                // if available, otherwise fall back to the handler-wide SSRC.
                uint32_t trackSsrc = SSRC;
                {
                    FScopeLock guard(&TrackSsrcMutex);
                    uint32* found = TrackSsrcMap.Find(Track);
                    if (found) trackSsrc = *found;
                }

                UE_LOG(LogTemp, Verbose, TEXT("VP9 Send: PT=%d SSRC=%u(seq_base=%u) Seq=%u Len=%d Track=%d TS=%u"), PayloadType, trackSsrc, SSRC, SeqForPacket, Packet.Len, Track, Frame.Timestamp90khz);

                // Patch SSRC in packet header (bytes 8..11) with track-specific SSRC.
                if (Packet.Len >= 12)
                {
                    Packet.Data[8] = static_cast<uint8_t>((trackSsrc >> 24) & 0xFF);
                    Packet.Data[9] = static_cast<uint8_t>((trackSsrc >> 16) & 0xFF);
                    Packet.Data[10] = static_cast<uint8_t>((trackSsrc >> 8) & 0xFF);
                    Packet.Data[11] = static_cast<uint8_t>((trackSsrc) & 0xFF);
                }

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

void USynavisVp9SendoffHandler::EncodeAndSendYuv420Buffers(AVBufferRef* BufY, AVBufferRef* BufU, AVBufferRef* BufV, int Width, int Height, int YRowPitch, int URowPitch, int VRowPitch, const TArray<int32>& Tracks, FLibAVEncoderState* LibAVState)
{
    if (!BufY || !BufU || !BufV || !LibAVState)
    {
        if (BufY) av_buffer_unref(&BufY);
        if (BufU) av_buffer_unref(&BufU);
        if (BufV) av_buffer_unref(&BufV);
        return;
    }

    FScopeLock guard(&LibAVState->Mutex);
    if (!LibAVState->CodecCtx)
    {
        if (!LibAVState->Codec)
        {
            av_buffer_unref(&BufY);
            av_buffer_unref(&BufU);
            av_buffer_unref(&BufV);
            return;
        }
        LibAVState->CodecCtx = avcodec_alloc_context3(LibAVState->Codec);
        if (!LibAVState->CodecCtx)
        {
            av_buffer_unref(&BufY);
            av_buffer_unref(&BufU);
            av_buffer_unref(&BufV);
            return;
        }
        LibAVState->CodecCtx->width = Width;
        LibAVState->CodecCtx->height = Height;
        LibAVState->CodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
        LibAVState->CodecCtx->time_base = AVRational{1, 30};
        if (!LibAVState->Packet) LibAVState->Packet = av_packet_alloc();
        if (LibAVState->CodecCtx && LibAVState->CodecCtx->priv_data)
        {
            int r1 = av_opt_set(LibAVState->CodecCtx->priv_data, "deadline", "realtime", 0);
            LogAvError(r1, TEXT("VP9 Enqueue: av_opt_set(deadline)"));
            int r2 = av_opt_set_int(LibAVState->CodecCtx->priv_data, "lag-in-frames", 0, 0);
            LogAvError(r2, TEXT("VP9 Enqueue: av_opt_set_int(lag-in-frames)"));
            int r3 = av_opt_set_int(LibAVState->CodecCtx->priv_data, "cpu-used", 8, 0);
            LogAvError(r3, TEXT("VP9 Enqueue: av_opt_set_int(cpu-used)"));
            int64_t EffectiveBitrateKbps = (this && this->TargetBitrateKbps > 0) ? this->TargetBitrateKbps : 512;
            int64_t EffectiveKeyframeInterval = (this && this->KeyframeInterval > 0) ? this->KeyframeInterval : 128;
            int r4 = av_opt_set_int(LibAVState->CodecCtx->priv_data, "rc_target_bitrate", (int)EffectiveBitrateKbps, 0);
            LogAvError(r4, TEXT("VP9 Enqueue: av_opt_set_int(rc_target_bitrate)"));
            int r5 = av_opt_set_int(LibAVState->CodecCtx->priv_data, "kf-max-dist", (int)EffectiveKeyframeInterval, 0);
            LogAvError(r5, TEXT("VP9 Enqueue: av_opt_set_int(kf-max-dist)"));
            LibAVState->CodecCtx->bit_rate = (int64_t)EffectiveBitrateKbps * 1000;
            LibAVState->CodecCtx->gop_size = (int)EffectiveKeyframeInterval;
        }
        UE_LOG(LogTemp, Warning, TEXT("VP9 Enqueue: pre-open: codec=%S id=%d width=%d height=%d pix_fmt=%d (YUV420P=%d) RowPitch Y=%d U=%d V=%d"),
            LibAVState->Codec ? LibAVState->Codec->name : (const char*)"?",
            LibAVState->Codec ? (int)LibAVState->Codec->id : -1,
            LibAVState->CodecCtx->width,
            LibAVState->CodecCtx->height,
            LibAVState->CodecCtx->pix_fmt,
            AV_PIX_FMT_YUV420P,
            YRowPitch, URowPitch, VRowPitch);

        int openRc = avcodec_open2(LibAVState->CodecCtx, LibAVState->Codec, nullptr);
        if (openRc < 0)
        {
            LogAvError(openRc, TEXT("VP9 Enqueue: avcodec_open2 failed"));
            if (LibAVState->CodecCtx)
            {
                UE_LOG(LogTemp, Error, TEXT("VP9 Enqueue: CodecCtx dump before free: codec=%S id=%d width=%d height=%d pix_fmt=%d threads=%d bit_rate=%lld gop_size=%d max_b_frames=%d profile=%d level=%d"),
                    LibAVState->Codec ? LibAVState->Codec->name : (const char*)"?",
                    (int)LibAVState->CodecCtx->codec_id,
                    LibAVState->CodecCtx->width,
                    LibAVState->CodecCtx->height,
                    LibAVState->CodecCtx->pix_fmt,
                    LibAVState->CodecCtx->thread_count,
                    (long long)LibAVState->CodecCtx->bit_rate,
                    LibAVState->CodecCtx->gop_size,
                    LibAVState->CodecCtx->max_b_frames,
                    LibAVState->CodecCtx->profile,
                    LibAVState->CodecCtx->level);
                UE_LOG(LogTemp, Error, TEXT("VP9 Enqueue: time_base=%d/%d framerate=%d/%d sample_aspect_ratio=%d/%d"),
                    LibAVState->CodecCtx->time_base.num, LibAVState->CodecCtx->time_base.den,
                    LibAVState->CodecCtx->framerate.num, LibAVState->CodecCtx->framerate.den,
                    LibAVState->CodecCtx->sample_aspect_ratio.num, LibAVState->CodecCtx->sample_aspect_ratio.den);
                UE_LOG(LogTemp, Error, TEXT("VP9 Enqueue: pix_fmt description hint: YUV420P=%d"), AV_PIX_FMT_YUV420P);
                UE_LOG(LogTemp, Error, TEXT("VP9 Enqueue: CodecCtx priv_data=%p priv_class=%p"), LibAVState->CodecCtx->priv_data, LibAVState->CodecCtx->av_class ? (void*)LibAVState->CodecCtx->av_class : nullptr);
            }
            avcodec_free_context(&LibAVState->CodecCtx);
            av_buffer_unref(&BufY);
            av_buffer_unref(&BufU);
            av_buffer_unref(&BufV);
            return;
        }
        UE_LOG(LogTemp, Log, TEXT("ENC init codec=%S w=%d h=%d pix_fmt=%d tb=%d/%d fr=%d/%d gop=%d max_b=%d"),
            LibAVState->Codec ? LibAVState->Codec->name : "?", LibAVState->CodecCtx->width, LibAVState->CodecCtx->height, LibAVState->CodecCtx->pix_fmt,
            LibAVState->CodecCtx->time_base.num, LibAVState->CodecCtx->time_base.den,
            LibAVState->CodecCtx->framerate.num, LibAVState->CodecCtx->framerate.den,
            LibAVState->CodecCtx->gop_size, LibAVState->CodecCtx->max_b_frames);
        LibAVState->Width = Width;
        LibAVState->Height = Height;
    }

    AVFrame* frame = LibAVState->Frame;
    if (!frame)
    {
        LibAVState->Frame = av_frame_alloc();
        frame = LibAVState->Frame;
        if (!frame)
        {
            UE_LOG(LogTemp, Warning, TEXT("VP9 Enqueue: av_frame_alloc failed"));
            av_buffer_unref(&BufY);
            av_buffer_unref(&BufU);
            av_buffer_unref(&BufV);
            return;
        }
    }
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = Width;
    frame->height = Height;

    AVBufferRef* refY = av_buffer_ref(BufY);
    AVBufferRef* refU = av_buffer_ref(BufU);
    AVBufferRef* refV = av_buffer_ref(BufV);
    av_buffer_unref(&BufY);
    av_buffer_unref(&BufU);
    av_buffer_unref(&BufV);

    if (!refY || !refU || !refV)
    {
        if (refY) av_buffer_unref(&refY);
        if (refU) av_buffer_unref(&refU);
        if (refV) av_buffer_unref(&refV);
        av_frame_unref(frame);
        return;
    }

    frame->buf[0] = refY;
    frame->buf[1] = refU;
    frame->buf[2] = refV;
    frame->data[0] = refY->data;
    frame->data[1] = refU->data;
    frame->data[2] = refV->data;
    frame->linesize[0] = YRowPitch;
    frame->linesize[1] = URowPitch;
    frame->linesize[2] = VRowPitch;

    static std::atomic<bool> bDumped(false);
    if (!bDumped.load())
    {
        int bufSize = av_image_get_buffer_size(static_cast<AVPixelFormat>(frame->format), frame->width, frame->height, 1);
        if (bufSize > 0)
        {
            TArray<uint8> DumpArray;
            DumpArray.SetNumUninitialized(bufSize);
            int copied = av_image_copy_to_buffer(DumpArray.GetData(), bufSize, frame->data, frame->linesize, static_cast<AVPixelFormat>(frame->format), frame->width, frame->height, 1);
            if (copied > 0)
            {
                FString OutPath = FPaths::ProjectSavedDir() / TEXT("synavis_frame_dump.yuv");
                if (FFileHelper::SaveArrayToFile(DumpArray, *OutPath))
                {
                    UE_LOG(LogTemp, Warning, TEXT("Wrote raw YUV frame to %s size=%d"), *OutPath, copied);
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("Failed to save dump file %s"), *OutPath);
                }
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("av_image_copy_to_buffer failed with %d"), copied);
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("av_image_get_buffer_size returned %d"), bufSize);
        }
        bDumped.store(true);
    }

    frame->pts = static_cast<int64_t>(++LibAVState->FrameCounter);
    UE_LOG(LogTemp, Verbose, TEXT("ENC send_frame pts=%lld fmt=%d w=%d h=%d"), (long long)frame->pts, frame->format, frame->width, frame->height);
    int ret = avcodec_send_frame(LibAVState->CodecCtx, frame);
    if (ret == AVERROR(EAGAIN)) { UE_LOG(LogTemp, Verbose, TEXT("ENC send_frame: EAGAIN, must drain packets")); av_frame_unref(frame); }
    else if (ret == AVERROR_EOF) { UE_LOG(LogTemp, Warning, TEXT("ENC send_frame: EOF, encoder flushed")); av_frame_unref(frame); }
    else if (ret < 0) { LogAvError(ret, TEXT("ENC send_frame error")); av_frame_unref(frame); return; }
    else
    {
        UE_LOG(LogTemp, VeryVerbose, TEXT("ENC send_frame ok"));
        UE_LOG(LogTemp, Verbose, TEXT("VP9 Enqueue: frame input width=%d height=%d linesize[0]=%d linesize[1]=%d linesize[2]=%d"),
            frame->width, frame->height, frame->linesize[0], frame->linesize[1], frame->linesize[2]);
        if (frame->data[0] && frame->linesize[0] > 0)
        {
            const int DumpBytes = FMath::Min(16, frame->linesize[0]);
            UE_LOG(LogTemp, VeryVerbose, TEXT("ENC frame Y[0..15]=%s linesizeY=%d"), *DumpFirstBytes(frame->data[0], DumpBytes), frame->linesize[0]);
        }

        int packets = 0;
        for (;;)
        {
            int r = avcodec_receive_packet(LibAVState->CodecCtx, LibAVState->Packet);
            if (r == 0)
            {
                ++packets;
                size_t sz = static_cast<size_t>(LibAVState->Packet->size);
                UE_LOG(LogTemp, Verbose, TEXT("ENC got packet size=%d pts=%lld dts=%lld flags=%d"), (int)sz, (long long)LibAVState->Packet->pts, (long long)LibAVState->Packet->dts, LibAVState->Packet->flags);
                if (sz > 0 && sz < 100)
                {
                    AVCodecContext* c = LibAVState->CodecCtx;
                    UE_LOG(LogTemp, Verbose, TEXT("ENC small-pkt state: sz=%d codec=%S w=%d h=%d pix=%d tb=%d/%d fr=%d/%d gop=%d max_b=%d frameCounter=%llu lastPktPts=%lld rtpMult=%f"),
                        (int)sz,
                        c && c->codec ? c->codec->name : "?",
                        c ? c->width : 0,
                        c ? c->height : 0,
                        c ? c->pix_fmt : -1,
                        c ? c->time_base.num : 0, c ? c->time_base.den : 0,
                        c ? c->framerate.num : 0, c ? c->framerate.den : 0,
                        c ? c->gop_size : 0, c ? c->max_b_frames : 0,
                        (unsigned long long)LibAVState->FrameCounter,
                        (long long)LibAVState->LastPacketPts,
                        LibAVState->RtpMultiplier);
                }
                if (sz > 0 && sz <= 128) { LogHexVerbose(reinterpret_cast<const char*>(LibAVState->Packet->data), (int)sz, TEXT("VP9 Enqueue: pkt hex")); }
                if (sz == 0) { av_packet_unref(LibAVState->Packet); UE_LOG(LogTemp, Verbose, TEXT("VP9 Enqueue: empty packet received, skip")); continue; }
                static IFileHandle* IVFHandle = nullptr;
                static uint32 IVFFrameCount = 0;
                if (!IVFHandle)
                {
                    FString IVFPath = FPaths::ProjectSavedDir() / TEXT("synavis_frame_dump.ivf");
                    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
                    IVFHandle = PlatformFile.OpenWrite(*IVFPath, false);
                    if (IVFHandle)
                    {
                        uint8 header[32] = {0};
                        header[0] = 'D'; header[1] = 'K'; header[2] = 'I'; header[3] = 'F';
                        header[4] = 0; header[5] = 0;
                        header[6] = 32; header[7] = 0;
                        header[8] = 'V'; header[9] = 'P'; header[10] = '9'; header[11] = '0';
                        header[12] = static_cast<uint8>(LibAVState->CodecCtx->width & 0xFF);
                        header[13] = static_cast<uint8>((LibAVState->CodecCtx->width >> 8) & 0xFF);
                        header[14] = static_cast<uint8>(LibAVState->CodecCtx->height & 0xFF);
                        header[15] = static_cast<uint8>((LibAVState->CodecCtx->height >> 8) & 0xFF);
                        uint32 frn = 30;
                        uint32 frd = 1;
                        header[16] = static_cast<uint8>(frn & 0xFF);
                        header[17] = static_cast<uint8>((frn >> 8) & 0xFF);
                        header[18] = static_cast<uint8>((frn >> 16) & 0xFF);
                        header[19] = static_cast<uint8>((frn >> 24) & 0xFF);
                        header[20] = static_cast<uint8>(frd & 0xFF);
                        header[21] = static_cast<uint8>((frd >> 8) & 0xFF);
                        header[22] = static_cast<uint8>((frd >> 16) & 0xFF);
                        header[23] = static_cast<uint8>((frd >> 24) & 0xFF);
                        for (int i = 24; i < 28; ++i) header[i] = 0;
                        for (int i = 28; i < 32; ++i) header[i] = 0;
                        IVFHandle->Write(header, 32);
                        IVFFrameCount = 0;
                    }
                }
                if (IVFHandle)
                {
                    uint32_t fsize = static_cast<uint32_t>(LibAVState->Packet->size);
                    uint64_t fpts = static_cast<uint64_t>(LibAVState->Packet->pts < 0 ? 0 : LibAVState->Packet->pts);
                    uint8 szbuf[4] = { static_cast<uint8>(fsize & 0xFF), static_cast<uint8>((fsize>>8)&0xFF), static_cast<uint8>((fsize>>16)&0xFF), static_cast<uint8>((fsize>>24)&0xFF) };
                    uint8 ptsbuf[8];
                    for (int i = 0; i < 8; ++i) ptsbuf[i] = static_cast<uint8>((fpts >> (8*i)) & 0xFF);
                    IVFHandle->Write(szbuf, 4);
                    IVFHandle->Write(ptsbuf, 8);
                    IVFHandle->Write(reinterpret_cast<const uint8*>(LibAVState->Packet->data), LibAVState->Packet->size);
                    IVFFrameCount++;
                }

                TArray<uint8> Vp9Buffer;
                Vp9Buffer.Append(reinterpret_cast<uint8*>(LibAVState->Packet->data), LibAVState->Packet->size);
                uint32_t rtpTs = ComputeRtpTimestamp(LibAVState, LibAVState->Packet->pts, LibAVState->CodecCtx->time_base);
                UE_LOG(LogTemp, Verbose, TEXT("VP9 Enqueue: sending encoded frame %d bytes TS=%u"), Vp9Buffer.Num(), rtpTs);
                this->SendFrame(MoveTemp(Vp9Buffer), rtpTs, 90000/30, Tracks);
                av_packet_unref(LibAVState->Packet);
                continue;
            }
            if (r == AVERROR(EAGAIN)) { UE_LOG(LogTemp, VeryVerbose, TEXT("ENC recv_packet: EAGAIN after %d packets"), packets); break; }
            if (r == AVERROR_EOF) { UE_LOG(LogTemp, Warning, TEXT("ENC recv_packet: EOF after %d packets"), packets); UE_LOG(LogTemp, Log, TEXT("ENC flush complete, encoder drained")); break; }
            { char ebuf[128] = {0}; av_strerror(r, ebuf, sizeof(ebuf)); UE_LOG(LogTemp, Error, TEXT("ENC recv_packet error: %S (%d) after %d packets"), ebuf, r, packets); break; }
        }
        if (packets == 0) UE_LOG(LogTemp, Verbose, TEXT("ENC frame pts=%lld produced no packets this cycle"), (long long)frame->pts);
        av_frame_unref(frame);
    }
}

// Non-blocking enqueue: runs encoding and sending on an async thread so game thread is not blocked.
void USynavisVp9SendoffHandler::EnqueueReadbackNonBlocking(FRHIGPUTextureReadback* ReadbackY, FRHIGPUTextureReadback* ReadbackU, FRHIGPUTextureReadback* ReadbackV, int Width, int Height, const TArray<int32>& TargetTracks, FLibAVEncoderState* LibAVState)
{
    UE_LOG(LogTemp, Verbose, TEXT("VP9 EnqueueReadbackNonBlocking called Width=%d Height=%d ReadbackY=%p ReadbackU=%p ReadbackV=%p LibAVState=%p Tracks=%d"),
        Width, Height, ReadbackY, ReadbackU, ReadbackV, LibAVState, TargetTracks.Num());
    if (!ReadbackY || !ReadbackU || !ReadbackV || !LibAVState)
    {
        bool nullY = (ReadbackY == nullptr);
        bool nullU = (ReadbackU == nullptr);
        bool nullV = (ReadbackV == nullptr);
        bool nullState = (LibAVState == nullptr);
        UE_LOG(LogTemp, Warning, TEXT("VP9 EnqueueReadbackNonBlocking: invalid args ReadbackY=%p ReadbackU=%p ReadbackV=%p LibAVState=%p (nullY=%d nullU=%d nullV=%d nullState=%d)"),
            ReadbackY, ReadbackU, ReadbackV, LibAVState, nullY ? 1 : 0, nullU ? 1 : 0, nullV ? 1 : 0, nullState ? 1 : 0);
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

        // Diagnostic: log row pitches vs logical plane widths so we can detect pitch/padding issues
        int UVWidth = (Width + 1) / 2;
        int UVHeight = (Height + 1) / 2;
        UE_LOG(LogTemp, Verbose, TEXT("ENC readback dims W=%d H=%d UVW=%d UVH=%d RowPitch Y=%d U=%d V=%d"), Width, Height, UVWidth, UVHeight, YRowPitch, URowPitch, VRowPitch);
        if (UE_GET_LOG_VERBOSITY(LogTemp) >= ELogVerbosity::VeryVerbose)
        {
            const int DumpBytes = FMath::Min(16, URowPitch);
            UE_LOG(LogTemp, VeryVerbose, TEXT("ENC readback U[0..15]=%s rowPitch=%d UVW=%d UVH=%d"), *DumpFirstBytes(static_cast<const uint8*>(UPtr), DumpBytes), URowPitch, UVWidth, UVHeight);
            UE_LOG(LogTemp, VeryVerbose, TEXT("ENC readback V[0..15]=%s rowPitch=%d UVW=%d UVH=%d"), *DumpFirstBytes(static_cast<const uint8*>(VPtr), DumpBytes), VRowPitch, UVWidth, UVHeight);
        }

        // One-time dump of the raw readback buffers (Y,U,V planes) before wrapping into AVBuffers.
        static std::atomic<bool> bReadbackDumped(false);
        if (!bReadbackDumped.load())
        {
            // Create tightly-packed 8-bit planar buffers from the readback. The RDG readback
            // may be 32-bit-per-texel (PF_R32_UINT) so we extract the low 8 bits per texel.
            int UVW = (Width + 1) / 2;
            int UVH = (Height + 1) / 2;
            TArray<uint8> PackedY; PackedY.SetNumUninitialized(Width * Height);
            TArray<uint8> PackedU; PackedU.SetNumUninitialized(UVW * UVH);
            TArray<uint8> PackedV; PackedV.SetNumUninitialized(UVW * UVH);

            // Pack Y
            if (YRowPitch == Width)
            {
                for (int y = 0; y < Height; ++y)
                {
                    const uint8* srcRow = static_cast<const uint8*>(YPtr) + (size_t)y * YRowPitch;
                    FMemory::Memcpy(PackedY.GetData() + (size_t)y * Width, srcRow, Width);
                }
            }
            else if (YRowPitch >= Width * 4)
            {
                const uint32_t* src32 = static_cast<const uint32_t*>(YPtr);
                int stride = YRowPitch / 4;
                for (int y = 0; y < Height; ++y)
                {
                    const uint32_t* row = src32 + (size_t)y * stride;
                    for (int x = 0; x < Width; ++x) PackedY[y * Width + x] = static_cast<uint8>(row[x] & 0xFFu);
                }
            }
            else
            {
                // Generic fallback: copy low byte of each texel by indexing bytes
                for (int y = 0; y < Height; ++y)
                {
                    const uint8* row = static_cast<const uint8*>(YPtr) + (size_t)y * YRowPitch;
                    for (int x = 0; x < Width; ++x) PackedY[y * Width + x] = row[x * (YRowPitch / Width)];
                }
            }

            // Pack U
            if (URowPitch == UVW)
            {
                for (int y = 0; y < UVH; ++y)
                {
                    const uint8* srcRow = static_cast<const uint8*>(UPtr) + (size_t)y * URowPitch;
                    FMemory::Memcpy(PackedU.GetData() + (size_t)y * UVW, srcRow, UVW);
                }
            }
            else if (URowPitch >= UVW * 4)
            {
                const uint32_t* src32 = static_cast<const uint32_t*>(UPtr);
                int stride = URowPitch / 4;
                for (int y = 0; y < UVH; ++y)
                {
                    const uint32_t* row = src32 + (size_t)y * stride;
                    for (int x = 0; x < UVW; ++x) PackedU[y * UVW + x] = static_cast<uint8>(row[x] & 0xFFu);
                }
            }
            else
            {
                for (int y = 0; y < UVH; ++y)
                {
                    const uint8* row = static_cast<const uint8*>(UPtr) + (size_t)y * URowPitch;
                    for (int x = 0; x < UVW; ++x) PackedU[y * UVW + x] = row[x * (URowPitch / UVW)];
                }
            }

            // Pack V
            if (VRowPitch == UVW)
            {
                for (int y = 0; y < UVH; ++y)
                {
                    const uint8* srcRow = static_cast<const uint8*>(VPtr) + (size_t)y * VRowPitch;
                    FMemory::Memcpy(PackedV.GetData() + (size_t)y * UVW, srcRow, UVW);
                }
            }
            else if (VRowPitch >= UVW * 4)
            {
                const uint32_t* src32 = static_cast<const uint32_t*>(VPtr);
                int stride = VRowPitch / 4;
                for (int y = 0; y < UVH; ++y)
                {
                    const uint32_t* row = src32 + (size_t)y * stride;
                    for (int x = 0; x < UVW; ++x) PackedV[y * UVW + x] = static_cast<uint8>(row[x] & 0xFFu);
                }
            }
            else
            {
                for (int y = 0; y < UVH; ++y)
                {
                    const uint8* row = static_cast<const uint8*>(VPtr) + (size_t)y * VRowPitch;
                    for (int x = 0; x < UVW; ++x) PackedV[y * UVW + x] = row[x * (VRowPitch / UVW)];
                }
            }

            // Use packed planes for the dump
            const uint8_t* srcPlanes[3] = { PackedY.GetData(), PackedU.GetData(), PackedV.GetData() };
            int srcLines[3] = { Width, UVW, UVW };
            int dumpSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, Width, Height, 1);
            if (dumpSize > 0)
            {
                TArray<uint8> DumpArray;
                DumpArray.SetNumUninitialized(dumpSize);
                int copied = av_image_copy_to_buffer(DumpArray.GetData(), dumpSize, srcPlanes, srcLines, AV_PIX_FMT_YUV420P, Width, Height, 1);
                if (copied > 0)
                {
                    FString OutPath = FPaths::ProjectSavedDir() / TEXT("synavis_readback_raw.yuv");
                    if (FFileHelper::SaveArrayToFile(DumpArray, *OutPath))
                    {
                        UE_LOG(LogTemp, Warning, TEXT("Wrote raw readback YUV to %s size=%d"), *OutPath, copied);
                    }
                    else
                    {
                        UE_LOG(LogTemp, Warning, TEXT("Failed to save readback dump file %s"), *OutPath);
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("av_image_copy_to_buffer(readback) failed with %d"), copied);
                }
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("av_image_get_buffer_size(readback) returned %d"), dumpSize);
            }
            bReadbackDumped.store(true);
        }

        // Additional one-time per-plane dumps and a reconstructed RGB PPM for easier visual debugging.
        static std::atomic<bool> bPlaneDumps(false);
        if (!bPlaneDumps.load())
        {
            // If RDG readback is 32-bit-per-texel, pack low bytes into temporary planes
            int UVW = (Width + 1) / 2;
            int UVH = (Height + 1) / 2;
            TArray<uint8> PackedY; PackedY.SetNumUninitialized(Width * Height);
            TArray<uint8> PackedU; PackedU.SetNumUninitialized(UVW * UVH);
            TArray<uint8> PackedV; PackedV.SetNumUninitialized(UVW * UVH);
            // Pack Y
            if (YRowPitch == Width)
            {
                for (int y = 0; y < Height; ++y) FMemory::Memcpy(PackedY.GetData() + (size_t)y * Width, static_cast<const uint8*>(YPtr) + (size_t)y * YRowPitch, Width);
            }
            else if (YRowPitch >= Width * 4)
            {
                const uint32_t* src32 = static_cast<const uint32_t*>(YPtr);
                int stride = YRowPitch / 4;
                for (int y = 0; y < Height; ++y)
                {
                    const uint32_t* row = src32 + (size_t)y * stride;
                    for (int x = 0; x < Width; ++x) PackedY[y * Width + x] = static_cast<uint8>(row[x] & 0xFFu);
                }
            }
            else
            {
                for (int y = 0; y < Height; ++y)
                {
                    const uint8* row = static_cast<const uint8*>(YPtr) + (size_t)y * YRowPitch;
                    for (int x = 0; x < Width; ++x) PackedY[y * Width + x] = row[x * (YRowPitch / Width)];
                }
            }
            // Pack U
            if (URowPitch == UVW)
            {
                for (int y = 0; y < UVH; ++y) FMemory::Memcpy(PackedU.GetData() + (size_t)y * UVW, static_cast<const uint8*>(UPtr) + (size_t)y * URowPitch, UVW);
            }
            else if (URowPitch >= UVW * 4)
            {
                const uint32_t* src32 = static_cast<const uint32_t*>(UPtr);
                int stride = URowPitch / 4;
                for (int y = 0; y < UVH; ++y)
                {
                    const uint32_t* row = src32 + (size_t)y * stride;
                    for (int x = 0; x < UVW; ++x) PackedU[y * UVW + x] = static_cast<uint8>(row[x] & 0xFFu);
                }
            }
            else
            {
                for (int y = 0; y < UVH; ++y)
                {
                    const uint8* row = static_cast<const uint8*>(UPtr) + (size_t)y * URowPitch;
                    for (int x = 0; x < UVW; ++x) PackedU[y * UVW + x] = row[x * (URowPitch / UVW)];
                }
            }
            // Pack V
            if (VRowPitch == UVW)
            {
                for (int y = 0; y < UVH; ++y) FMemory::Memcpy(PackedV.GetData() + (size_t)y * UVW, static_cast<const uint8*>(VPtr) + (size_t)y * VRowPitch, UVW);
            }
            else if (VRowPitch >= UVW * 4)
            {
                const uint32_t* src32 = static_cast<const uint32_t*>(VPtr);
                int stride = VRowPitch / 4;
                for (int y = 0; y < UVH; ++y)
                {
                    const uint32_t* row = src32 + (size_t)y * stride;
                    for (int x = 0; x < UVW; ++x) PackedV[y * UVW + x] = static_cast<uint8>(row[x] & 0xFFu);
                }
            }
            else
            {
                for (int y = 0; y < UVH; ++y)
                {
                    const uint8* row = static_cast<const uint8*>(VPtr) + (size_t)y * VRowPitch;
                    for (int x = 0; x < UVW; ++x) PackedV[y * UVW + x] = row[x * (VRowPitch / UVW)];
                }
            }

            // Prepare UTF8 header helper
            auto AppendUtf8 = [](TArray<uint8>& Arr, const FString& S){ FTCHARToUTF8 Conv(*S); Arr.Append(reinterpret_cast<const uint8*>(Conv.Get()), Conv.Length()); };

            // Y plane PGM (P5)
            {
                FString YPath = FPaths::ProjectSavedDir() / TEXT("synavis_readback_Y.pgm");
                FString Header = FString::Printf(TEXT("P5\n%d %d\n255\n"), Width, Height);
                TArray<uint8> Out;
                AppendUtf8(Out, Header);
                // Append each row (copy only Width bytes per row) from packed plane
                const uint8* srcY = PackedY.GetData();
                for (int y = 0; y < Height; ++y)
                {
                    Out.Append(srcY + (size_t)y * Width, Width);
                }
                if (FFileHelper::SaveArrayToFile(Out, *YPath))
                {
                UE_LOG(LogTemp, Warning, TEXT("Wrote readback Y plane to %s"), *YPath);
                }
                else
                {
                  UE_LOG(LogTemp, Warning, TEXT("Failed to save Y plane dump %s"), *YPath);
                }
            }

            {
                FString UPath = FPaths::ProjectSavedDir() / TEXT("synavis_readback_U.pgm");
                FString Header = FString::Printf(TEXT("P5\n%d %d\n255\n"), UVW, UVH);
                TArray<uint8> Out;
                AppendUtf8(Out, Header);
                const uint8* srcU = PackedU.GetData();
                for (int y = 0; y < UVH; ++y) Out.Append(srcU + (size_t)y * UVW, UVW);
                FFileHelper::SaveArrayToFile(Out, *UPath);
            }
            {
                FString VPath = FPaths::ProjectSavedDir() / TEXT("synavis_readback_V.pgm");
                FString Header = FString::Printf(TEXT("P5\n%d %d\n255\n"), UVW, UVH);
                TArray<uint8> Out;
                AppendUtf8(Out, Header);
                const uint8* srcV = PackedV.GetData();
                for (int y = 0; y < UVH; ++y) Out.Append(srcV + (size_t)y * UVW, UVW);
                FFileHelper::SaveArrayToFile(Out, *VPath);
            }

            // Reconstruct a simple RGB PPM (P6) using nearest-neighbor chroma upsample for quick visual check
            {
                FString RgbPath = FPaths::ProjectSavedDir() / TEXT("synavis_readback_recon.ppm");
                FString Header = FString::Printf(TEXT("P6\n%d %d\n255\n"), Width, Height);
                TArray<uint8> Out;
                AppendUtf8(Out, Header);
                const uint8* srcY = PackedY.GetData();
                const uint8* srcU = PackedU.GetData();
                const uint8* srcV = PackedV.GetData();
                Out.Reserve(Header.Len() + Width * Height * 3);
                for (int y = 0; y < Height; ++y)
                {
                    const uint8* rowY = srcY + (size_t)y * YRowPitch;
                    const uint8* rowU = srcU + (size_t)(y/2) * URowPitch;
                    const uint8* rowV = srcV + (size_t)(y/2) * VRowPitch;
                    for (int x = 0; x < Width; ++x)
                    {
                        int Yv = rowY[x];
                        int Uv = rowU[x/2];
                        int Vv = rowV[x/2];
                        // Convert YUV->RGB using full-range BT.601-ish matrix
                        float Yf = static_cast<float>(Yv);
                        float Uf = static_cast<float>(Uv) - 128.0f;
                        float Vf = static_cast<float>(Vv) - 128.0f;
                        int R = FMath::Clamp<int>(static_cast<int>(Yf + 1.402f * Vf + 0.5f), 0, 255);
                        int G = FMath::Clamp<int>(static_cast<int>(Yf - 0.344136f * Uf - 0.714136f * Vf + 0.5f), 0, 255);
                        int B = FMath::Clamp<int>(static_cast<int>(Yf + 1.772f * Uf + 0.5f), 0, 255);
                        Out.Add(static_cast<uint8>(R)); Out.Add(static_cast<uint8>(G)); Out.Add(static_cast<uint8>(B));
                    }
                }
                FFileHelper::SaveArrayToFile(Out, *RgbPath);
            }

            bPlaneDumps.store(true);
        }

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

        this->EncodeAndSendYuv420Buffers(bufY, bufU, bufV, Width, Height, YRowPitch, URowPitch, VRowPitch, Tracks, LibAVState);
    });
}

// Non-blocking queue using FFMpeg to accept RGB frames
void USynavisVp9SendoffHandler::EnqueueSoftwareNonBlocking(const TArrayView<const FColor>& RgbData, int Width, int Height, const TArray<int32>& TargetTracks, FLibAVEncoderState* LibAVState)
{
    TArray<FColor> OwnedRgb;
    OwnedRgb.Append(RgbData.GetData(), RgbData.Num());
        Async(EAsyncExecution::Thread, [OwnedRgb, Width, Height, TargetTracks, LibAVState, this]()
  {
    // Convert RGB to YUV420P using libavutil's swscale for testing/debugging purposes.
    // This is a non-optimized path just to verify the encoder can accept frames in the expected format.
    // The RDG readback path should produce YUV420P directly to avoid this extra conversion.
    struct SwsContextDeleter
    {
      void operator()(SwsContext* ctx) const
      {
        sws_freeContext(ctx);
      }
    };
    TUniquePtr<SwsContext, SwsContextDeleter> SwsCtx(sws_getContext(Width, Height, AV_PIX_FMT_BGRA, Width, Height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr));
    if (!SwsCtx)
    {
      UE_LOG(LogTemp, Warning, TEXT("Failed to create SwsContext for RGB->YUV conversion")); return;
    }
    // Prepare source data pointers and strides
    const uint8_t* SrcData[1] = { reinterpret_cast<const uint8_t*>(OwnedRgb.GetData()) };
    int SrcLinesize[1] = { Width * 4 };
    // Allocate destination buffers for YUV420P
    int UVW = (Width + 1) / 2;
    int UVH = (Height + 1) / 2;
    TArray<uint8> YPlane; YPlane.SetNumUninitialized(Width * Height);
    TArray<uint8> UPlane; UPlane.SetNumUninitialized(UVW * UVH);
    TArray<uint8> VPlane; VPlane.SetNumUninitialized(UVW * UVH);
    uint8_t* DstData[3] = { YPlane.GetData(), UPlane.GetData(), VPlane.GetData() };
    int DstLinesize[3] = { Width, UVW, UVW };
    int rc = sws_scale(SwsCtx.Get(), SrcData, SrcLinesize, 0, Height, DstData, DstLinesize);
    if (rc <= 0)
    {
      UE_LOG(LogTemp, Warning, TEXT("sws_scale failed with return code %d"), rc);
      return;
    }
    UE_LOG(LogTemp, Log, TEXT("RGB->YUV conversion successful: input %dx%d stride=%d output Y stride=%d U/V stride=%d"), Width, Height, SrcLinesize[0], DstLinesize[0], DstLinesize[1]);
        AVBufferRef* BufY = av_buffer_alloc(YPlane.Num());
        AVBufferRef* BufU = av_buffer_alloc(UPlane.Num());
        AVBufferRef* BufV = av_buffer_alloc(VPlane.Num());
        if (!BufY || !BufU || !BufV)
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to allocate AVBuffers for software YUV420P frame"));
            if (BufY) av_buffer_unref(&BufY);
            if (BufU) av_buffer_unref(&BufU);
            if (BufV) av_buffer_unref(&BufV);
            return;
        }

        FMemory::Memcpy(BufY->data, YPlane.GetData(), YPlane.Num());
        FMemory::Memcpy(BufU->data, UPlane.GetData(), UPlane.Num());
        FMemory::Memcpy(BufV->data, VPlane.GetData(), VPlane.Num());
        this->EncodeAndSendYuv420Buffers(BufY, BufU, BufV, Width, Height, Width, UVW, UVW, TargetTracks, LibAVState);
  });
}
