#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bs {

struct CpbParams {
  uint64_t bit_rate = 0;  // bit/s
  uint64_t cpb_size = 0;  // bits
  bool cbr = false;
  int cpb_index = 0;
  int delay_bits = 0;  // cpb_removal_delay_length_minus1 + 1
  std::string source;
};

struct CpbInitial {
  bool present = false;
  uint32_t initial_delay = 0;   // 90 kHz ticks
  uint32_t initial_offset = 0;  // 90 kHz ticks
  bool estimated = false;
};

struct CpbAuInput {
  uint64_t bits = 0;
  uint32_t removal_delay = 0;  // H.264 cpb_removal_delay, H.265 minus1 + 1
  bool has_removal_delay = false;
  bool segment_start = false;
  double fallback_interval_sec = 0.0;
};

struct CpbSample {
  int frame_index = -1;
  uint64_t au_bits = 0;
  uint32_t removal_delay = 0;
  bool has_removal_delay = false;
  double removal_time_sec = 0.0;
  double occupancy_before_bits = 0.0;
  double occupancy_after_bits = 0.0;
  double occupancy_ratio = 0.0;
  bool underflow = false;
  bool overflow = false;
  bool estimated = false;
  bool segment_start = false;
};

struct CpbModelResult {
  bool valid = false;
  std::string error;
  CpbParams params;
  std::vector<CpbSample> samples;
  int underflow_count = 0;
  int full_events = 0;
  double min_occupancy = 0.0;
  double max_occupancy = 0.0;
  double initial_occupancy = 0.0;
  bool estimated_initial = false;
  int first_underflow_index = -1;
};

CpbModelResult buildCpbModel(const CpbParams& params, const CpbInitial& initial,
                             double tick_seconds,
                             const std::vector<CpbAuInput>& aus);

}  // namespace bs
