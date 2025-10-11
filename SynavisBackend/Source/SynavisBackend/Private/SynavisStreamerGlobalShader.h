#pragma once

// Shader wrappers disabled for simplified build iteration

#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "ShaderParameterStruct.h"
#include "RHIStaticStates.h"

class FConvertRGBAToI420PS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FConvertRGBAToI420PS);
    SHADER_USE_PARAMETER_STRUCT(FConvertRGBAToI420PS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_TEXTURE(Texture2D<float4>, InputTexture)
        SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
    END_SHADER_PARAMETER_STRUCT()

public:
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return true;
    }
};

