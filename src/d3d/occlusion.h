#pragma once
#include <cstdint>

// Ambient occlusion reaches the renderer as a clear with a flag no D3DCLEAR value uses, so the render thread keeps it
// in order with the draws. Its two rects carry the eight floats Occlusion sends
namespace gpu {
constexpr uint32_t kClearOcclusion = 0x100;
constexpr uint32_t kOcclusionRects = 2;
}  // namespace gpu

namespace d3d {
// m: the camera to screen matrix, row vectors, as SetTransform takes it
void Occlusion(const float* m);
}  // namespace d3d
