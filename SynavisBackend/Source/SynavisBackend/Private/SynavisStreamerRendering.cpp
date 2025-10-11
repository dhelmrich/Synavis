#include "SynavisStreamerRendering.h"
#include "Math/UnrealMathUtility.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Containers/Array.h"
#include "RHICommandList.h"
#include "RenderGraphUtils.h"
#include "SynavisStreamerGlobalShader.h"
#include "RenderUtils.h"
#include "Engine/TextureRenderTarget2D.h"
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
    // GPU path is not implemented yet - return false to indicate CPU path should be used.
    return false;
}
// Note: GPU conversion path requires a shader and RDG/RHI handling which is engine-version specific.
// The stub above intentionally returns false to fall back to CPU conversion in the streamer.
