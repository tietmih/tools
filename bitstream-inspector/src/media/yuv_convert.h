#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/quality.h"
#include "core/yuv.h"

namespace bs {

// Converts a planar YUV frame to top-down BGRA using an explicit matrix and
// range. The source layout is inferred from chroma_format and bit_depth.
bool yuvToBgra(const YuvFrameView& frame, int matrix, bool full_range,
               std::vector<uint8_t>& bgra, std::string& error);

// Converts a planar YUV frame to another planar layout (chroma sampling and
// bit depth), writing tightly packed little-endian samples with low-aligned
// bits for >8-bit output. No colour-matrix conversion is applied.
bool yuvToYuv(const YuvFrameView& frame, YuvLayout target,
              std::vector<uint8_t>& out, std::string& error);

}  // namespace bs
