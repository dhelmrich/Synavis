#include "SynavisStreamerComputeShader.h"

IMPLEMENT_GLOBAL_SHADER(FConvertRGBACompute, "/Plugin/SynavisBackend/ConvertRGBAToI420_CS.usf", "CSMainY", SF_Compute);
