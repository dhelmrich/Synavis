#pragma once

// Compute shader wrapper disabled for simplified build iteration
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"

class FConvertRGBACompute : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FConvertRGBACompute);
    SHADER_USE_PARAMETER_STRUCT(FConvertRGBACompute, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_SRV(Texture2D<float4>, InputTexture)
        SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
        // UAVs would be bound in the dispatch call
    END_SHADER_PARAMETER_STRUCT()

public:
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return true;
    }
};
