#include "core/sei.h"

#include <algorithm>
#include <cstdio>

#include "core/fieldreader.h"

namespace bs {

namespace {

const char* h264PayloadName(uint32_t type) {
  switch (type) {
    case 0:
      return "buffering_period";
    case 1:
      return "pic_timing";
    case 2:
      return "pan_scan_rect";
    case 3:
      return "filler_payload";
    case 4:
      return "user_data_registered_itu_t_t35";
    case 5:
      return "user_data_unregistered";
    case 6:
      return "recovery_point";
    case 7:
      return "dec_ref_pic_marking_repetition";
    case 8:
      return "spare";
    case 9:
      return "scene_info";
    case 10:
      return "sub_seq_info";
    case 11:
      return "sub_seq_layer_characteristics";
    case 12:
      return "sub_seq_characteristics";
    case 13:
      return "full_frame_freeze";
    case 14:
      return "full_frame_freeze_release";
    case 15:
      return "full_frame_snapshot";
    case 16:
      return "progressive_refinement_segment_start";
    case 17:
      return "progressive_refinement_segment_end";
    case 18:
      return "motion_constrained_slice_group_set";
    case 19:
      return "film_grain_characteristics";
    case 20:
      return "deblocking_filter_display_preference";
    case 21:
      return "stereo_video_info";
    case 22:
      return "post_filter_hint";
    case 23:
      return "tone_mapping_info";
    case 24:
      return "scalability_info";
    case 25:
      return "sub_pic_scalable_layer";
    case 26:
      return "non_required_layer_rep";
    case 27:
      return "priority_layer_info";
    case 28:
      return "layers_not_present";
    case 29:
      return "layer_dependency_change";
    case 30:
      return "scalable_nesting";
    case 31:
      return "base_layer_temporal_hrd";
    case 32:
      return "quality_layer_integrity_check";
    case 33:
      return "redundant_pic_property";
    case 34:
      return "tl0_dep_rep_index";
    case 35:
      return "tl_switching_point";
    case 36:
      return "parallel_decoding_info";
    case 37:
      return "mvc_scalable_nesting";
    case 38:
      return "view_scalability_info";
    case 39:
      return "multiview_scene_info";
    case 40:
      return "multiview_acquisition_info";
    case 41:
      return "non_required_view_component";
    case 42:
      return "view_dependency_change";
    case 43:
      return "operation_points_not_present";
    case 44:
      return "base_view_temporal_hrd";
    case 45:
      return "frame_packing_arrangement";
    case 46:
      return "multiview_view_position";
    case 47:
      return "display_orientation";
    case 48:
      return "mvcd_scalable_nesting";
    case 49:
      return "mvcd_view_scalability_info";
    case 50:
      return "depth_representation_info";
    case 51:
      return "three_dimensional_reference_displays_info";
    case 52:
      return "depth_timing";
    case 53:
      return "depth_sampling_info";
    case 54:
      return "constrained_depth_parameter_set_identifier";
    case 56:
      return "green_metadata";
    case 137:
      return "mastering_display_colour_volume";
    case 142:
      return "colour_remapping_info";
    case 144:
      return "content_light_level_info";
    case 147:
      return "alternative_transfer_characteristics";
    case 148:
      return "ambient_viewing_environment";
    default:
      return "reserved";
  }
}

const char* h265PayloadName(uint32_t type) {
  switch (type) {
    case 0:
      return "buffering_period";
    case 1:
      return "pic_timing";
    case 2:
      return "pan_scan_rect";
    case 3:
      return "filler_payload";
    case 4:
      return "user_data_registered_itu_t_t35";
    case 5:
      return "user_data_unregistered";
    case 6:
      return "recovery_point";
    case 45:
      return "frame_packing_arrangement";
    case 46:
      return "multiview_view_position";
    case 47:
      return "display_orientation";
    case 128:
      return "structure_of_pictures_info";
    case 129:
      return "active_parameter_sets";
    case 130:
      return "decoding_unit_info";
    case 131:
      return "temporal_sub_layer_zero_idx";
    case 132:
      return "decoded_picture_hash";
    case 133:
      return "scalable_nesting";
    case 134:
      return "region_refresh_info";
    case 135:
      return "no_display";
    case 136:
      return "time_code";
    case 137:
      return "mastering_display_colour_volume";
    case 138:
      return "segmented_rect_frame_packing_arrangement";
    case 139:
      return "temporal_motion_constrained_tile_sets";
    case 140:
      return "chroma_resampling_filter_hint";
    case 141:
      return "knee_function_info";
    case 142:
      return "colour_remapping_info";
    case 143:
      return "deinterlaced_field_identification";
    case 144:
      return "content_light_level_info";
    case 145:
      return "dependent_rap_indication";
    case 146:
      return "coded_region_completion";
    case 147:
      return "alternative_transfer_characteristics";
    case 148:
      return "ambient_viewing_environment";
    case 149:
      return "content_colour_volume";
    case 150:
      return "equirectangular_projection";
    case 151:
      return "cubemap_projection";
    case 152:
      return "fisheye_video_info";
    case 154:
      return "sphere_rotation";
    case 155:
      return "regionwise_packing";
    case 156:
      return "omni_viewport";
    case 157:
      return "regional_nesting";
    case 158:
      return "mcts_extraction_info_sets";
    case 159:
      return "mcts_extraction_info_nesting";
    case 160:
      return "layers_not_present";
    case 161:
      return "inter_layer_constrained_tile_sets";
    case 162:
      return "bsp_nesting";
    case 163:
      return "bsp_initial_arrival_time";
    case 164:
      return "sub_bitstream_property";
    case 165:
      return "alpha_channel_info";
    case 166:
      return "overlay_info";
    case 167:
      return "temporal_mv_prediction_constraints";
    case 168:
      return "frame_field_info";
    case 176:
      return "three_dimensional_reference_displays_info";
    case 177:
      return "depth_representation_info";
    case 178:
      return "multiview_scene_info";
    case 179:
      return "multiview_acquisition_info";
    case 180:
      return "multiview_view_position";
    case 181:
      return "alternative_depth_info";
    case 200:
      return "sei_manifest";
    case 201:
      return "sei_prefix_indication";
    case 202:
      return "annotated_regions";
    case 203:
      return "subpic_level_info";
    case 204:
      return "sample_aspect_ratio_info";
    default:
      return "reserved";
  }
}

const char* primariesName(int value) {
  switch (value) {
    case 1:
      return "BT.709";
    case 9:
      return "BT.2020";
    case 12:
      return "ST 2113";
    default:
      return nullptr;
  }
}

const char* transferName(int value) {
  switch (value) {
    case 1:
      return "BT.709";
    case 14:
      return "BT.2020 10-bit";
    case 16:
      return "PQ";
    case 18:
      return "HLG";
    default:
      return nullptr;
  }
}

const char* matrixName(int value) {
  switch (value) {
    case 1:
      return "BT.709";
    case 9:
      return "BT.2020 NCL";
    default:
      return nullptr;
  }
}

void parseMasteringDisplay(FieldReader& r, uint32_t size, SeiInfo& out) {
  if (size < 24) {
    r.error("mastering display payload too short");
    return;
  }
  SeiMasteringDisplay md;
  for (int i = 0; i < 3; i++) {
    std::string x = "display_primaries_x[" + std::to_string(i) + "]";
    std::string y = "display_primaries_y[" + std::to_string(i) + "]";
    md.primaries_x[i] = static_cast<uint16_t>(r.u(16, x.c_str()));
    md.primaries_y[i] = static_cast<uint16_t>(r.u(16, y.c_str()));
  }
  md.white_point_x = static_cast<uint16_t>(r.u(16, "white_point_x"));
  md.white_point_y = static_cast<uint16_t>(r.u(16, "white_point_y"));
  md.max_luminance = r.u(32, "max_display_mastering_luminance");
  md.min_luminance = r.u(32, "min_display_mastering_luminance");
  if (r.fail()) return;
  md.present = true;
  out.hdr.mastering_display = md;
}

void parseContentLightLevel(FieldReader& r, uint32_t size, SeiInfo& out) {
  if (size < 4) {
    r.error("content light level payload too short");
    return;
  }
  SeiContentLightLevel cll;
  cll.max_cll = static_cast<uint16_t>(r.u(16, "max_content_light_level"));
  cll.max_fall = static_cast<uint16_t>(r.u(16, "max_pic_average_light_level"));
  if (r.fail()) return;
  cll.present = true;
  out.hdr.content_light_level = cll;
}

void parseAlternativeTransfer(FieldReader& r, uint32_t size, SeiInfo& out) {
  if (size < 1) {
    r.error("alternative transfer payload too short");
    return;
  }
  SeiAlternativeTransfer atc;
  atc.preferred_transfer_characteristics =
      static_cast<uint8_t>(r.u(8, "preferred_transfer_characteristics"));
  if (r.fail()) return;
  atc.present = true;
  out.hdr.alternative_transfer = atc;
}

void parseRecoveryPoint(FieldReader& r, bool hevc) {
  if (hevc) {
    r.ue("recovery_poc_cnt");
  } else {
    r.ue("recovery_frame_cnt");
  }
  bool exact = false;
  bool broken = false;
  r.flag("exact_match_flag", exact);
  r.flag("broken_link_flag", broken);
  if (!hevc) r.u(2, "changing_slice_group_idc");
}

void parseHdr10Plus(FieldReader& r, SeiHdrInfo& out) {
  SeiHdr10Plus& hp = out.hdr10plus;
  hp.present = true;
  auto left = [&]() { return r.br().bits_left(); };

  if (left() < 10) return;
  hp.application_version = static_cast<int>(r.u(8, "application_version"));
  hp.num_windows = static_cast<int>(r.u(2, "num_windows"));
  if (hp.num_windows < 1 || hp.num_windows > 3) {
    hp.num_windows = 0;
    return;
  }
  // Windows after the first carry explicit geometry: 19 bytes + 1 bit each.
  for (int w = 1; w < hp.num_windows; w++) {
    if (left() < 153) {
      r.error("HDR10+ window geometry truncated");
      return;
    }
    r.skip(16, "window_upper_left_corner_x");
    r.skip(16, "window_upper_left_corner_y");
    r.skip(16, "window_lower_right_corner_x");
    r.skip(16, "window_lower_right_corner_y");
    r.skip(16, "center_of_ellipse_x");
    r.skip(16, "center_of_ellipse_y");
    r.skip(8, "rotation_angle");
    r.skip(16, "semimajor_axis_internal_ellipse");
    r.skip(16, "semimajor_axis_external_ellipse");
    r.skip(16, "semiminor_axis_external_ellipse");
    r.skip(1, "overlap_process_option");
  }
  if (left() < 28) {
    r.error("HDR10+ targeted luminance truncated");
    return;
  }
  hp.targeted_max_luminance_raw =
      static_cast<int>(r.u(27, "targeted_system_display_maximum_luminance"));
  const bool targeted_peak =
      r.u(1, "targeted_system_display_actual_peak_luminance_flag") != 0;
  if (targeted_peak) {
    if (left() < 10) return;
    const uint32_t rows = r.u(5, "num_rows_targeted_system_display");
    const uint32_t cols = r.u(5, "num_cols_targeted_system_display");
    if (rows < 2 || rows > 25 || cols < 2 || cols > 25) return;
    if (left() < rows * cols * 4) return;
    r.skip(static_cast<int>(rows * cols * 4), "targeted_peak_luminance");
  }

  hp.windows.clear();
  hp.windows.resize(static_cast<size_t>(hp.num_windows));
  for (int w = 0; w < hp.num_windows; w++) {
    SeiHdr10PlusWindow& win = hp.windows[static_cast<size_t>(w)];
    if (left() < 3 * 17 + 17 + 4 + 10) {
      r.error("HDR10+ window statistics truncated");
      return;
    }
    for (int c = 0; c < 3; c++)
      win.maxscl[c] = static_cast<int>(r.u(17, "maxscl"));
    win.average_maxrgb = static_cast<int>(r.u(17, "average_maxrgb"));
    win.num_percentiles =
        static_cast<int>(r.u(4, "num_distribution_maxrgb_percentiles"));
    if (win.num_percentiles > 15 ||
        left() < static_cast<uint64_t>(win.num_percentiles) * 24 + 10) {
      r.error("HDR10+ percentiles truncated");
      return;
    }
    for (int i = 0; i < win.num_percentiles; i++) {
      r.skip(7, "percentage");
      r.skip(17, "percentile");
    }
    win.fraction_bright_pixels =
        static_cast<int>(r.u(10, "fraction_bright_pixels"));
  }

  if (left() < 1) return;
  const bool mastering_peak =
      r.u(1, "mastering_display_actual_peak_luminance_flag") != 0;
  if (mastering_peak) {
    if (left() < 10) return;
    const uint32_t rows = r.u(5, "num_rows_mastering_display");
    const uint32_t cols = r.u(5, "num_cols_mastering_display");
    if (rows < 2 || rows > 25 || cols < 2 || cols > 25) return;
    if (left() < rows * cols * 4) return;
    r.skip(static_cast<int>(rows * cols * 4), "mastering_peak_luminance");
  }

  for (int w = 0; w < hp.num_windows; w++) {
    SeiHdr10PlusWindow& win = hp.windows[static_cast<size_t>(w)];
    if (left() < 1) return;
    win.tone_mapping = r.u(1, "tone_mapping_flag") != 0;
    if (win.tone_mapping) {
      if (left() < 28) return;
      win.knee_point_x = static_cast<int>(r.u(12, "knee_point_x"));
      win.knee_point_y = static_cast<int>(r.u(12, "knee_point_y"));
      win.num_bezier_anchors =
          static_cast<int>(r.u(4, "num_bezier_curve_anchors"));
      if (win.num_bezier_anchors > 15 ||
          left() < static_cast<uint64_t>(win.num_bezier_anchors) * 10)
        return;
      r.skip(win.num_bezier_anchors * 10, "bezier_curve_anchors");
    }
    if (left() < 1) return;
    win.saturation_mapping = r.u(1, "color_saturation_mapping_flag") != 0;
    if (win.saturation_mapping) {
      if (left() < 6) return;
      win.saturation_weight =
          static_cast<int>(r.u(6, "color_saturation_weight"));
    }
  }
}

void parseRegisteredUserData(FieldReader& r, uint32_t size, SeiInfo& out) {
  if (size < 1) return;
  uint32_t country = r.u(8, "itu_t_t35_country_code");
  if (country == 0xFF) {
    r.u(8, "itu_t_t35_country_code_extension_byte");
    return;
  }
  if (r.br().bits_left() < 16) return;
  uint32_t provider = r.u(16, "itu_t_t35_terminal_provider_code");
  uint32_t oriented = 0;
  if (r.br().bits_left() >= 16) {
    oriented = r.u(16, "itu_t_t35_terminal_provider_oriented_code");
  }
  if (country == 0xB5 && provider == 0x003C && oriented == 0x0001) {
    out.hdr.hdr10_plus = true;
    if (r.br().bits_left() >= 8) {
      const uint32_t app_id = r.u(8, "application_identifier");
      if (app_id == 4) parseHdr10Plus(r, out.hdr);
    }
    return;
  }
  if (country == 0xB5 && provider == 0x003B) out.hdr.dolby_vision = true;
}

void parseUnregisteredUserData(FieldReader& r, uint32_t size) {
  if (size < 16) return;
  uint64_t start = r.br().pos();
  std::string uuid;
  char hex[4];
  for (int i = 0; i < 16; i++) {
    std::snprintf(hex, sizeof(hex), "%02x", r.uu(8));
    uuid += hex;
  }
  if (r.sb()) r.sb()->add("uuid_iso_iec_11578", uuid, start, 128);
}

void parseActiveParameterSets(FieldReader& r) {
  uint32_t count = r.ue("num_sps_ids_minus1");
  if (count > 63) {
    r.error("num_sps_ids_minus1 out of range");
    return;
  }
  for (uint32_t i = 0; i <= count && !r.fail(); i++) {
    std::string name = "active_seq_parameter_set_id[" + std::to_string(i) + "]";
    r.ue(name.c_str());
  }
}

void parseBufferingPeriod264(FieldReader& r, const SeiHrdContext& ctx,
                             SeiInfo& out) {
  SeiBufferingPeriod bp;
  bp.sps_id = static_cast<int>(r.ue("seq_parameter_set_id"));
  const int bits = std::max(1, ctx.initial_cpb_removal_delay_length);
  if (ctx.nal_hrd) {
    for (int i = 0; i <= ctx.nal_cpb_cnt_minus1 && !r.fail(); i++) {
      std::string delay =
          "initial_cpb_removal_delay[" + std::to_string(i) + "]";
      std::string offset =
          "initial_cpb_removal_delay_offset[" + std::to_string(i) + "]";
      bp.nal_initial_cpb_removal_delay.push_back(r.u(bits, delay.c_str()));
      bp.nal_initial_cpb_removal_offset.push_back(r.u(bits, offset.c_str()));
    }
  }
  if (ctx.vcl_hrd) {
    for (int i = 0; i <= ctx.vcl_cpb_cnt_minus1 && !r.fail(); i++) {
      std::string delay =
          "vcl_initial_cpb_removal_delay[" + std::to_string(i) + "]";
      std::string offset =
          "vcl_initial_cpb_removal_offset[" + std::to_string(i) + "]";
      bp.vcl_initial_cpb_removal_delay.push_back(r.u(bits, delay.c_str()));
      bp.vcl_initial_cpb_removal_offset.push_back(r.u(bits, offset.c_str()));
    }
  }
  if (r.fail()) return;
  bp.present = true;
  out.buffering_period = bp;
}

void parsePicTiming264(FieldReader& r, const SeiHrdContext& ctx, SeiInfo& out) {
  SeiPicTiming pt;
  if (ctx.nal_hrd || ctx.vcl_hrd) {
    const int cpb_bits = std::max(1, ctx.cpb_removal_delay_length);
    const int dpb_bits = std::max(1, ctx.dpb_output_delay_length);
    pt.cpb_removal_delay = r.u(cpb_bits, "cpb_removal_delay");
    pt.dpb_output_delay = r.u(dpb_bits, "dpb_output_delay");
    pt.has_delays = true;
  }
  if (ctx.pic_struct_present) {
    pt.pic_struct = r.u(4, "pic_struct");
    pt.has_pic_struct = true;
  }
  if (r.fail()) return;
  pt.present = true;
  out.pic_timing = pt;
}

void parseBufferingPeriod265(FieldReader& r, const SeiHrdContext& ctx,
                             SeiInfo& out) {
  const H265Hrd* hrd = ctx.h265_hrd;
  if (!hrd || hrd->sub_layers.empty()) return;
  int tid = ctx.highest_tid;
  if (tid < 0) tid = 0;
  if (tid >= static_cast<int>(hrd->sub_layers.size()))
    tid = static_cast<int>(hrd->sub_layers.size()) - 1;
  const H265HrdSubLayer& sl = hrd->sub_layers[static_cast<size_t>(tid)];
  const int cpb_cnt = static_cast<int>(sl.cpb_cnt_minus1) + 1;
  const int init_bits = std::max(
      1, static_cast<int>(hrd->initial_cpb_removal_delay_length_minus1) + 1);
  const int au_bits = std::max(
      1, static_cast<int>(hrd->au_cpb_removal_delay_length_minus1) + 1);
  const int dpb_bits =
      std::max(1, static_cast<int>(hrd->dpb_output_delay_length_minus1) + 1);

  SeiBufferingPeriod bp;
  bp.sps_id = static_cast<int>(r.ue("bp_seq_parameter_set_id"));
  if (!hrd->sub_pic_hrd_params_present) {
    r.flag("irap_cpb_params_present_flag", bp.irap_cpb_params_present);
  }
  if (bp.irap_cpb_params_present) {
    bp.cpb_delay_offset = r.u(au_bits, "cpb_delay_offset");
    bp.dpb_delay_offset = r.u(dpb_bits, "dpb_delay_offset");
  }
  r.flag("concatenation_flag", bp.concatenation_flag);
  bp.au_cpb_removal_delay_delta_minus1 =
      r.u(au_bits, "au_cpb_removal_delay_delta_minus1");
  if (hrd->nal_hrd_present) {
    for (int i = 0; i < cpb_cnt && !r.fail(); i++) {
      std::string idx = std::to_string(i);
      bp.nal_initial_cpb_removal_delay.push_back(r.u(
          init_bits, ("nal_initial_cpb_removal_delay[" + idx + "]").c_str()));
      bp.nal_initial_cpb_removal_offset.push_back(r.u(
          init_bits, ("nal_initial_cpb_removal_offset[" + idx + "]").c_str()));
      if (hrd->sub_pic_hrd_params_present || bp.irap_cpb_params_present) {
        r.u(init_bits,
            ("nal_initial_alt_cpb_removal_delay[" + idx + "]").c_str());
        r.u(init_bits,
            ("nal_initial_alt_cpb_removal_offset[" + idx + "]").c_str());
      }
    }
  }
  if (hrd->vcl_hrd_present) {
    for (int i = 0; i < cpb_cnt && !r.fail(); i++) {
      std::string idx = std::to_string(i);
      bp.vcl_initial_cpb_removal_delay.push_back(r.u(
          init_bits, ("vcl_initial_cpb_removal_delay[" + idx + "]").c_str()));
      bp.vcl_initial_cpb_removal_offset.push_back(r.u(
          init_bits, ("vcl_initial_cpb_removal_offset[" + idx + "]").c_str()));
      if (hrd->sub_pic_hrd_params_present || bp.irap_cpb_params_present) {
        r.u(init_bits,
            ("vcl_initial_alt_cpb_removal_delay[" + idx + "]").c_str());
        r.u(init_bits,
            ("vcl_initial_alt_cpb_removal_offset[" + idx + "]").c_str());
      }
    }
  }
  if (r.fail()) return;
  bp.present = true;
  out.buffering_period = bp;
}

void parsePicTiming265(FieldReader& r, const SeiHrdContext& ctx, SeiInfo& out) {
  const H265Hrd* hrd = ctx.h265_hrd;
  if (!hrd) return;
  SeiPicTiming pt;
  if (ctx.frame_field_info_present) {
    pt.pic_struct = r.u(4, "pic_struct");
    r.u(2, "source_scan_type");
    r.u(1, "duplicate_flag");
    pt.has_pic_struct = true;
  }
  if (hrd->nal_hrd_present || hrd->vcl_hrd_present) {
    const int au_bits = std::max(
        1, static_cast<int>(hrd->au_cpb_removal_delay_length_minus1) + 1);
    const int dpb_bits =
        std::max(1, static_cast<int>(hrd->dpb_output_delay_length_minus1) + 1);
    pt.au_cpb_removal_delay_minus1 =
        r.u(au_bits, "au_cpb_removal_delay_minus1");
    pt.pic_dpb_output_delay = r.u(dpb_bits, "pic_dpb_output_delay");
    pt.has_delays = true;
  }
  if (r.fail()) return;
  pt.present = true;
  out.pic_timing = pt;
}

void parseSeiPayload(FieldReader& r, uint32_t type, uint32_t size, bool hevc,
                     SeiInfo& out, const SeiHrdContext* ctx) {
  switch (type) {
    case 0:
      if (ctx && ctx->has_sps) {
        if (hevc)
          parseBufferingPeriod265(r, *ctx, out);
        else
          parseBufferingPeriod264(r, *ctx, out);
      } else if (hevc) {
        r.ue("sps_seq_parameter_set_id");
      } else {
        r.ue("seq_parameter_set_id");
      }
      break;
    case 1:
      if (ctx && ctx->has_sps) {
        if (hevc)
          parsePicTiming265(r, *ctx, out);
        else
          parsePicTiming264(r, *ctx, out);
      }
      break;
    case 4:
      parseRegisteredUserData(r, size, out);
      break;
    case 5:
      parseUnregisteredUserData(r, size);
      break;
    case 6:
      parseRecoveryPoint(r, hevc);
      break;
    case 129:
      if (hevc) parseActiveParameterSets(r);
      break;
    case 137:
      parseMasteringDisplay(r, size, out);
      break;
    case 144:
      parseContentLightLevel(r, size, out);
      break;
    case 147:
      parseAlternativeTransfer(r, size, out);
      break;
    default:
      break;
  }
}

}  // namespace

void parseSei(const Rbsp& rbsp, SyntaxBuilder* sb, bool hevc, SeiInfo& out,
              const SeiHrdContext* ctx) {
  out = SeiInfo();
  BitReader br(rbsp.data.data(), rbsp.data.size());
  Section s(sb, "sei_rbsp");
  int count = 0;
  while (br.more_rbsp_data() && !br.fail() && count < 64) {
    uint64_t type_pos = br.pos();
    uint32_t payload_type = 0;
    while (br.pos() < rbsp.data.size() * 8) {
      uint32_t b = br.u(8);
      payload_type += b;
      if (b != 255) break;
    }
    uint64_t size_pos = br.pos();
    uint32_t payload_size = 0;
    while (br.pos() < rbsp.data.size() * 8) {
      uint32_t b = br.u(8);
      payload_size += b;
      if (b != 255) break;
    }
    if (br.fail()) break;
    uint64_t payload_start = br.pos();
    Section p(sb, ("sei_message[" + std::to_string(count) + "]").c_str());
    const char* name = seiPayloadName(payload_type, hevc);
    if (sb) {
      sb->addUInt("payloadType", payload_type, type_pos, size_pos - type_pos);
      sb->add("  meaning", name, type_pos, 0);
      sb->addUInt("payloadSize", payload_size, size_pos,
                  payload_start - size_pos);
    }
    SeiMessage message;
    message.payload_type = payload_type;
    message.payload_size = payload_size;
    message.bit_pos = payload_start;
    message.name = name;
    out.messages.push_back(message);
    out.parsed = true;

    if (payload_size > br.bits_left() / 8) {
      if (sb) sb->addError("payload size exceeds remaining data");
      break;
    }
    if (payload_size > 0) {
      if (payload_start % 8 != 0) {
        if (sb) sb->addError("SEI payload is not byte aligned");
      } else {
        BitReader payload_br(rbsp.data.data() + payload_start / 8,
                             payload_size);
        FieldReader pr(payload_br, sb, payload_start);
        parseSeiPayload(pr, payload_type, payload_size, hevc, out, ctx);
        if (payload_br.fail()) {
          if (sb) sb->addError("SEI payload parse failed");
        }
        uint64_t used = payload_br.pos();
        uint64_t payload_bits = static_cast<uint64_t>(payload_size) * 8;
        if (sb && used < payload_bits) {
          sb->add("  payload",
                  "(" + std::to_string((payload_bits - used) / 8) + " bytes)",
                  payload_start + used, payload_bits - used);
        }
      }
    }
    br.skip(static_cast<uint32_t>(payload_size) * 8);
    count++;
  }
}

const char* seiPayloadName(uint32_t type, bool hevc) {
  return hevc ? h265PayloadName(type) : h264PayloadName(type);
}

std::string seiSummary(const SeiInfo& info) {
  std::string summary;
  for (const auto& message : info.messages) {
    if (!summary.empty()) summary += ", ";
    summary += message.name;
  }
  return summary;
}

void mergeSeiHdr(SeiHdrInfo& dst, const SeiHdrInfo& src) {
  if (src.mastering_display.present)
    dst.mastering_display = src.mastering_display;
  if (src.content_light_level.present)
    dst.content_light_level = src.content_light_level;
  if (src.alternative_transfer.present)
    dst.alternative_transfer = src.alternative_transfer;
  dst.hdr10_plus = dst.hdr10_plus || src.hdr10_plus;
  dst.dolby_vision = dst.dolby_vision || src.dolby_vision;
  if (src.hdr10plus.present) dst.hdr10plus = src.hdr10plus;
}

std::string formatHdrSummary(const SeiHdrInfo& sei,
                             const ColourDescription& colour) {
  const SeiMasteringDisplay& md = sei.mastering_display;
  const SeiContentLightLevel& cll = sei.content_light_level;
  int transfer =
      sei.alternative_transfer.present
          ? sei.alternative_transfer.preferred_transfer_characteristics
          : colour.transfer;
  bool hlg = transfer == 18;
  bool hdr10 = md.present || cll.present || transfer == 16;
  if (!hlg && !hdr10 && !sei.hdr10_plus && !sei.dolby_vision) return "";

  std::string text;
  if (sei.dolby_vision)
    text = "Dolby Vision";
  else if (sei.hdr10_plus)
    text = "HDR10+";
  else if (hlg)
    text = "HLG";
  else
    text = "HDR10";

  std::string detail;
  const char* primaries =
      colour.present ? primariesName(colour.primaries) : nullptr;
  const char* transfer_name = transferName(transfer);
  const char* matrix = colour.present ? matrixName(colour.matrix) : nullptr;
  if (primaries) detail = primaries;
  if (transfer_name) {
    if (!detail.empty()) detail += "/";
    detail += transfer_name;
  }
  if (matrix) {
    if (!detail.empty()) detail += "/";
    detail += matrix;
  }
  if (!detail.empty()) text += " (" + detail + ")";

  char buf[96];
  if (md.present) {
    std::snprintf(buf, sizeof(buf), " %.0f/%.4f nits",
                  static_cast<double>(md.max_luminance) / 10000.0,
                  static_cast<double>(md.min_luminance) / 10000.0);
    text += buf;
  }
  if (cll.present) {
    std::snprintf(buf, sizeof(buf), " MaxCLL %u/%u",
                  static_cast<unsigned>(cll.max_cll),
                  static_cast<unsigned>(cll.max_fall));
    text += buf;
  }
  if (sei.hdr10plus.present && !sei.hdr10plus.windows.empty()) {
    const SeiHdr10PlusWindow& w = sei.hdr10plus.windows.front();
    const double itm =
        static_cast<double>(sei.hdr10plus.targeted_max_luminance_raw) / 10000.0;
    std::snprintf(buf, sizeof(buf), " ITM %.0f nits maxscl %.3f/%.3f/%.3f", itm,
                  static_cast<double>(w.maxscl[0]) / 100000.0,
                  static_cast<double>(w.maxscl[1]) / 100000.0,
                  static_cast<double>(w.maxscl[2]) / 100000.0);
    text += buf;
  }
  return text;
}

}  // namespace bs
