#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/bitreader.h"
#include "core/h264.h"  // SliceType
#include "core/hrd.h"
#include "core/syntax.h"

namespace bs {

struct H265ProfileTierLevel {
  int profile_space = 0;
  bool tier_flag = false;
  int profile_idc = 0;
  bool profile_compatibility[32] = {};
  bool progressive_source = true;
  bool interlaced_source = false;
  bool non_packed_constraint = false;
  bool frame_only_constraint = true;
  int level_idc = 0;
};

struct H265Rps {
  int num_delta_pocs = 0;
  int num_negative = 0;
  std::vector<int> delta_pocs;
  std::vector<uint8_t> used;
};

struct H265Sps {
  int vps_id = 0;
  int sps_id = 0;
  int max_sub_layers_minus1 = 0;
  bool temporal_id_nesting = true;
  H265ProfileTierLevel ptl;

  int chroma_format_idc = 1;
  bool separate_colour_plane = false;
  int width = 0;   // coded width in luma samples
  int height = 0;  // coded height in luma samples
  bool conformance_window = false;
  int conf_left = 0, conf_right = 0, conf_top = 0, conf_bottom = 0;
  int bit_depth_luma_minus8 = 0;
  int bit_depth_chroma_minus8 = 0;
  int log2_max_poc_lsb_minus4 = 0;
  int max_dec_pic_buffering_minus1 = 0;
  int max_num_reorder_pics = 0;
  int max_latency_increase_plus1 = 0;

  int log2_min_cb_minus3 = 0;
  int log2_diff_max_min_cb = 0;
  int log2_min_tb_minus2 = 0;
  int log2_diff_max_min_tb = 0;
  bool scaling_list_enabled = false;
  bool amp_enabled = true;
  bool sao_enabled = false;
  bool pcm_enabled = false;
  int pcm_bit_depth_luma_minus1 = 0;
  int pcm_bit_depth_chroma_minus1 = 0;
  int pcm_log2_min_cb_minus3 = 0;
  int pcm_log2_diff_max_min_cb = 0;
  bool pcm_loop_filter_disabled = false;

  bool long_term_ref_pics_present = false;
  int num_long_term_ref_pics_sps = 0;
  std::vector<int> lt_poc_lsb_sps;
  std::vector<uint8_t> lt_used_sps;
  bool temporal_mvp_enabled = false;
  bool strong_intra_smoothing = true;
  bool vui_present = false;
  bool timing_info_present = false;
  uint32_t num_units_in_tick = 0;
  uint32_t time_scale = 0;
  bool video_full_range = false;
  bool colour_description_present = false;
  int colour_primaries = 2;
  int transfer_characteristics = 2;
  int matrix_coefficients = 2;
  bool field_seq_flag = false;
  bool frame_field_info_present = false;
  bool vui_hrd_present = false;
  H265Hrd hrd;

  std::vector<H265Rps> st_rps;
  bool use_integer_mv = false;
  bool parsed_fully = false;

