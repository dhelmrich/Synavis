#include "SynavisStreamerGlobalShader.h"
#include "PipelineStateCache.h"
#include "RenderGraphUtils.h"
#include "GlobalShader.h"

IMPLEMENT_GLOBAL_SHADER(FConvertRGBAToI420PS, "/Plugin/SynavisBackend/ConvertRGBAToI420.usf", "PSMain", SF_Pixel);

// Note: PSMain writes both SV_TARGET0 (Y) and SV_TARGET1 (UV packed). We draw a fullscreen quad into two RTs.
