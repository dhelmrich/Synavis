#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RenderResource.h"

// GPU conversion: schedules a shader pass that converts SrcRT into three render targets
// (Y full-res, U half-res, V half-res). On success this function returns true and
// provides three FRHIGPUTextureReadback objects (OutY, OutU and OutV) which the caller
// can poll for readiness and then Lock() to access the plane bytes. Outputs are planar
// I420 buffers (Y full-res, U and V half-resolution each).
bool ConvertRenderTargetToI420_GPU(class UTextureRenderTarget2D* SrcRT, TArray<uint8>& OutY, TArray<uint8>& OutU, TArray<uint8>& OutV);

// Enqueue I420 (Y + U + V planar) readbacks for a render target.
// Returns true if the GPU dispatch and readbacks were enqueued. On success the function
// allocates and returns three FRHIGPUTextureReadback* objects (caller is responsible for
// letting FFmpeg free/unlock them via av_buffer free callbacks). The readbacks will
// become ready asynchronously; callers must poll Readback->IsReady() and then Lock().
// Forward declare FRHIGPUTextureReadback here.
class FRHIGPUTextureReadback;
bool EnqueueI420ReadbackFromRenderTarget(class UTextureRenderTarget2D* SrcRT, FRHIGPUTextureReadback*& OutReadbackY, FRHIGPUTextureReadback*& OutReadbackU, FRHIGPUTextureReadback*& OutReadbackV);