  int display_width() const;
  int display_height() const;
  int ctb_log2() const { return log2_min_cb_minus3 + 3 + log2_diff_max_min_cb; }
  int ctb_size() const { return 1 << ctb_log2(); }
  int pic_size_in_ctbs() const {
    int ctb = ctb_size();
    int w = (width + ctb - 1) / ctb;
    int h = (height + ctb - 1) / ctb;
    return w * h;
  }
  int chroma_array_type() const {
    return separate_colour_plane ? 0 : chroma_format_idc;
  }
  double fps() const {
    if (!timing_info_present || num_units_in_tick == 0) return 0.0;
    return static_cast<double>(time_scale) / num_units_in_tick;
  }
};

struct H265Pps {
  int pps_id = 0;
  int sps_id = 0;
  bool dependent_slice_segments_enabled = false;
  bool output_flag_present = false;
  int num_extra_slice_header_bits = 0;
  bool sign_data_hiding_enabled = false;
  bool cabac_init_present = false;
  int num_ref_idx_l0_default_active_minus1 = 0;
  int num_ref_idx_l1_default_active_minus1 = 0;
  int init_qp_minus26 = 0;
  bool constrained_intra_pred = false;
  bool transform_skip_enabled = false;
  bool cu_qp_delta_enabled = false;
  int diff_cu_qp_delta_depth = 0;
  int cb_qp_offset = 0;
  int cr_qp_offset = 0;
  bool slice_chroma_qp_offsets_present = false;
  bool weighted_pred = false;
  bool weighted_bipred = false;
  bool transquant_bypass_enabled = false;
  bool tiles_enabled = false;
  bool entropy_coding_sync_enabled = false;
  int num_tile_columns_minus1 = 0;
  int num_tile_rows_minus1 = 0;
  std::vector<int> tile_column_widths;
  std::vector<int> tile_row_heights;
  bool uniform_spacing = true;
  bool loop_filter_across_tiles_enabled = false;
  bool loop_filter_across_slices_enabled = false;
  bool deblocking_filter_control_present = false;
  bool deblocking_filter_override_enabled = false;
  bool deblocking_filter_disabled = false;
  int beta_offset_div2 = 0;
  int tc_offset_div2 = 0;
  bool lists_modification_present = false;
  int log2_parallel_merge_level_minus2 = 0;
  bool slice_segment_header_extension_present = false;
  bool chroma_qp_offset_list_enabled = false;
  int diff_cu_chroma_qp_offset_depth = 0;
  bool parsed_fully = false;
};

struct H265SliceInfo {
  bool valid = false;
  bool first_slice_segment_in_pic = false;
  int pps_id = 0;
  bool dependent_slice_segment = false;
  int slice_segment_address = 0;
  SliceType slice_type = SliceType::P;
  int pic_order_cnt_lsb = 0;
  int slice_qp_delta = 0;
  int num_ref_idx_l0_active_minus1 = 0;
  int num_ref_idx_l1_active_minus1 = 0;
  bool sao_luma = false;
  bool sao_chroma = false;
  bool deblocking_filter_disabled = false;
  int nal_type = 0;
  int temporal_id_plus1 = 1;
  bool irap = false;
  bool idr = false;
  H265Rps short_term_rps;
  std::vector<int> lt_poc_lsb;
  std::vector<uint8_t> lt_used;
  std::vector<uint8_t> lt_msb_present;
  std::vector<uint32_t> lt_delta_msb_cycle;
  int num_pic_total_curr = 0;
  uint64_t header_bits = 0;
};

struct H265VpsHrd {
  uint32_t hrd_layer_set_idx = 0;
  bool cprms_present = true;
  H265Hrd hrd;
};

struct H265Vps {
  int vps_id = 0;
  int max_layers_minus1 = 0;
  int max_sub_layers_minus1 = 0;
  H265ProfileTierLevel ptl;
  bool sub_layer_ordering_info_present = false;
  std::vector<int> max_dec_pic_buffering_minus1;
  std::vector<int> max_num_reorder_pics;
  std::vector<int> max_latency_increase_plus1;
  int max_layer_id = 0;
  int num_layer_sets_minus1 = 0;
  bool timing_info_present = false;
  uint32_t num_units_in_tick = 0;
  uint32_t time_scale = 0;
  bool poc_proportional_to_timing = false;
  uint32_t num_ticks_poc_diff_one_minus1 = 0;
  std::vector<H265VpsHrd> hrd;
  bool extension = false;
  bool parsed_fully = false;
};

struct H265State {
  H265Sps sps;
  H265Pps pps;
  H265Vps vps;
  bool have_sps = false;
  bool have_pps = false;
  bool have_vps = false;
  int nal_type = 0;
  int temporal_id_plus1 = 1;
};

void parseH265Vps(BitReader& br, SyntaxBuilder* sb, H265Vps* out = nullptr);
void parseH265Sps(BitReader& br, SyntaxBuilder* sb, H265Sps& sps);
void parseH265Pps(BitReader& br, SyntaxBuilder* sb, H265Pps& pps);
void parseH265SliceSegmentHeader(BitReader& br, SyntaxBuilder* sb,
                                 H265State& state, H265SliceInfo& info);

std::string h265ProfileName(int profile_idc);
std::string h265TierName(bool high);
std::string h265LevelName(int level_idc);
std::string h265ChromaFormatName(int idc);
std::string h265NalGroupName(int nal_type);

}  // namespace bs
