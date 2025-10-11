#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RenderResource.h"

// Simple CPU conversion: Convert an RGBA8 buffer (uint8_t per channel) into I420 planes.
// Input: Src - pointer to RGBA bytes (width*height*4)
// Output: YPlane length = width*height
//         UPlane length = (width/2)*(height/2)
//         VPlane length = (width/2)*(height/2)
// Returns true on success.
bool ConvertRGBA8ToI420_CPU(const uint8_t* Src, int Width, int Height, TArray<uint8>& OutY, TArray<uint8>& OutU, TArray<uint8>& OutV);

// GPU stub API: schedule a render/dispatch that will fill provided render targets (Y and UV packed)
// For now this is a stub that returns false (not implemented). When implemented it should enqueue
// the render commands and return true when the GPU path was used.
// GPU conversion: schedules a shader pass that converts SrcRT into two render targets (Y full-res, UV half-res packed).
// On success this function returns true and provides two FRHIGPUTextureReadback objects (OutY and OutUV) which the caller
// can poll for readiness and then Map() to access the plane bytes. Both OutY and OutUV will be non-null when true is returned.
// GPU conversion: render into two RTs then transfer to CPU via render queue and ReadPixels (blocking FlushRenderingCommands).
// Outputs are planar I420 buffers (Y full-res, U and V quarter-sized each).
bool ConvertRenderTargetToI420_GPU(class UTextureRenderTarget2D* SrcRT, TArray<uint8>& OutY, TArray<uint8>& OutU, TArray<uint8>& OutV);
