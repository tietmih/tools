#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/bitreader.h"
#include "core/syntax.h"

namespace bs {

enum class SliceType : int {
  P = 0,
  B = 1,
  I = 2,
  SP = 3,
  SI = 4,
};

inline char sliceTypeChar(SliceType t) {
  switch (t) {
    case SliceType::P:
      return 'P';
    case SliceType::B:
      return 'B';
    case SliceType::I:
      return 'I';
    case SliceType::SP:
      return 'P';
    case SliceType::SI:
      return 'I';
  }
  return '?';
}

struct H264Sps {
  int profile_idc = 0;
  int level_idc = 0;
  int sps_id = 0;
  int chroma_format_idc = 1;
  bool separate_colour_plane = false;
  int bit_depth_luma_minus8 = 0;
  int bit_depth_chroma_minus8 = 0;
  bool scaling_matrix_present = false;

  int log2_max_frame_num_minus4 = 0;
  int pic_order_cnt_type = 0;
  int log2_max_poc_lsb_minus4 = 0;
  bool delta_pic_order_always_zero = false;
  int offset_for_non_ref_pic = 0;
  int offset_for_top_to_bottom_field = 0;
  std::vector<int> offset_for_ref_frame;

  int max_num_ref_frames = 0;
  bool gaps_in_frame_num_allowed = false;
  int pic_width_in_mbs_minus1 = 0;
  int pic_height_in_map_units_minus1 = 0;
  bool frame_mbs_only = true;
  bool mb_adaptive_frame_field = false;
  bool direct_8x8_inference = true;
  bool frame_cropping = false;
  int crop_left = 0, crop_right = 0, crop_top = 0, crop_bottom = 0;

  bool vui_present = false;
  // VUI subset
  bool timing_info_present = false;
  uint32_t num_units_in_tick = 0;
  uint32_t time_scale = 0;
  bool fixed_frame_rate = false;
  uint32_t sar_num = 0, sar_den = 0;  // sample aspect ratio
  bool frame_cropping_vui = false;
  int video_format = 5;
  bool video_full_range = false;
  bool chroma_loc_info_present = false;
  uint32_t num_reorder_frames = 0;
  uint32_t max_dec_frame_buffering = 0;

  bool valid = false;  // parser reached the end without range errors
  int width() const;   // cropped display width in samples
  int height() const;
  int frame_num_modulus() const { return 1 << (log2_max_frame_num_minus4 + 4); }
  double fps() const {
    if (!timing_info_present || num_units_in_tick == 0) return 0.0;
    return static_cast<double>(time_scale) / (2.0 * num_units_in_tick);
  }
};

struct H264Pps {
  int pps_id = 0;
  int sps_id = 0;
  bool entropy_coding_mode = false;  // CABAC
  bool bottom_field_pic_order_in_frame_present = false;
  int num_slice_groups_minus1 = 0;
  int num_ref_idx_l0_default_active_minus1 = 0;
  int num_ref_idx_l1_default_active_minus1 = 0;
  bool weighted_pred = false;
  int weighted_bipred_idc = 0;
  int pic_init_qp_minus26 = 0;
  int pic_init_qs_minus26 = 0;
  int chroma_qp_index_offset = 0;
  bool deblocking_filter_control_present = false;
  bool constrained_intra_pred = false;
  bool redundant_pic_cnt_present = false;
  bool transform_8x8_mode = false;
  int second_chroma_qp_index_offset = 0;
  bool valid = false;
};

struct H264SliceInfo {
  bool valid = false;
  int first_mb_in_slice = 0;
  SliceType slice_type = SliceType::P;
  bool all_slices_same_type = false;  // slice_type >= 5 in bitstream
  int pps_id = 0;
  int sps_id = 0;
  int frame_num = 0;
  bool field_pic = false;
  bool bottom_field = false;
  int idr_pic_id = 0;
  int pic_order_cnt_lsb = 0;
  int delta_pic_order_cnt_bottom = 0;
  int delta_pic_order_cnt0 = 0;
  int delta_pic_order_cnt1 = 0;
  bool idr = false;
  bool ref = false;
  int nal_ref_idc = 0;
  int nal_type = 0;
  int slice_qp_delta = 0;
  int slice_qs_delta = 0;
  int cabac_init_idc = 0;
  int num_ref_idx_l0_active_minus1 = 0;
  int num_ref_idx_l1_active_minus1 = 0;
  int disable_deblocking_filter_idc = 0;
  int slice_alpha_c0_offset_div2 = 0;
  int slice_beta_offset_div2 = 0;
  // Computed
  int poc = 0;
  int frame_poc = 0;
  int ref_idc_display = 0;
  uint64_t header_bits = 0;  // slice header length in bits (RBSP relative)
};

// Context needed to parse a slice header and compute POC.
struct H264State {
  H264Sps sps;
  H264Pps pps;
  bool have_sps = false;
  bool have_pps = false;

  // POC state
  int prev_poc_msb = 0;
  int prev_poc_lsb = 0;
  int prev_frame_num = 0;
  bool prev_frame_num_valid = false;
  int prev_frame_num_offset = 0;
};

// Parse functions. `b` may be empty to skip tree building (for lightweight
// scans).
void parseH264Sps(BitReader& br, SyntaxBuilder* b, H264Sps& out);
void parseH264Pps(BitReader& br, SyntaxBuilder* b, H264Pps& out,
                  const H264Sps* sps);
void parseH264Sei(const Rbsp& rbsp, SyntaxBuilder* b, std::string& summary);
// Returns header bits consumed; fills info. Uses state's sps/pps.
void parseH264SliceHeader(BitReader& br, SyntaxBuilder* b, H264State& state,
                          H264SliceInfo& info, bool emit_poc = true);

std::string h264ProfileName(int profile_idc);
bool isKnownH264Profile(int profile_idc);
std::string h264LevelName(int level_idc);
std::string h264ChromaFormatName(int idc);

}  // namespace bs
