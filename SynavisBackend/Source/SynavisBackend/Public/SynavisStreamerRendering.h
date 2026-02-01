#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RenderResource.h"

// GPU conversion: schedules a shader pass that converts SrcRT into two render targets (Y full-res, UV half-res packed).
// On success this function returns true and provides two FRHIGPUTextureReadback objects (OutY and OutUV) which the caller
// can poll for readiness and then Map() to access the plane bytes. Both OutY and OutUV will be non-null when true is returned.
// GPU conversion: render into two RTs then transfer to CPU via render queue and ReadPixels (blocking FlushRenderingCommands).
// Outputs are planar I420 buffers (Y full-res, U and V quarter-sized each).
bool ConvertRenderTargetToI420_GPU(class UTextureRenderTarget2D* SrcRT, TArray<uint8>& OutY, TArray<uint8>& OutU, TArray<uint8>& OutV);

// Enqueue NV12 (Y + packed UV) readbacks for a render target.
// Returns true if the GPU dispatch and readbacks were enqueued. On success the function
// allocates and returns two FRHIGPUTextureReadback* objects (caller is responsible for
// letting FFmpeg free/unlock them via av_buffer free callbacks). The readbacks will
// become ready asynchronously; callers must poll Readback->IsReady() and then Lock().
// Forward declare FRHIGPUTextureReadback here.
class FRHIGPUTextureReadback;
bool EnqueueNV12ReadbackFromRenderTarget(class UTextureRenderTarget2D* SrcRT, FRHIGPUTextureReadback*& OutReadbackY, FRHIGPUTextureReadback*& OutReadbackU, FRHIGPUTextureReadback*& OutReadbackV);
