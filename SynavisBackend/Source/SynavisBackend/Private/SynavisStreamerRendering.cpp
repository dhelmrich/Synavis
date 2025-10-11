#include "SynavisStreamerRendering.h"
#include "Math/UnrealMathUtility.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Containers/Array.h"
#include "RHICommandList.h"
#include "RenderGraphUtils.h"
#include "SynavisStreamerGlobalShader.h"
#include "RenderUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "SynavisStreamerComputeShader.h"
#include "ShaderParameterUtils.h"
#include "RHIStaticStates.h"
// We need access to the USynavisStreamer declaration (libav includes live in SynavisStreamer.cpp)
#include "SynavisStreamer.h"
bool ConvertRGBA8ToI420_CPU(const uint8_t* Src, int Width, int Height, TArray<uint8>& OutY, TArray<uint8>& OutU, TArray<uint8>& OutV)
{
    if (!Src || Width <= 0 || Height <= 0)
    {
        return false;
    }

    const int YSize = Width * Height;
    const int UVWidth = (Width + 1) / 2;
    const int UVHeight = (Height + 1) / 2;
    const int UVSize = UVWidth * UVHeight;

    OutY.SetNumUninitialized(YSize);
    OutU.SetNumUninitialized(UVSize);
    OutV.SetNumUninitialized(UVSize);

    // Simple naive conversion
    for (int y = 0; y < Height; ++y)
    {
        for (int x = 0; x < Width; ++x)
        {
            const int srcIndex = (y * Width + x) * 4;
            const uint8_t R = Src[srcIndex + 0];
            const uint8_t G = Src[srcIndex + 1];
            const uint8_t B = Src[srcIndex + 2];

            // ITU-R BT.601 conversion
            const int Yval = ( ( 66 * R + 129 * G +  25 * B + 128) >> 8) + 16;
            const int Uval = ( (-38 * R -  74 * G + 112 * B + 128) >> 8) + 128;
            const int Vval = ( (112 * R -  94 * G -  18 * B + 128) >> 8) + 128;

            OutY[y * Width + x] = static_cast<uint8_t>(FMath::Clamp(Yval, 0, 255));
            // chroma at subsampled coords
            if ((x % 2) == 0 && (y % 2) == 0)
            {
                const int uvIndex = (y / 2) * UVWidth + (x / 2);
                OutU[uvIndex] = static_cast<uint8_t>(FMath::Clamp(Uval, 0, 255));
                OutV[uvIndex] = static_cast<uint8_t>(FMath::Clamp(Vval, 0, 255));
            }
        }
    }

    return true;
}

