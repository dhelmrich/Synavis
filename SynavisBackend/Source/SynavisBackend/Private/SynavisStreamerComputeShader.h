#pragma once

// Compute shader wrapper disabled for simplified build iteration
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "ShaderParameterStruct.h"

class FConvertRGBACompute : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FConvertRGBACompute);
    SHADER_USE_PARAMETER_STRUCT(FConvertRGBACompute, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        // Use RDG-aware parameter types so we can directly assign FRDGTextureRef
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputTexture)
        SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
        // UAV outputs for Y (R8) and UV (R8G8)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutY)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutUV)
        // Texture size (width, height)
        SHADER_PARAMETER(FIntPoint, TextureSize)
    END_SHADER_PARAMETER_STRUCT()

public:
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return true;
    }
};
