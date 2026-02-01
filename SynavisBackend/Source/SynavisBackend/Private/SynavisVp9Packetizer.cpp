#include "SynavisVp9Packetizer.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformProcess.h"
#include "Logging/LogMacros.h"

THIRD_PARTY_INCLUDES_START
#include "rtc/rtc.h"
THIRD_PARTY_INCLUDES_END

FVp9PacketizerWorker::FVp9PacketizerWorker(USynavisVp9Packetizer* InOwner)
    : Owner(InOwner) {}

uint32 FVp9PacketizerWorker::Run()
{
    while (!Owner->bShouldExit.Load())
    {
        FEncodedVp9Frame Frame;
        if (Owner->FrameQueue && Owner->FrameQueue->Dequeue(Frame))
        {
            Owner->ProcessFrame(Frame);
        }
        else
        {
            FPlatformProcess::Sleep(0.001f);
        }
    }
    return 0;
}

void FVp9PacketizerWorker::Stop()
{
    Owner->bShouldExit = true;
}

USynavisVp9Packetizer::USynavisVp9Packetizer()
{
    PacketBuffers.SetNum(MaxPacketsPerFrame * PacketBufSize);
    PacketPool.SetNum(MaxPacketsPerFrame);
    FrameQueue = new TCircularQueue<FEncodedVp9Frame>(128);
}

void USynavisVp9Packetizer::Initialize(int32 InPayloadType, uint32 InSSRC, uint16 InMaxPayloadSize)
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
        Worker = new FVp9PacketizerWorker(this);
        WorkerThread = FRunnableThread::Create(Worker, TEXT("SynavisVp9PacketizerWorker"));
    }
}

void USynavisVp9Packetizer::SendFrame(TArray<uint8>&& InFrameBuffer, uint32 FrameTimestamp90khz, uint32 FrameDuration90khz, const TArray<int32>& TracksToSend)
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
}

void USynavisVp9Packetizer::Shutdown()
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

static FORCEINLINE void WriteBE16(uint8* Buf, uint16 Val)
{
    Buf[0] = (Val >> 8) & 0xFF;
    Buf[1] = Val & 0xFF;
}
static FORCEINLINE void WriteBE32(uint8* Buf, uint32 Val)
{
    Buf[0] = (Val >> 24) & 0xFF;
    Buf[1] = (Val >> 16) & 0xFF;
    Buf[2] = (Val >> 8) & 0xFF;
    Buf[3] = Val & 0xFF;
}

static int BuildRtpVp9Packet(uint8* OutBuf, int Remaining, const uint8* FrameData, bool IsFirst, bool IsLast, uint16 SeqNum, uint32 Timestamp, uint32 SSRC, uint8 PayloadType, uint16 MaxPayloadSize)
{
    // RTP header (12 bytes)
    OutBuf[0] = 0x80; // V=2, P=0, X=0, CC=0
    OutBuf[1] = PayloadType & 0x7F; // M=0, PT
    if (IsLast) OutBuf[1] |= 0x80; // M=1 for last packet
    WriteBE16(OutBuf + 2, SeqNum);
    WriteBE32(OutBuf + 4, Timestamp);
    WriteBE32(OutBuf + 8, SSRC);
    // VP9 payload descriptor (RFC 9628 §5.2, 1 byte, non-flexible)
    OutBuf[12] = 0;
    if (IsFirst) OutBuf[12] |= 0x08; // B=1
    if (IsLast) OutBuf[12] |= 0x04; // E=1
    // Copy payload
    int PayloadSize = FMath::Min(Remaining, (int)MaxPayloadSize - 1); // 1 byte for VP9 desc
    FMemory::Memcpy(OutBuf + 13, FrameData, PayloadSize);
    return 13 + PayloadSize;
}

void USynavisVp9Packetizer::ProcessFrame(const FEncodedVp9Frame& Frame)
{
    int32 PacketIdx = 0;
    uint32 FrameSize = Frame.Buffer.Num();
    const uint8* FrameData = Frame.Buffer.GetData();
    int32 Remaining = FrameSize;
    bool IsFirst = true;
    while (Remaining > 0 && PacketIdx < MaxPacketsPerFrame)
    {
        bool IsLast = (Remaining <= (MaxPayloadSize - 1));
        uint8* PacketBuf = &PacketBuffers[PacketIdx * PacketBufSize];
        int Written = BuildRtpVp9Packet(PacketBuf, Remaining, FrameData, IsFirst, IsLast, SequenceNumber, Frame.Timestamp90khz, SSRC, PayloadType, MaxPayloadSize);
        PacketPool[PacketIdx] = {PacketBuf, static_cast<uint16>(Written)};
        int PayloadSize = FMath::Min(Remaining, (int)MaxPayloadSize - 1);
        Remaining -= PayloadSize;
        FrameData += PayloadSize;
        IsFirst = false;
        SequenceNumber++;
        PacketIdx++;
    }
    UE_LOG(LogTemp, Verbose, TEXT("VP9 Packetized %d bytes into %d RTP packets (TS=%u)"), FrameSize, PacketIdx, Frame.Timestamp90khz);
    for (int32 i = 0; i < PacketIdx; ++i)
    {
        const FVp9PacketDesc& Packet = PacketPool[i];
        for (int32 Track : CurrentTracksToSend)
        {
            if (rtcIsOpen(Track))
            {
                rtcSendMessage(Track, reinterpret_cast<const char*>(Packet.Data), Packet.Len);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("VP9: Track %d closed, drop packet"), Track);
            }
        }
    }
    PictureId++;
}
