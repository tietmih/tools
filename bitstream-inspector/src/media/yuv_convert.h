#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/quality.h"

namespace bs {

// Converts a planar YUV frame to top-down BGRA using an explicit matrix and
// range. The source layout is inferred from chroma_format and bit_depth.
bool yuvToBgra(const YuvFrameView& frame, int matrix, bool full_range,
               std::vector<uint8_t>& bgra, std::string& error);

}  // namespace bs
