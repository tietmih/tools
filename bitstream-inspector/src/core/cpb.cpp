#include "core/cpb.h"

#include <algorithm>
#include <cmath>

namespace bs {

namespace {

double saturatingDouble(uint64_t value) { return static_cast<double>(value); }

}  // namespace

CpbModelResult buildCpbModel(const CpbParams& params, const CpbInitial& initial,
                             double tick_seconds,
                             const std::vector<CpbAuInput>& aus) {
  CpbModelResult result;
  result.params = params;
  if (params.bit_rate == 0 || params.cpb_size == 0) {
    result.error = "HRD bit rate or CPB size is zero";
    return result;
  }
  if (!(tick_seconds > 0.0) || !std::isfinite(tick_seconds)) {
    result.error = "HRD clock tick is unavailable";
    return result;
  }
  if (aus.empty()) {
    result.error = "no access units";
    return result;
  }

  const double bit_rate = saturatingDouble(params.bit_rate);
  const double cpb_size = saturatingDouble(params.cpb_size);
  result.estimated_initial = !initial.present || initial.estimated;

  std::vector<double> removal(aus.size(), 0.0);
  double prev_removal = 0.0;
  double previous_anchor = 0.0;
  uint32_t prev_delay = 0;
  bool have_prev_delay = false;
  for (size_t i = 0; i < aus.size(); i++) {
    const bool have_delay = aus[i].has_removal_delay && params.delay_bits > 0 &&
                            params.delay_bits < 63;
    if (i == 0) {
      if (initial.present) {
        removal[i] = static_cast<double>(initial.initial_delay) / 90000.0;
      } else {
        removal[i] = cpb_size / bit_rate;
      }
      previous_anchor = removal[i];
    } else if (aus[i].segment_start && have_delay) {
      removal[i] = previous_anchor +
                   static_cast<double>(aus[i].removal_delay) * tick_seconds;
      previous_anchor = removal[i];
    } else if (have_delay) {
      const uint64_t modulus = 1ull << params.delay_bits;
      const uint64_t current = aus[i].removal_delay;
      uint64_t delta =
          have_prev_delay ? (current + modulus - prev_delay) % modulus : 0;
      if (delta == 0) delta = 1;
      removal[i] = prev_removal + static_cast<double>(delta) * tick_seconds;
    } else if (aus[i].fallback_interval_sec > 0.0) {
      removal[i] = prev_removal + aus[i].fallback_interval_sec;
      if (aus[i].segment_start) previous_anchor = removal[i];
    } else {
      result.error = "missing pic_timing removal delay";
      return result;
    }
    if (have_delay) {
      prev_delay = aus[i].removal_delay;
      have_prev_delay = true;
    }
    prev_removal = removal[i];
  }

  const double lead_seconds =
      initial.present ? (static_cast<double>(initial.initial_delay) +
                         static_cast<double>(initial.initial_offset)) /
                            90000.0
                      : cpb_size / bit_rate;
  std::vector<double> arrival_earliest(aus.size(), 0.0);
  std::vector<double> arrival_init(aus.size(), 0.0);
  std::vector<double> arrival_final(aus.size(), 0.0);
  for (size_t i = 0; i < aus.size(); i++) {
    arrival_earliest[i] = removal[i] - lead_seconds;
    const double bits = saturatingDouble(aus[i].bits);
    if (i == 0) {
      arrival_init[i] = 0.0;
    } else if (params.cbr) {
      arrival_init[i] = arrival_final[i - 1];
    } else {
      arrival_init[i] = std::max(arrival_final[i - 1], arrival_earliest[i]);
    }
    arrival_final[i] = arrival_init[i] + bits / bit_rate;
  }

  result.samples.reserve(aus.size());
  double removed_before = 0.0;
  bool first = true;
  for (size_t i = 0; i < aus.size(); i++) {
    double arrived = 0.0;
    for (size_t k = 0; k < aus.size(); k++) {
      const double age = removal[i] - arrival_init[k];
      if (age <= 0.0) continue;
      const double bits = saturatingDouble(aus[k].bits);
      arrived += std::min(bits, bit_rate * age);
    }
    const double before = arrived - removed_before;
    const double bits = saturatingDouble(aus[i].bits);
    const double after = before - bits;

    CpbSample sample;
    sample.frame_index = static_cast<int>(i);
    sample.au_bits = aus[i].bits;
    sample.removal_delay = aus[i].removal_delay;
    sample.has_removal_delay = aus[i].has_removal_delay;
    sample.removal_time_sec = removal[i];
    sample.occupancy_before_bits = before;
    sample.occupancy_after_bits = after;
    sample.occupancy_ratio = after / cpb_size;
    sample.underflow = after < -0.5;
    sample.overflow = before > cpb_size + 0.5;
    sample.estimated =
        !initial.present || initial.estimated || !aus[i].has_removal_delay;
    sample.segment_start = aus[i].segment_start;
    if (sample.underflow) {
      result.underflow_count++;
      if (result.first_underflow_index < 0)
        result.first_underflow_index = static_cast<int>(i);
    }
    if (before > cpb_size + 0.5) result.full_events++;
    if (first) {
      result.initial_occupancy = before;
      result.min_occupancy = after;
      result.max_occupancy = after;
      first = false;
    } else {
      result.min_occupancy = std::min(result.min_occupancy, after);
      result.max_occupancy = std::max(result.max_occupancy, after);
    }
    result.samples.push_back(sample);
    removed_before += bits;
  }
  result.valid = true;
  return result;
}

}  // namespace bs
