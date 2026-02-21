#include "SynavisStreamerRendering.h"
#include "Math/UnrealMathUtility.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
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

bool ConvertRenderTargetToI420_GPU(UTextureRenderTarget2D* SrcRT, TArray<uint8>& OutY, TArray<uint8>& OutU, TArray<uint8>& OutV)
{
    if (!SrcRT)
        return false;

    // Create RDG graph and dispatch compute shader to output I420: Y plane (full res) + separate U and V (half res)
    FTextureRenderTargetResource* RTResource = SrcRT->GameThread_GetRenderTargetResource();
    if (!RTResource)
        return false;

    const int Width = SrcRT->SizeX;
    const int Height = SrcRT->SizeY;
    if (Width <= 0 || Height <= 0)
        return false;

    // Prepare GPU readbacks
    TUniquePtr<FRHIGPUTextureReadback> ReadbackY = MakeUnique<FRHIGPUTextureReadback>(TEXT("Synavis_Y_Readback"));
    TUniquePtr<FRHIGPUTextureReadback> ReadbackU = MakeUnique<FRHIGPUTextureReadback>(TEXT("Synavis_U_Readback"));
    TUniquePtr<FRHIGPUTextureReadback> ReadbackV = MakeUnique<FRHIGPUTextureReadback>(TEXT("Synavis_V_Readback"));

    // Run render graph on render thread
    ENQUEUE_RENDER_COMMAND(Synavis_ConvertRTToI420)([RTTexture = RTResource->GetRenderTargetTexture(), Width, Height, ReadbackYPtr = ReadbackY.Get(), ReadbackUPtr = ReadbackU.Get(), ReadbackVPtr = ReadbackV.Get()](FRHICommandListImmediate& RHICmdList)
    {
      // Build RDG
      FRDGBuilder GraphBuilder(RHICmdList);

      // Register external texture (source render target) with RDG
      FRDGTextureRef RDGInput = RegisterExternalTexture(GraphBuilder, RTTexture, TEXT("Synavis_Input"));

    FRDGTextureDesc DescY = FRDGTextureDesc::Create2D(FIntPoint(Width, Height), PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
      FRDGTextureRef RDGY = GraphBuilder.CreateTexture(DescY, TEXT("Synavis_Y"));

    FRDGTextureDesc DescU = FRDGTextureDesc::Create2D(FIntPoint((Width + 1) / 2, (Height + 1) / 2), PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
    FRDGTextureRef RDGU = GraphBuilder.CreateTexture(DescU, TEXT("Synavis_U"));

    FRDGTextureDesc DescV = FRDGTextureDesc::Create2D(FIntPoint((Width + 1) / 2, (Height + 1) / 2), PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
    FRDGTextureRef RDGV = GraphBuilder.CreateTexture(DescV, TEXT("Synavis_V"));

    // Ensure a global shader map exists for the current feature level and obtain the compute shader via TShaderMapRef.
    const auto* GlobalMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
    checkf(GlobalMap, TEXT("GlobalShaderMap null for feature level %d"), (int32)GMaxRHIFeatureLevel);

      // Setup compute shader parameters and add pass
      TShaderMapRef<FConvertRGBACompute> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
      FConvertRGBACompute::FParameters* PassParameters = GraphBuilder.AllocParameters<FConvertRGBACompute::FParameters>();
      PassParameters->InputTexture = RDGInput;
      PassParameters->LinearSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
      // Create UAVs from the RDG textures and assign
    PassParameters->OutY = GraphBuilder.CreateUAV(RDGY);
    PassParameters->OutU = GraphBuilder.CreateUAV(RDGU);
    PassParameters->OutV = GraphBuilder.CreateUAV(RDGV);
      PassParameters->TextureSize = FIntPoint(Width, Height);

      // Add a compute pass and dispatch via helper
      GraphBuilder.AddPass(
          RDG_EVENT_NAME("SynavisConvertToI420"),
          PassParameters,
          ERDGPassFlags::Compute,
          [PassParameters, ComputeShader, Width, Height](FRHIComputeCommandList& RHICmdListInner)
          {
              FComputeShaderUtils::Dispatch(RHICmdListInner, ComputeShader, *PassParameters, FIntVector((Width + 15) / 16, (Height + 15) / 16, 1));
          }
      );

      // Enqueue readbacks
    AddEnqueueCopyPass(GraphBuilder, ReadbackYPtr, RDGY);
    AddEnqueueCopyPass(GraphBuilder, ReadbackUPtr, RDGU);
    AddEnqueueCopyPass(GraphBuilder, ReadbackVPtr, RDGV);

            GraphBuilder.Execute();
            UE_LOG(LogTemp, Verbose, TEXT("Synavis: ConvertRTToI420 RDG executed"));
    });

    // Hint for Option B (pixel-shader fullscreen pass):
    // - Implement a pixel shader that writes Y to RT0 and packed UV to RT1 (I420) if desired.
    // - Create two transient render targets via RDG with PF_R8 and PF_R8G8.
    // - Use AddDrawScreenPass or a full-screen draw call to render a quad using the pixel shader.
    // - Enqueue readbacks similarly with AddEnqueueCopyPass for each RT.
    // This path can be simpler to implement in some engine versions where compute binding semantics differ.

    // Now we need to wait until readbacks are ready on CPU; do a simple poll/wait loop on game thread with small sleeps is not ideal.
    // For simplicity we will spin-wait with a timeout. In production you'd integrate async callbacks.
    const double TimeoutSeconds = 0.5; // 500ms
    double StartTime = FPlatformTime::Seconds();
    bool bReadyY = false, bReadyU = false, bReadyV = false;
    while (FPlatformTime::Seconds() - StartTime < TimeoutSeconds)
    {
        if (!bReadyY && ReadbackY->IsReady()) bReadyY = true;
        if (!bReadyU && ReadbackU->IsReady()) bReadyU = true;
        if (!bReadyV && ReadbackV->IsReady()) bReadyV = true;
        if (bReadyY && bReadyU && bReadyV) break;
        FPlatformProcess::Sleep(0.001f);
    }

    if (!bReadyY || !bReadyU || !bReadyV)
    {
        return false;
    }

    // Lock and copy planes
    int YSize = Width * Height;
    int UVWidth = (Width + 1) / 2;
    int UVHeight = (Height + 1) / 2;
    int UVSize = UVWidth * UVHeight;

    OutY.SetNumUninitialized(YSize);
    OutU.SetNumUninitialized(UVSize);
    OutV.SetNumUninitialized(UVSize);

    // PF_R8_UINT -> 1 byte per texel; CPU readback returns rows of bytes (low byte contains sample)
    uint32 RowPitchBytes = 0;
    void* YData = ReadbackY->Lock(RowPitchBytes);
    if (!YData)
    {
        return false;
    }
    for (int y = 0; y < Height; ++y)
    {
        uint8_t* srcRow = (uint8_t*)YData + (size_t)y * RowPitchBytes;
        memcpy(OutY.GetData() + y * Width, srcRow, Width);
    }
    ReadbackY->Unlock();

    uint32 URowPitchBytes = 0;
    void* UData = ReadbackU->Lock(URowPitchBytes);
    if (!UData)
    {
        return false;
    }
    for (int y = 0; y < UVHeight; ++y)
    {
        uint8_t* srcRow = (uint8_t*)UData + (size_t)y * URowPitchBytes;
        memcpy(OutU.GetData() + y * UVWidth, srcRow, UVWidth);
    }
    ReadbackU->Unlock();

    uint32 VRowPitchBytes = 0;
    void* VData = ReadbackV->Lock(VRowPitchBytes);
    if (!VData)
    {
        return false;
    }
    for (int y = 0; y < UVHeight; ++y)
    {
        uint8_t* srcRow = (uint8_t*)VData + (size_t)y * VRowPitchBytes;
        memcpy(OutV.GetData() + y * UVWidth, srcRow, UVWidth);
    }
    ReadbackV->Unlock();

    return true;
}
// Note: GPU conversion path requires a shader and RDG/RHI handling which is engine-version specific.
// The stub above intentionally returns false to fall back to CPU conversion in the streamer.

bool EnqueueI420ReadbackFromRenderTarget(UTextureRenderTarget2D* SrcRT, FRHIGPUTextureReadback*& OutReadbackY, FRHIGPUTextureReadback*& OutReadbackU, FRHIGPUTextureReadback*& OutReadbackV)
{
    OutReadbackY = nullptr;
    OutReadbackU = nullptr;
    OutReadbackV = nullptr;
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
    FRHIGPUTextureReadback* ReadbackU = new FRHIGPUTextureReadback(TEXT("Synavis_U_Readback"));
    FRHIGPUTextureReadback* ReadbackV = new FRHIGPUTextureReadback(TEXT("Synavis_V_Readback"));

    // Enqueue on render thread
    ENQUEUE_RENDER_COMMAND(Synavis_EnqueueI420Readback)([RTTexture = RTResource->GetRenderTargetTexture(), Width, Height, ReadbackY, ReadbackU, ReadbackV](FRHICommandListImmediate& RHICmdList)
    {
        UE_LOG(LogTemp, Verbose, TEXT("Synavis: EnqueueI420ReadbackFromRenderTarget - dispatching RDG for %dx%d"), Width, Height);
        FRDGBuilder GraphBuilder(RHICmdList);
        FRDGTextureRef RDGInput = RegisterExternalTexture(GraphBuilder, RTTexture, TEXT("Synavis_Input"));

        FRDGTextureDesc DescY = FRDGTextureDesc::Create2D(FIntPoint(Width, Height), PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
        FRDGTextureRef RDGY = GraphBuilder.CreateTexture(DescY, TEXT("Synavis_Y"));

        FRDGTextureDesc DescU = FRDGTextureDesc::Create2D(FIntPoint((Width + 1) / 2, (Height + 1) / 2), PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
        FRDGTextureRef RDGU = GraphBuilder.CreateTexture(DescU, TEXT("Synavis_U"));

        FRDGTextureDesc DescV = FRDGTextureDesc::Create2D(FIntPoint((Width + 1) / 2, (Height + 1) / 2), PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
        FRDGTextureRef RDGV = GraphBuilder.CreateTexture(DescV, TEXT("Synavis_V"));

        const auto* GlobalMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
        checkf(GlobalMap, TEXT("GlobalShaderMap null for feature level %d"), (int32)GMaxRHIFeatureLevel);

        TShaderMapRef<FConvertRGBACompute> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FConvertRGBACompute::FParameters* PassParameters = GraphBuilder.AllocParameters<FConvertRGBACompute::FParameters>();
        PassParameters->InputTexture = RDGInput;
        PassParameters->LinearSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
        PassParameters->OutY = GraphBuilder.CreateUAV(RDGY);
        PassParameters->OutU = GraphBuilder.CreateUAV(RDGU);
        PassParameters->OutV = GraphBuilder.CreateUAV(RDGV);
        PassParameters->TextureSize = FIntPoint(Width, Height);

        GraphBuilder.AddPass(RDG_EVENT_NAME("SynavisConvertToI420"), PassParameters, ERDGPassFlags::Compute,
            [PassParameters, ComputeShader, Width, Height](FRHIComputeCommandList& RHICmdListInner)
            {
                FComputeShaderUtils::Dispatch(RHICmdListInner, ComputeShader, *PassParameters, FIntVector((Width + 15) / 16, (Height + 15) / 16, 1));
            }
        );

        AddEnqueueCopyPass(GraphBuilder, ReadbackY, RDGY);
        AddEnqueueCopyPass(GraphBuilder, ReadbackU, RDGU);
        AddEnqueueCopyPass(GraphBuilder, ReadbackV, RDGV);

        GraphBuilder.Execute();
        UE_LOG(LogTemp, Verbose, TEXT("Synavis: EnqueueI420ReadbackFromRenderTarget - RDG executed and readbacks enqueued"));
    });

    OutReadbackY = ReadbackY;
    OutReadbackU = ReadbackU;
    OutReadbackV = ReadbackV;
    return true;
}

