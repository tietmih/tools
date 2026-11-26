#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/h264.h"
#include "core/h265.h"

namespace bs {

struct ConformanceIssue {
  std::string code;
  std::string message;
};

std::vector<ConformanceIssue> checkH264Conformance(const H264Sps& sps,
                                                   double fps,
                                                   uint64_t bitrate);
std::vector<ConformanceIssue> checkH265Conformance(const H265Sps& sps,
                                                   double fps,
                                                   uint64_t bitrate);

}  // namespace bs
