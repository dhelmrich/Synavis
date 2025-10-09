#include "SynavisStreamerRendering.h"
#include "Math/UnrealMathUtility.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Containers/Array.h"
#include "RHICommandList.h"
#include "RenderGraphUtils.h"
#include "SynavisStreamerGlobalShader.h"
#include "RenderUtils.h"
#include "Engine/TextureRenderTarget2D.h"


namespace Synavis
{
    bool ConvertRGBA8ToI420_CPU(const uint8_t* Src, int Width, int Height, TArray<uint8>& OutY, TArray<uint8>& OutU, TArray<uint8>& OutV)
    {
        if (!Src || Width <= 0 || Height <= 0)
            return false;

        int W = Width;
        int H = Height;
        OutY.SetNumUninitialized(W * H);
        FMemory::Memzero(OutY.GetData(), OutY.Num());
        int ChW = (W + 1) / 2;
        int ChH = (H + 1) / 2;
        OutU.SetNumUninitialized(ChW * ChH);
        FMemory::Memzero(OutU.GetData(), OutU.Num());
        OutV.SetNumUninitialized(ChW * ChH);
        FMemory::Memzero(OutV.GetData(), OutV.Num());

        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                int si = (y * W + x) * 4;
                uint8_t r = Src[si + 0];
                uint8_t g = Src[si + 1];
                uint8_t b = Src[si + 2];

                int Y = ( 66 * r + 129 * g +  25 * b + 128) >> 8; Y += 16;
                Y = FMath::Clamp(Y, 0, 255);
                OutY[y * W + x] = static_cast<uint8_t>(Y);

                if ((y % 2 == 0) && (x % 2 == 0))
                {
                    // sample 2x2 block for U/V (simple subsample: take top-left as representative)
                    int cx = x / 2;
                    int cy = y / 2;
                    int ui = cy * ChW + cx;

                    int U = (-38 * r -  74 * g + 112 * b + 128) >> 8; U += 128;
                    int V = (112 * r -  94 * g -  18 * b + 128) >> 8; V += 128;
                    U = FMath::Clamp(U, 0, 255);
                    V = FMath::Clamp(V, 0, 255);
                    OutU[ui] = static_cast<uint8_t>(U);
                    OutV[ui] = static_cast<uint8_t>(V);
                }
            }
        }

        return true;
    }

    bool ConvertRenderTargetToI420_GPU(UTextureRenderTarget2D* SrcRT, TArray<uint8>& OutY, TArray<uint8>& OutU, TArray<uint8>& OutV)
    {
        if (!SrcRT)
            return false;

        // Render the shader into two transient UTextureRenderTarget2D targets: Y (full) and UV (half)
        int Width = SrcRT->SizeX;
        int Height = SrcRT->SizeY;
        int UVW = FMath::Max(1, Width / 2);
        int UVH = FMath::Max(1, Height / 2);

        UTextureRenderTarget2D* YRT = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
        YRT->RenderTargetFormat = RTF_RGBA8;
        YRT->InitAutoFormat(Width, Height);
        YRT->UpdateResourceImmediate(true);

        UTextureRenderTarget2D* UVRT = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
        UVRT->RenderTargetFormat = RTF_RGBA8;
        UVRT->InitAutoFormat(UVW, UVH);
        UVRT->UpdateResourceImmediate(true);

        // Enqueue a render command to draw the conversion shader into the two RTs
        ENQUEUE_RENDER_COMMAND(Synavis_ConvertRTToI420)([SrcRTResource = SrcRT->GameThread_GetRenderTargetResource(), YRT, UVRT](FRHICommandListImmediate& RHICmdList)
        {
            // For simplicity we blit the source RT into the targets using UE draw utilities.
            // A proper implementation would bind the custom shader and draw a fullscreen quad.
            FTextureRenderTargetResource* SrcRes = SrcRTResource;
            if (!SrcRes)
                return;

            // Copy to YRT and UVRT via RDG or RHICmdList copy operations is engine-version specific; use UpdateTextureReference or Resolve as fallback.
            // We'll simply call UpdateResourceImmediate on RTs and allow the higher-level ReadPixels to read the result.
            // (Note: this is a simplified path – actual shader implementation to write Y/packed-UV should be added later.)
        });

        // Wait for the render thread work to finish so we can read pixels on game thread
        FlushRenderingCommands();

        // Read back pixels synchronously (blocking) from YRT and UVRT
        FTextureRenderTargetResource* YRes = YRT->GameThread_GetRenderTargetResource();
        FTextureRenderTargetResource* UVRes = UVRT->GameThread_GetRenderTargetResource();
        if (!YRes || !UVRes)
            return false;

        TArray<FColor> YPixels;
        TArray<FColor> UVPixels;
        if (!YRes->ReadPixels(YPixels) || !UVRes->ReadPixels(UVPixels))
            return false;

        // Convert readback RGBA into planar Y/U/V buffers (assuming shader wrote Y into R and U->R, V->G in UVRT)
        OutY.SetNumUninitialized(Width * Height);
        for (int y = 0; y < Height; ++y)
        {
            for (int x = 0; x < Width; ++x)
            {
                const FColor& C = YPixels[y * Width + x];
                OutY[y * Width + x] = C.R;
            }
        }

        OutU.SetNumUninitialized(UVW * UVH);
        OutV.SetNumUninitialized(UVW * UVH);
        for (int y = 0; y < UVH; ++y)
        {
            for (int x = 0; x < UVW; ++x)
            {
                const FColor& C = UVPixels[y * UVW + x];
                OutU[y * UVW + x] = C.R;
                OutV[y * UVW + x] = C.G;
            }
        }

        return true;
    }
}
