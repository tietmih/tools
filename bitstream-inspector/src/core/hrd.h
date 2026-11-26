#pragma once

#include <cstdint>
#include <vector>

namespace bs {

struct HrdCpb {
  uint32_t bit_rate_value_minus1 = 0;
  uint32_t cpb_size_value_minus1 = 0;
  bool cbr = false;
  uint64_t bit_rate = 0;  // bit/s
  uint64_t cpb_size = 0;  // bits
};

struct H264Hrd {
  bool present = false;
  uint32_t cpb_cnt_minus1 = 0;
  uint32_t bit_rate_scale = 0;
  uint32_t cpb_size_scale = 0;
  std::vector<HrdCpb> cpb;
  uint32_t initial_cpb_removal_delay_length_minus1 = 0;
  uint32_t cpb_removal_delay_length_minus1 = 0;
  uint32_t dpb_output_delay_length_minus1 = 0;
  uint32_t time_offset_length = 0;
};

struct H265HrdSubLayer {
  bool fixed_pic_rate_general = false;
  bool fixed_pic_rate_within_cvs = false;
  bool low_delay_hrd = false;
  uint32_t elemental_duration_in_tc_minus1 = 0;
  uint32_t cpb_cnt_minus1 = 0;
  std::vector<HrdCpb> nal_cpb;
  std::vector<HrdCpb> vcl_cpb;
};

struct H265Hrd {
  bool present = false;
  bool nal_hrd_present = false;
  bool vcl_hrd_present = false;
  bool sub_pic_hrd_params_present = false;
  bool sub_pic_cpb_params_in_pic_timing_sei = false;
  uint32_t tick_divisor_minus2 = 0;
  uint32_t du_cpb_removal_delay_increment_length_minus1 = 0;
  uint32_t dpb_output_delay_du_length_minus1 = 0;
  uint32_t bit_rate_scale = 0;
  uint32_t cpb_size_scale = 0;
  uint32_t cpb_size_du_scale = 0;
  uint32_t initial_cpb_removal_delay_length_minus1 = 0;
  uint32_t au_cpb_removal_delay_length_minus1 = 0;
  uint32_t dpb_output_delay_length_minus1 = 0;
  std::vector<H265HrdSubLayer> sub_layers;
};

}  // namespace bs
