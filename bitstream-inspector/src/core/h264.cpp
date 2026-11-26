#include "core/h264.h"

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "core/fieldreader.h"

namespace bs {

namespace {

const int kHighProfiles[] = {100, 110, 122, 244, 44,  83,  86, 118,
                             128, 138, 139, 134, 135, 136, 137};
bool isHighProfile(int idc) {
  return std::find(std::begin(kHighProfiles), std::end(kHighProfiles), idc) !=
         std::end(kHighProfiles);
}

void parseScalingList(FieldReader& r, int size, const char* name) {
  Section s(r.sb(), name);
  int last_scale = 8, next_scale = 8;
  for (int j = 0; j < size; j++) {
    if (next_scale != 0) {
      int32_t delta = r.se("delta_scale");
      next_scale = (last_scale + delta + 256) % 256;
      if (j == 0 && next_scale == 0) r.note("useDefaultScalingMatrix", "true");
    }
    last_scale = (next_scale == 0) ? last_scale : next_scale;
  }
}

void parseHrd(FieldReader& r, const char* name, H264Hrd* out) {
  Section s(r.sb(), name);
  H264Hrd hrd;
  hrd.cpb_cnt_minus1 = r.ue("cpb_cnt_minus1");
  hrd.bit_rate_scale = r.u(4, "bit_rate_scale");
  hrd.cpb_size_scale = r.u(4, "cpb_size_scale");
  if (hrd.cpb_cnt_minus1 > 31) {
    r.error("cpb_cnt_minus1 out of range");
    return;
  }
  for (uint32_t i = 0; i <= hrd.cpb_cnt_minus1 && !r.fail(); i++) {
    std::string rate = "bit_rate_value_minus1[" + std::to_string(i) + "]";
    std::string size = "cpb_size_value_minus1[" + std::to_string(i) + "]";
    std::string cbr_name = "cbr_flag[" + std::to_string(i) + "]";
    HrdCpb cpb;
    cpb.bit_rate_value_minus1 = r.ue(rate.c_str());
    cpb.cpb_size_value_minus1 = r.ue(size.c_str());
    r.flag(cbr_name.c_str(), cpb.cbr);
    cpb.bit_rate = (static_cast<uint64_t>(cpb.bit_rate_value_minus1) + 1)
                   << (6 + hrd.bit_rate_scale);
    cpb.cpb_size = (static_cast<uint64_t>(cpb.cpb_size_value_minus1) + 1)
                   << (4 + hrd.cpb_size_scale);
    hrd.cpb.push_back(cpb);
  }
  hrd.initial_cpb_removal_delay_length_minus1 =
      r.u(5, "initial_cpb_removal_delay_length_minus1");
  hrd.cpb_removal_delay_length_minus1 =
      r.u(5, "cpb_removal_delay_length_minus1");
  hrd.dpb_output_delay_length_minus1 = r.u(5, "dpb_output_delay_length_minus1");
  hrd.time_offset_length = r.u(5, "time_offset_length");
  if (r.fail()) return;
  hrd.present = true;
  if (out) *out = hrd;
}

const char* kAspectRatioNames[] = {
    "Unspecified", "1:1",    "12:11", "10:11", "16:11", "40:33",
    "24:11",       "20:11",  "32:11", "80:33", "18:11", "15:11",
    "64:33",       "160:99", "4:3",   "3:2",   "2:1",   "Extended_SAR"};

void parseH264Vui(FieldReader& r, H264Sps& sps) {
  Section s(r.sb(), "vui_parameters");
  bool aspect = false;
  r.flag("aspect_ratio_info_present_flag", aspect);
  if (aspect) {
    uint32_t idc = r.u(8, "aspect_ratio_idc");
    std::string name = "Reserved";
    if (idc < std::size(kAspectRatioNames)) name = kAspectRatioNames[idc];
    r.note("  meaning", name);
    if (idc == 255) {
      sps.sar_num = r.u(16, "sar_width");
      sps.sar_den = r.u(16, "sar_height");
    } else if (idc >= 1 && idc <= 16) {
      static const int num[] = {0,  1,  12, 10, 16,  40, 24, 20, 32,
                                80, 18, 15, 64, 160, 4,  3,  2};
      static const int den[] = {0,  1,  11, 11, 11, 33, 11, 11, 11,
                                33, 11, 11, 33, 99, 3,  2,  1};
      sps.sar_num = num[idc];
      sps.sar_den = den[idc];
    }
  }
  bool overscan = false;
  r.flag("overscan_info_present_flag", overscan);
  if (overscan) {
    bool appropriate = false;
    r.flag("overscan_appropriate_flag", appropriate);
  }
  bool video_signal = false;
  r.flag("video_signal_type_present_flag", video_signal);
  if (video_signal) {
    sps.video_format = static_cast<int>(r.u(3, "video_format"));
    bool full = false;
    r.flag("video_full_range_flag", full);
    sps.video_full_range = full;
    bool colour = false;
    r.flag("colour_description_present_flag", colour);
    if (colour) {
      sps.colour_description_present = true;
      sps.colour_primaries = static_cast<int>(r.u(8, "colour_primaries"));
      sps.transfer_characteristics =
          static_cast<int>(r.u(8, "transfer_characteristics"));
      sps.matrix_coefficients = static_cast<int>(r.u(8, "matrix_coefficients"));
    }
  }
  bool chroma_loc = false;
  r.flag("chroma_loc_info_present_flag", chroma_loc);
  sps.chroma_loc_info_present = chroma_loc;
  if (chroma_loc) {
    r.ue("chroma_sample_loc_type_top_field");
    r.ue("chroma_sample_loc_type_bottom_field");
  }
  bool timing = false;
  r.flag("timing_info_present_flag", timing);
  sps.timing_info_present = timing;
  if (timing) {
    sps.num_units_in_tick = r.u(32, "num_units_in_tick");
    sps.time_scale = r.u(32, "time_scale");
    bool fixed = false;
    r.flag("fixed_frame_rate_flag", fixed);
    sps.fixed_frame_rate = fixed;
  }
  bool nal_hrd = false, vcl_hrd = false;
  r.flag("nal_hrd_parameters_present_flag", nal_hrd);
  if (nal_hrd) parseHrd(r, "nal_hrd_parameters", &sps.nal_hrd);
  r.flag("vcl_hrd_parameters_present_flag", vcl_hrd);
  if (vcl_hrd) parseHrd(r, "vcl_hrd_parameters", &sps.vcl_hrd);
  if (nal_hrd || vcl_hrd) r.flag("low_delay_hrd_flag", sps.low_delay_hrd);
  r.flag("pic_struct_present_flag", sps.pic_struct_present);
  bool restriction = false;
  r.flag("bitstream_restriction_flag", restriction);
  if (restriction) {
    bool mv = false;
    r.flag("motion_vectors_over_pic_boundaries_flag", mv);
    r.ue("max_bytes_per_pic_denom");
    r.ue("max_bits_per_mb_denom");
    r.ue("log2_max_mv_length_horizontal");
    r.ue("log2_max_mv_length_vertical");
    sps.num_reorder_frames = r.ue("max_num_reorder_frames");
    sps.max_dec_frame_buffering = r.ue("max_dec_frame_buffering");
  }
  sps.vui_present = true;
}

int macroblockWidth(const H264Sps& sps) {
  // ChromaArrayType: 0 when separate_colour_plane_flag, else chroma_format_idc
  int chroma_array_type = sps.separate_colour_plane ? 0 : sps.chroma_format_idc;
  int sub_width_c = (chroma_array_type == 1 || chroma_array_type == 2) ? 2 : 1;
  int mb_width = (sps.pic_width_in_mbs_minus1 + 1) * 16;
  int crop_unit_x = (chroma_array_type == 0) ? 1 : sub_width_c;
  return mb_width - crop_unit_x * (sps.crop_left + sps.crop_right);
}

int macroblockHeight(const H264Sps& sps) {
  int chroma_array_type = sps.separate_colour_plane ? 0 : sps.chroma_format_idc;
  int sub_height_c = (chroma_array_type == 1) ? 2 : 1;
  int frame_factor = 2 - (sps.frame_mbs_only ? 1 : 0);
  int mb_height = frame_factor * (sps.pic_height_in_map_units_minus1 + 1) * 16;
  int crop_unit_y =
      (chroma_array_type == 0) ? frame_factor : sub_height_c * frame_factor;
  return mb_height - crop_unit_y * (sps.crop_top + sps.crop_bottom);
}

}  // namespace

int H264Sps::width() const { return macroblockWidth(*this); }
int H264Sps::height() const { return macroblockHeight(*this); }

void parseH264Sps(BitReader& br, SyntaxBuilder* sb, H264Sps& sps) {
  FieldReader r(br, sb);
  Section s(sb, "seq_parameter_set_rbsp");

  sps.profile_idc = static_cast<int>(r.u(8, "profile_idc"));
  std::string profile = h264ProfileName(sps.profile_idc);
  r.note("  profile_name", profile);
  uint32_t constraint_flags = r.u(8, "constraint_set_flags");
  r.note("  constraint_set0..5", "0b" + std::to_string(constraint_flags));
  sps.level_idc = static_cast<int>(r.u(8, "level_idc"));
  r.note("  level_name", h264LevelName(sps.level_idc));
  sps.sps_id = static_cast<int>(r.ue("seq_parameter_set_id"));

  if (isHighProfile(sps.profile_idc)) {
    sps.chroma_format_idc = static_cast<int>(r.ue("chroma_format_idc"));
    r.note("  chroma_format", h264ChromaFormatName(sps.chroma_format_idc));
    if (sps.chroma_format_idc == 3) {
      bool separate = false;
      r.flag("separate_colour_plane_flag", separate);
      sps.separate_colour_plane = separate;
    }
    sps.bit_depth_luma_minus8 = static_cast<int>(r.ue("bit_depth_luma_minus8"));
    sps.bit_depth_chroma_minus8 =
        static_cast<int>(r.ue("bit_depth_chroma_minus8"));
    bool qpprime = false;
    r.flag("qpprime_y_zero_transform_bypass_flag", qpprime);
    bool scaling_matrix = false;
    r.flag("seq_scaling_matrix_present_flag", scaling_matrix);
    sps.scaling_matrix_present = scaling_matrix;
    if (scaling_matrix) {
      int lists = (sps.chroma_format_idc != 3) ? 8 : 12;
      for (int i = 0; i < lists; i++) {
        bool present = false;
        std::string name =
            "seq_scaling_list_present_flag[" + std::to_string(i) + "]";
        r.flag(name.c_str(), present);
        if (present) {
          std::string lname = "scaling_list_" + std::to_string(i < 6 ? 4 : 8) +
                              "x" + std::to_string(i < 6 ? 4 : 8) + "[" +
                              std::to_string(i) + "]";
          parseScalingList(r, i < 6 ? 16 : 64, lname.c_str());
        }
      }
    }
  }
  if (sps.chroma_format_idc > 3) {
    r.error("chroma_format_idc out of range");
    return;
  }

  sps.log2_max_frame_num_minus4 =
      static_cast<int>(r.ue("log2_max_frame_num_minus4"));
  if (sps.log2_max_frame_num_minus4 > 12) {
    r.error("log2_max_frame_num_minus4 out of range");
    return;
  }
  sps.pic_order_cnt_type = static_cast<int>(r.ue("pic_order_cnt_type"));
  if (sps.pic_order_cnt_type == 0) {
    sps.log2_max_poc_lsb_minus4 =
        static_cast<int>(r.ue("log2_max_pic_order_cnt_lsb_minus4"));
    if (sps.log2_max_poc_lsb_minus4 > 12) {
      r.error("log2_max_pic_order_cnt_lsb_minus4 out of range");
      return;
    }
  } else if (sps.pic_order_cnt_type == 1) {
    r.flag("delta_pic_order_always_zero_flag", sps.delta_pic_order_always_zero);
    sps.offset_for_non_ref_pic = r.se("offset_for_non_ref_pic");
    sps.offset_for_top_to_bottom_field = r.se("offset_for_top_to_bottom_field");
    uint32_t num = r.ue("num_ref_frames_in_pic_order_cnt_cycle");
    if (num > 255) {
      r.error("num_ref_frames_in_pic_order_cnt_cycle out of range");
      return;
    }
    sps.offset_for_ref_frame.resize(num);
    for (uint32_t i = 0; i < num; i++) {
      std::string name = "offset_for_ref_frame[" + std::to_string(i) + "]";
      sps.offset_for_ref_frame[i] = r.se(name.c_str());
    }
  } else if (sps.pic_order_cnt_type != 2) {
    r.error("pic_order_cnt_type out of range");
    return;
  }

  sps.max_num_ref_frames = static_cast<int>(r.ue("max_num_ref_frames"));
  r.flag("gaps_in_frame_num_value_allowed_flag", sps.gaps_in_frame_num_allowed);
  sps.pic_width_in_mbs_minus1 =
      static_cast<int>(r.ue("pic_width_in_mbs_minus1"));
  sps.pic_height_in_map_units_minus1 =
      static_cast<int>(r.ue("pic_height_in_map_units_minus1"));
  r.flag("frame_mbs_only_flag", sps.frame_mbs_only);
  if (!sps.frame_mbs_only)
    r.flag("mb_adaptive_frame_field_flag", sps.mb_adaptive_frame_field);
  r.flag("direct_8x8_inference_flag", sps.direct_8x8_inference);
  r.flag("frame_cropping_flag", sps.frame_cropping);
  if (sps.frame_cropping) {
    sps.crop_left = static_cast<int>(r.ue("frame_crop_left_offset"));
    sps.crop_right = static_cast<int>(r.ue("frame_crop_right_offset"));
    sps.crop_top = static_cast<int>(r.ue("frame_crop_top_offset"));
    sps.crop_bottom = static_cast<int>(r.ue("frame_crop_bottom_offset"));
  }
  bool vui = false;
  r.flag("vui_parameters_present_flag", vui);
  if (vui && !r.fail()) parseH264Vui(r, sps);

  r.note("  resolution",
         std::to_string(sps.width()) + "x" + std::to_string(sps.height()));
  if (sps.pic_width_in_mbs_minus1 > 511 ||
      sps.pic_height_in_map_units_minus1 > 511) {
    r.error("picture size fields exceed level limits");
    return;
  }
  auto badCrop = [](int v) { return v < 0 || v > 65535; };
  if (badCrop(sps.crop_left) || badCrop(sps.crop_right) ||
      badCrop(sps.crop_top) || badCrop(sps.crop_bottom)) {
    r.error("frame cropping offsets out of range");
    return;
  }
  int64_t coded_w =
      (static_cast<int64_t>(sps.pic_width_in_mbs_minus1) + 1) * 16;
  int64_t coded_h =
      (static_cast<int64_t>(sps.pic_height_in_map_units_minus1) + 1) * 16;
  if (static_cast<int64_t>(sps.crop_left) + sps.crop_right > coded_w ||
      static_cast<int64_t>(sps.crop_top) + sps.crop_bottom > coded_h) {
    r.error("frame cropping exceeds picture size");
    return;
  }
  sps.valid = !r.fail();
}

void parseH264Pps(BitReader& br, SyntaxBuilder* sb, H264Pps& pps,
                  const H264Sps* sps) {
  FieldReader r(br, sb);
  Section s(sb, "pic_parameter_set_rbsp");

  pps.pps_id = static_cast<int>(r.ue("pic_parameter_set_id"));
  pps.sps_id = static_cast<int>(r.ue("seq_parameter_set_id"));
  r.flag("entropy_coding_mode_flag", pps.entropy_coding_mode);
  r.flag("bottom_field_pic_order_in_frame_present_flag",
         pps.bottom_field_pic_order_in_frame_present);
  pps.num_slice_groups_minus1 =
      static_cast<int>(r.ue("num_slice_groups_minus1"));
  if (pps.num_slice_groups_minus1 > 7) {
    r.error("num_slice_groups_minus1 out of range");
    return;
  }
  if (pps.num_slice_groups_minus1 > 0) {
    uint32_t map_type = r.ue("slice_group_map_type");
    if (map_type == 0) {
      for (int i = 0; i <= pps.num_slice_groups_minus1; i++) {
        std::string name = "run_length_minus1[" + std::to_string(i) + "]";
        r.ue(name.c_str());
      }
    } else if (map_type == 2) {
      for (int i = 0; i < pps.num_slice_groups_minus1; i++) {
        std::string t = "top_left[" + std::to_string(i) + "]";
        std::string b = "bottom_right[" + std::to_string(i) + "]";
        r.ue(t.c_str());
        r.ue(b.c_str());
      }
    } else if (map_type >= 3 && map_type <= 5) {
      r.u(1, "slice_group_change_direction_flag");
      r.ue("slice_group_change_rate_minus1");
    } else if (map_type == 6) {
      uint32_t num = r.ue("pic_size_in_map_units_minus1");
      if (num > 65535) {
        r.error("pic_size_in_map_units_minus1 out of range");
        return;
      }
      uint32_t bits = 1;
      while (((uint32_t)1 << bits) <
             (uint32_t)(pps.num_slice_groups_minus1 + 1))
        bits++;
      for (uint32_t i = 0; i <= num && !r.fail(); i++) {
        std::string name = "slice_group_id[" + std::to_string(i) + "]";
        r.u(static_cast<int>(bits), name.c_str());
      }
    } else {
      r.error("slice_group_map_type out of range");
      return;
    }
  }
  pps.num_ref_idx_l0_default_active_minus1 =
      static_cast<int>(r.ue("num_ref_idx_l0_default_active_minus1"));
  pps.num_ref_idx_l1_default_active_minus1 =
      static_cast<int>(r.ue("num_ref_idx_l1_default_active_minus1"));
  if (pps.num_ref_idx_l0_default_active_minus1 < 0 ||
      pps.num_ref_idx_l0_default_active_minus1 > 31 ||
      pps.num_ref_idx_l1_default_active_minus1 < 0 ||
      pps.num_ref_idx_l1_default_active_minus1 > 31) {
    r.error("num_ref_idx_default_active_minus1 out of range");
    return;
  }
  r.flag("weighted_pred_flag", pps.weighted_pred);
  pps.weighted_bipred_idc = static_cast<int>(r.u(2, "weighted_bipred_idc"));
  pps.pic_init_qp_minus26 = r.se("pic_init_qp_minus26");
  pps.pic_init_qs_minus26 = r.se("pic_init_qs_minus26");
  pps.chroma_qp_index_offset = r.se("chroma_qp_index_offset");
  r.flag("deblocking_filter_control_present_flag",
         pps.deblocking_filter_control_present);
  r.flag("constrained_intra_pred_flag", pps.constrained_intra_pred);
  r.flag("redundant_pic_cnt_present_flag", pps.redundant_pic_cnt_present);

  if (br.more_rbsp_data()) {
    bool t8x8 = false;
    r.flag("transform_8x8_mode_flag", t8x8);
    pps.transform_8x8_mode = t8x8;
    bool pic_scaling = false;
    r.flag("pic_scaling_matrix_present_flag", pic_scaling);
    if (pic_scaling) {
      int chroma = sps ? sps->chroma_format_idc : 1;
      int lists = 6;
      if (pps.transform_8x8_mode) lists += (chroma == 3) ? 6 : 2;
      for (int i = 0; i < lists; i++) {
        bool present = false;
        std::string name =
            "pic_scaling_list_present_flag[" + std::to_string(i) + "]";
        r.flag(name.c_str(), present);
        if (present) {
          std::string lname = "scaling_list[" + std::to_string(i) + "]";
          parseScalingList(r, i < 6 ? 16 : 64, lname.c_str());
        }
      }
    }
    pps.second_chroma_qp_index_offset = r.se("second_chroma_qp_index_offset");
  }
  pps.valid = !r.fail();
}

void parseH264SliceHeader(BitReader& br, SyntaxBuilder* sb, H264State& state,
                          H264SliceInfo& info, bool emit_poc) {
  FieldReader r(br, sb);
  Section s(sb, "slice_header");
  const H264Sps& sps = state.sps;
  const H264Pps& pps = state.pps;

  info.valid = false;
  if (!state.have_sps || !state.have_pps) {
    r.error("no active SPS/PPS for this slice");
    return;
  }
  if (sps.log2_max_frame_num_minus4 < 0 || sps.log2_max_frame_num_minus4 > 12 ||
      sps.log2_max_poc_lsb_minus4 < 0 || sps.log2_max_poc_lsb_minus4 > 12 ||
      sps.pic_order_cnt_type < 0 || sps.pic_order_cnt_type > 2) {
    r.error("invalid SPS values for slice header");
    return;
  }

  info.first_mb_in_slice = static_cast<int>(r.ue("first_mb_in_slice"));
  uint32_t st = r.ue("slice_type");
  info.all_slices_same_type = st >= 5;
  if (st > 9) {
    r.error("slice_type out of range");
    return;
  }
  info.slice_type = static_cast<SliceType>(st % 5);
  r.note("  slice_type_name",
         std::string(1, sliceTypeChar(info.slice_type)) + " slice");

  info.pps_id = static_cast<int>(r.ue("pic_parameter_set_id"));
  if (info.pps_id != state.pps.pps_id) {
    r.error("slice references a different PPS than the active one");
  }
  if (sps.separate_colour_plane) {
    uint32_t plane = r.u(2, "colour_plane_id");
    (void)plane;
  }
  info.idr = (info.nal_type == 5);
  info.frame_num =
      static_cast<int>(r.u(sps.log2_max_frame_num_minus4 + 4, "frame_num"));
  if (!sps.frame_mbs_only) {
    bool field = false;
    r.flag("field_pic_flag", field);
    info.field_pic = field;
    if (field) {
      bool bottom = false;
      r.flag("bottom_field_flag", bottom);
      info.bottom_field = bottom;
    }
  }
  if (info.idr) info.idr_pic_id = static_cast<int>(r.ue("idr_pic_id"));

  int delta_poc_bottom = 0, delta_poc0 = 0, delta_poc1 = 0;
  if (sps.pic_order_cnt_type == 0) {
    info.pic_order_cnt_lsb = static_cast<int>(
        r.u(sps.log2_max_poc_lsb_minus4 + 4, "pic_order_cnt_lsb"));
    if (pps.bottom_field_pic_order_in_frame_present && !info.field_pic)
      delta_poc_bottom = r.se("delta_pic_order_cnt_bottom");
  }
  if (sps.pic_order_cnt_type == 1 && !sps.delta_pic_order_always_zero) {
    delta_poc0 = r.se("delta_pic_order_cnt[0]");
    if (pps.bottom_field_pic_order_in_frame_present && !info.field_pic)
      delta_poc1 = r.se("delta_pic_order_cnt[1]");
  }
  info.delta_pic_order_cnt_bottom = delta_poc_bottom;
  info.delta_pic_order_cnt0 = delta_poc0;
  info.delta_pic_order_cnt1 = delta_poc1;

  if (pps.redundant_pic_cnt_present) r.ue("redundant_pic_cnt");

  if (info.slice_type == SliceType::B) {
    bool direct = false;
    r.flag("direct_spatial_mv_pred_flag", direct);
  }
  int num_ref_l0 = pps.num_ref_idx_l0_default_active_minus1;
  int num_ref_l1 = pps.num_ref_idx_l1_default_active_minus1;
  if (info.slice_type == SliceType::P || info.slice_type == SliceType::SP ||
      info.slice_type == SliceType::B) {
    bool override_flag = false;
    r.flag("num_ref_idx_active_override_flag", override_flag);
    if (override_flag) {
      num_ref_l0 = static_cast<int>(r.ue("num_ref_idx_l0_active_minus1"));
      if (info.slice_type == SliceType::B)
        num_ref_l1 = static_cast<int>(r.ue("num_ref_idx_l1_active_minus1"));
    }
  }
  if (num_ref_l0 < 0 || num_ref_l0 > 31 || num_ref_l1 < 0 || num_ref_l1 > 31) {
    r.error("num_ref_idx_active_minus1 out of range");
    return;
  }
  info.num_ref_idx_l0_active_minus1 = num_ref_l0;
  info.num_ref_idx_l1_active_minus1 = num_ref_l1;

  if (info.slice_type != SliceType::I && info.slice_type != SliceType::SI) {
    Section m(r.sb(), "ref_pic_list_modification");
    bool l0_mod = false;
    r.flag("ref_pic_list_modification_flag_l0", l0_mod);
    info.ref_pic_list_modification_flag_l0 = l0_mod;
    if (l0_mod) {
      int c = 0;
      while (c < 66 && !r.fail()) {
        uint32_t idc = r.ue("modification_of_pic_nums_idc");
        if (idc == 3) break;
        int arg = 0;
        if (idc == 0 || idc == 1)
          arg = static_cast<int>(r.ue("abs_diff_pic_num_minus1"));
        else if (idc == 2)
          arg = static_cast<int>(r.ue("long_term_pic_num"));
        info.ref_mod_l0.push_back({static_cast<int>(idc), arg});
        c++;
      }
    }
    if (info.slice_type == SliceType::B) {
      bool l1_mod = false;
      r.flag("ref_pic_list_modification_flag_l1", l1_mod);
      info.ref_pic_list_modification_flag_l1 = l1_mod;
      if (l1_mod) {
        int c = 0;
        while (c < 66 && !r.fail()) {
          uint32_t idc = r.ue("modification_of_pic_nums_idc");
          if (idc == 3) break;
          int arg = 0;
          if (idc == 0 || idc == 1)
            arg = static_cast<int>(r.ue("abs_diff_pic_num_minus1"));
          else if (idc == 2)
            arg = static_cast<int>(r.ue("long_term_pic_num"));
          info.ref_mod_l1.push_back({static_cast<int>(idc), arg});
          c++;
        }
      }
    }
  }

  bool weighted =
      (pps.weighted_pred &&
       (info.slice_type == SliceType::P || info.slice_type == SliceType::SP)) ||
      (pps.weighted_bipred_idc == 1 && info.slice_type == SliceType::B);
  if (weighted) {
    Section w(r.sb(), "pred_weight_table");
    r.ue("luma_log2_weight_denom");
    if (sps.chroma_format_idc != 0) r.ue("chroma_log2_weight_denom");
    int l0 = num_ref_l0 + 1;
    int l1 = (info.slice_type == SliceType::B) ? num_ref_l1 + 1 : 0;
    for (int i = 0; i < l0 && !r.fail(); i++) {
      bool flag = false;
      std::string n = "luma_weight_l0_flag[" + std::to_string(i) + "]";
      r.flag(n.c_str(), flag);
      if (flag) {
        r.se("luma_weight_l0");
        r.se("luma_offset_l0");
      }
      if (sps.chroma_format_idc != 0) {
        std::string cn = "chroma_weight_l0_flag[" + std::to_string(i) + "]";
        r.flag(cn.c_str(), flag);
        if (flag) {
          for (int j = 0; j < 2; j++) {
            r.se("chroma_weight_l0");
            r.se("chroma_offset_l0");
          }
        }
      }
    }
    for (int i = 0; i < l1 && !r.fail(); i++) {
      bool flag = false;
      std::string n = "luma_weight_l1_flag[" + std::to_string(i) + "]";
      r.flag(n.c_str(), flag);
      if (flag) {
        r.se("luma_weight_l1");
        r.se("luma_offset_l1");
      }
      if (sps.chroma_format_idc != 0) {
        std::string cn = "chroma_weight_l1_flag[" + std::to_string(i) + "]";
        r.flag(cn.c_str(), flag);
        if (flag) {
          for (int j = 0; j < 2; j++) {
            r.se("chroma_weight_l1");
            r.se("chroma_offset_l1");
          }
        }
      }
    }
  }

  if (info.nal_ref_idc != 0) {
    Section d(r.sb(), "dec_ref_pic_marking");
    if (info.idr) {
      bool no_output = false, long_term = false;
      r.flag("no_output_of_prior_pics_flag", no_output);
      r.flag("long_term_reference_flag", long_term);
      info.long_term_reference_flag = long_term;
    } else {
      bool adaptive = false;
      r.flag("adaptive_ref_pic_marking_mode_flag", adaptive);
      info.adaptive_ref_pic_marking = adaptive;
      if (adaptive) {
        int c = 0;
        while (c < 66 && !r.fail()) {
          uint32_t op = r.ue("memory_management_control_operation");
          if (op == 0) break;
          int arg1 = 0, arg2 = 0;
          if (op == 1 || op == 3)
            arg1 = static_cast<int>(r.ue("difference_of_pic_nums_minus1"));
          if (op == 2) arg1 = static_cast<int>(r.ue("long_term_pic_num"));
          if (op == 3 || op == 6)
            arg2 = static_cast<int>(r.ue("long_term_frame_idx"));
          if (op == 4)
            arg1 = static_cast<int>(r.ue("max_long_term_frame_idx_plus1"));
          info.mmco.push_back({static_cast<int>(op), arg1, arg2});
          c++;
        }
      }
    }
  }

  if (pps.entropy_coding_mode && info.slice_type != SliceType::I &&
      info.slice_type != SliceType::SI) {
    info.cabac_init_idc = static_cast<int>(r.ue("cabac_init_idc"));
  }
  info.slice_qp_delta = r.se("slice_qp_delta");
  if (info.slice_type == SliceType::SP || info.slice_type == SliceType::SI) {
    if (info.slice_type == SliceType::SP) {
      bool sp_for_switch = false;
      r.flag("sp_for_switch_flag", sp_for_switch);
    }
    info.slice_qs_delta = r.se("slice_qs_delta");
  }
  if (pps.deblocking_filter_control_present) {
    info.disable_deblocking_filter_idc =
        static_cast<int>(r.ue("disable_deblocking_filter_idc"));
    if (info.disable_deblocking_filter_idc != 1) {
      info.slice_alpha_c0_offset_div2 = r.se("slice_alpha_c0_offset_div2");
      info.slice_beta_offset_div2 = r.se("slice_beta_offset_div2");
    }
  }
  info.header_bits = br.pos();
  info.valid = !br.fail();
  if (br.fail()) return;

  // POC derivation (8.2.1). FrameNumOffset accumulates across frame_num
  // wrap-arounds; using only max_frame_num resets the POC at the second
  // wrap inside one IDR period.
  int max_frame_num = 1 << (sps.log2_max_frame_num_minus4 + 4);
  int frame_num_offset;
  if (info.idr)
    frame_num_offset = 0;
  else if (state.prev_frame_num_valid && state.prev_frame_num > info.frame_num)
    frame_num_offset = state.prev_frame_num_offset + max_frame_num;
  else
    frame_num_offset = state.prev_frame_num_offset;

  int poc = 0;
  int poc_msb = 0;
  if (sps.pic_order_cnt_type == 0) {
    int max_lsb = 1 << (sps.log2_max_poc_lsb_minus4 + 4);
    int lsb = info.pic_order_cnt_lsb;
    int msb;
    if (info.idr) {
      msb = 0;
    } else if (lsb < state.prev_poc_lsb &&
               (state.prev_poc_lsb - lsb) >= max_lsb / 2) {
      msb = state.prev_poc_msb + max_lsb;
    } else if (lsb > state.prev_poc_lsb &&
               (lsb - state.prev_poc_lsb) > max_lsb / 2) {
      msb = state.prev_poc_msb - max_lsb;
    } else {
      msb = state.prev_poc_msb;
    }
    poc_msb = msb;
    int top, bottom;
    if (info.field_pic) {
      if (info.bottom_field) {
        top = 0;
        bottom = msb + lsb;
      } else {
        top = msb + lsb;
        bottom = 0;
      }
    } else {
      top = msb + lsb;
      bottom = top + delta_poc_bottom;
    }
    poc = std::min(top, bottom);
  } else if (sps.pic_order_cnt_type == 1) {
    int abs_frame_num = 0;
    int cycle_len = static_cast<int>(sps.offset_for_ref_frame.size());
    if (cycle_len > 0) abs_frame_num = frame_num_offset + info.frame_num;
    if (info.nal_ref_idc == 0 && abs_frame_num > 0) abs_frame_num--;
    int expected = 0;
    if (abs_frame_num > 0 && cycle_len > 0) {
      int cycle_cnt = (abs_frame_num - 1) / cycle_len;
      int in_cycle = (abs_frame_num - 1) % cycle_len;
      int per_cycle = 0;
      for (int off : sps.offset_for_ref_frame) per_cycle += off;
      expected = cycle_cnt * per_cycle;
      for (int i = 0; i <= in_cycle; i++)
        expected += sps.offset_for_ref_frame[i];
    }
    if (info.nal_ref_idc == 0) expected += sps.offset_for_non_ref_pic;
    int top, bottom;
    if (info.field_pic) {
      if (info.bottom_field) {
        top = 0;
        bottom = expected + delta_poc0;
      } else {
        top = expected + delta_poc0;
        bottom = 0;
      }
    } else {
      top = expected + delta_poc0;
      bottom = top + sps.offset_for_top_to_bottom_field + delta_poc1;
    }
    poc = std::min(top, bottom);
  } else {
    if (info.idr)
      poc = 0;
    else if (info.nal_ref_idc == 0)
      poc = 2 * (frame_num_offset + info.frame_num) - 1;
    else
      poc = 2 * (frame_num_offset + info.frame_num);
  }
  info.poc = poc;
  info.frame_poc = poc;
  if (sb && emit_poc) sb->addInt("  PicOrderCnt", poc, br.pos(), 0);

  // Picture boundary bookkeeping. prevPicOrderCntMsb/Lsb come from the
  // previous reference picture only (8.2.1.1).
  if (info.first_mb_in_slice == 0) {
    if (sps.pic_order_cnt_type == 0 && info.nal_ref_idc != 0) {
      state.prev_poc_msb = poc_msb;
      state.prev_poc_lsb = info.pic_order_cnt_lsb;
    }
    state.prev_frame_num = info.frame_num;
    state.prev_frame_num_valid = true;
    state.prev_frame_num_offset = frame_num_offset;
  }
}

bool isKnownH264Profile(int profile_idc) {
  return isHighProfile(profile_idc) || profile_idc == 66 || profile_idc == 77 ||
         profile_idc == 88;
}

std::string h264ProfileName(int profile_idc) {
  switch (profile_idc) {
    case 66:
      return "Baseline";
    case 77:
      return "Main";
    case 88:
      return "Extended";
    case 100:
      return "High";
    case 110:
      return "High 10";
    case 122:
      return "High 4:2:2";
    case 244:
      return "High 4:4:4 Predictive";
    case 44:
      return "CAVLC 4:4:4 Intra";
    case 83:
      return "Scalable Baseline";
    case 86:
      return "Scalable High";
    case 118:
      return "Multiview High";
    case 128:
      return "Stereo High";
    case 138:
      return "Multiview Depth High";
    case 139:
      return "Enhanced Multiview Depth High";
    default:
      return "Profile " + std::to_string(profile_idc);
  }
}

std::string h264LevelName(int level_idc) {
  if (level_idc == 9) return "1b";
  if (level_idc % 10 == 0 && level_idc >= 10 && level_idc <= 62)
    return std::to_string(level_idc / 10);
  std::string s =
      std::to_string(level_idc / 10) + "." + std::to_string(level_idc % 10);
  return s;
}

std::string h264ChromaFormatName(int idc) {
  switch (idc) {
    case 0:
      return "monochrome";
    case 1:
      return "4:2:0";
    case 2:
      return "4:2:2";
    case 3:
      return "4:4:4";
    default:
      return "reserved";
  }
}

}  // namespace bs