bool ConvertRenderTargetToI420_GPU(UTextureRenderTarget2D* SrcRT, TArray<uint8>& OutY, TArray<uint8>& OutU, TArray<uint8>& OutV)
{
    if (!SrcRT)
        return false;

    // Create RDG graph and dispatch compute shader to output NV12: Y plane (full res) + interleaved UV (half res)
    FTextureRenderTargetResource* RTResource = SrcRT->GameThread_GetRenderTargetResource();
    if (!RTResource)
        return false;

    const int Width = SrcRT->SizeX;
    const int Height = SrcRT->SizeY;
    if (Width <= 0 || Height <= 0)
        return false;

    // Prepare GPU readbacks
    TUniquePtr<FRHIGPUTextureReadback> ReadbackY = MakeUnique<FRHIGPUTextureReadback>(TEXT("Synavis_Y_Readback"));
    TUniquePtr<FRHIGPUTextureReadback> ReadbackUV = MakeUnique<FRHIGPUTextureReadback>(TEXT("Synavis_UV_Readback"));

    // Run render graph on render thread
    ENQUEUE_RENDER_COMMAND(Synavis_ConvertRTToNV12)([RTTexture = RTResource->GetRenderTargetTexture(), Width, Height, ReadbackYPtr = ReadbackY.Get(), ReadbackUVPtr = ReadbackUV.Get()](FRHICommandListImmediate& RHICmdList)
    {
            // Build RDG
            FRDGBuilder GraphBuilder(RHICmdList);

            // Register external texture (source render target) with RDG
            FRDGTextureRef RDGInput = RegisterExternalTexture(GraphBuilder, RTTexture, TEXT("Synavis_Input"));

        FRDGTextureDesc DescY = FRDGTextureDesc::Create2D(FIntPoint(Width, Height), PF_R8, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
        FRDGTextureRef RDGY = GraphBuilder.CreateTexture(DescY, TEXT("Synavis_Y"));

        FRDGTextureDesc DescUV = FRDGTextureDesc::Create2D(FIntPoint((Width + 1) / 2, (Height + 1) / 2), PF_R8G8, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
        FRDGTextureRef RDGUV = GraphBuilder.CreateTexture(DescUV, TEXT("Synavis_UV"));

        // Setup compute shader parameters and add pass
            TShaderMapRef<FConvertRGBACompute> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            FConvertRGBACompute::FParameters* PassParameters = GraphBuilder.AllocParameters<FConvertRGBACompute::FParameters>();
            PassParameters->InputTexture = RDGInput;
            PassParameters->LinearSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
            // Create UAVs from the RDG textures and assign
            PassParameters->OutY = GraphBuilder.CreateUAV(RDGY);
            PassParameters->OutUV = GraphBuilder.CreateUAV(RDGUV);
            PassParameters->TextureSize = FIntPoint(Width, Height);

            // Add a compute pass and dispatch via helper
            GraphBuilder.AddPass(
                RDG_EVENT_NAME("SynavisConvertToNV12"),
                PassParameters,
                ERDGPassFlags::Compute,
                [PassParameters, ComputeShader, Width, Height](FRHIComputeCommandList& RHICmdListInner)
                {
                    FComputeShaderUtils::Dispatch(RHICmdListInner, ComputeShader, *PassParameters, FIntVector((Width + 15) / 16, (Height + 15) / 16, 1));
                }
            );

    // Enqueue readbacks
        AddEnqueueCopyPass(GraphBuilder, ReadbackYPtr, RDGY);
        AddEnqueueCopyPass(GraphBuilder, ReadbackUVPtr, RDGUV);

        GraphBuilder.Execute();
    });

    // Hint for Option B (pixel-shader fullscreen pass):
    // - Implement a pixel shader that writes Y to RT0 and packed UV to RT1.
    // - Create two transient render targets via RDG with PF_R8 and PF_R8G8.
    // - Use AddDrawScreenPass or a full-screen draw call to render a quad using the pixel shader.
    // - Enqueue readbacks similarly with AddEnqueueCopyPass for each RT.
    // This path can be simpler to implement in some engine versions where compute binding semantics differ.

    // Now we need to wait until readbacks are ready on CPU; do a simple poll/wait loop on game thread with small sleeps is not ideal.
    // For simplicity we will spin-wait with a timeout. In production you'd integrate async callbacks.
    const double TimeoutSeconds = 0.5; // 500ms
    double StartTime = FPlatformTime::Seconds();
    bool bReadyY = false, bReadyUV = false;
    while (FPlatformTime::Seconds() - StartTime < TimeoutSeconds)
    {
        if (!bReadyY && ReadbackY->IsReady()) bReadyY = true;
        if (!bReadyUV && ReadbackUV->IsReady()) bReadyUV = true;
        if (bReadyY && bReadyUV) break;
        FPlatformProcess::Sleep(0.001f);
    }

    if (!bReadyY || !bReadyUV)
    {
        return false;
    }

    // Lock, copy and split UV into U and V planes
    int YSize = Width * Height;
    int UVWidth = (Width + 1) / 2;
    int UVHeight = (Height + 1) / 2;
    int UVSize = UVWidth * UVHeight;

    OutY.SetNumUninitialized(YSize);
    OutU.SetNumUninitialized(UVSize);
    OutV.SetNumUninitialized(UVSize);

    int RowPitchPixels = 0;
    void* YData = ReadbackY->Lock(RowPitchPixels);
    if (!YData)
    {
        ReadbackY->Unlock();
        ReadbackUV->Unlock();
        return false;
    }
    // RowPitchPixels is pitch in pixels for R8
    for (int y = 0; y < Height; ++y)
    {
        uint8_t* srcRow = (uint8_t*)YData + y * RowPitchPixels;
        memcpy(OutY.GetData() + y * Width, srcRow, Width);
    }
    ReadbackY->Unlock();

    int UVRowPitchPixels = 0;
    void* UVData = ReadbackUV->Lock(UVRowPitchPixels);
    if (!UVData)
    {
        ReadbackUV->Unlock();
        return false;
    }
    // UVData contains packed U,V in two bytes per pixel (R8G8)
    for (int y = 0; y < UVHeight; ++y)
    {
        uint8_t* srcRow = (uint8_t*)UVData + y * UVRowPitchPixels * 2; // pitch in pixels * 2 bytes
        for (int x = 0; x < UVWidth; ++x)
        {
            uint8_t U = srcRow[x * 2 + 0];
            uint8_t V = srcRow[x * 2 + 1];
            int idx = y * UVWidth + x;
            OutU[idx] = U;
            OutV[idx] = V;
        }
    }
    ReadbackUV->Unlock();

    return true;
}
// Note: GPU conversion path requires a shader and RDG/RHI handling which is engine-version specific.
// The stub above intentionally returns false to fall back to CPU conversion in the streamer.

bool EnqueueNV12ReadbackFromRenderTarget(UTextureRenderTarget2D* SrcRT, FRHIGPUTextureReadback*& OutReadbackY, FRHIGPUTextureReadback*& OutReadbackUV)
{
    OutReadbackY = nullptr;
    OutReadbackUV = nullptr;
    if (!SrcRT)
        return false;

    FTextureRenderTargetResource* RTResource = SrcRT->GameThread_GetRenderTargetResource();
    if (!RTResource)
        return false;

    const int Width = SrcRT->SizeX;
    const int Height = SrcRT->SizeY;
    if (Width <= 0 || Height <= 0)
        return false;

    // Allocate readbacks on heap and return ownership to caller; they must be freed later.
    FRHIGPUTextureReadback* ReadbackY = new FRHIGPUTextureReadback(TEXT("Synavis_Y_Readback"));
    FRHIGPUTextureReadback* ReadbackUV = new FRHIGPUTextureReadback(TEXT("Synavis_UV_Readback"));

    // Enqueue on render thread
    ENQUEUE_RENDER_COMMAND(Synavis_EnqueueNV12Readback)([RTTexture = RTResource->GetRenderTargetTexture(), Width, Height, ReadbackY, ReadbackUV](FRHICommandListImmediate& RHICmdList)
    {
        FRDGBuilder GraphBuilder(RHICmdList);
        FRDGTextureRef RDGInput = RegisterExternalTexture(GraphBuilder, RTTexture, TEXT("Synavis_Input"));

        FRDGTextureDesc DescY = FRDGTextureDesc::Create2D(FIntPoint(Width, Height), PF_R8, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
        FRDGTextureRef RDGY = GraphBuilder.CreateTexture(DescY, TEXT("Synavis_Y"));

        FRDGTextureDesc DescUV = FRDGTextureDesc::Create2D(FIntPoint((Width + 1) / 2, (Height + 1) / 2), PF_R8G8, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
        FRDGTextureRef RDGUV = GraphBuilder.CreateTexture(DescUV, TEXT("Synavis_UV"));

        TShaderMapRef<FConvertRGBACompute> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FConvertRGBACompute::FParameters* PassParameters = GraphBuilder.AllocParameters<FConvertRGBACompute::FParameters>();
        PassParameters->InputTexture = RDGInput;
        PassParameters->LinearSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
        PassParameters->OutY = GraphBuilder.CreateUAV(RDGY);
        PassParameters->OutUV = GraphBuilder.CreateUAV(RDGUV);
        PassParameters->TextureSize = FIntPoint(Width, Height);

        GraphBuilder.AddPass(RDG_EVENT_NAME("SynavisConvertToNV12"), PassParameters, ERDGPassFlags::Compute,
            [PassParameters, ComputeShader, Width, Height](FRHIComputeCommandList& RHICmdListInner)
            {
                FComputeShaderUtils::Dispatch(RHICmdListInner, ComputeShader, *PassParameters, FIntVector((Width + 15) / 16, (Height + 15) / 16, 1));
            }
        );

        AddEnqueueCopyPass(GraphBuilder, ReadbackY, RDGY);
        AddEnqueueCopyPass(GraphBuilder, ReadbackUV, RDGUV);

        GraphBuilder.Execute();
    });

    OutReadbackY = ReadbackY;
    OutReadbackUV = ReadbackUV;
    return true;
}

// Note: EncodeNV12ReadbackAndSend is implemented in SynavisStreamer.cpp where libav and rtc headers
// are available; this file only provides the EnqueueNV12ReadbackFromRenderTarget helper.
