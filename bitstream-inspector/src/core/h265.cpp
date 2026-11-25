#include "core/h265.h"

#include <algorithm>
#include <cstdio>

#include "core/fieldreader.h"

namespace bs {

namespace {

void parseProfileTierLevelData(FieldReader &r, H265ProfileTierLevel *ptl,
                               const char *name)
{
    Section s(r.sb(), name);
    uint32_t space = r.u(2, "general_profile_space");
    bool tier = false;
    r.flag("general_tier_flag", tier);
    uint32_t idc = r.u(5, "general_profile_idc");
    r.note("  profile_name", h265ProfileName(static_cast<int>(idc)));
    bool compat[32] = {};
    for (int j = 0; j < 32; j++) {
        std::string n = "general_profile_compatibility_flag[" + std::to_string(j) + "]";
        r.flag(n.c_str(), compat[j]);
    }
    bool progressive = false, interlaced = false, nonpacked = false, frameonly = false;
    r.flag("general_progressive_source_flag", progressive);
    r.flag("general_interlaced_source_flag", interlaced);
    r.flag("general_non_packed_constraint_flag", nonpacked);
    r.flag("general_frame_only_constraint_flag", frameonly);
    if (ptl) {
        ptl->profile_space = static_cast<int>(space);
        ptl->tier_flag = tier;
        ptl->profile_idc = static_cast<int>(idc);
        for (int j = 0; j < 32; j++)
            ptl->profile_compatibility[j] = compat[j];
        ptl->progressive_source = progressive;
        ptl->interlaced_source = interlaced;
        ptl->non_packed_constraint = nonpacked;
        ptl->frame_only_constraint = frameonly;
    }
    auto compat_idc = [&](int id) {
        return static_cast<int>(idc) == id || (id >= 0 && id < 32 && compat[id]);
    };
    if (compat_idc(4) || compat_idc(5) || compat_idc(6) || compat_idc(7) ||
        compat_idc(8) || compat_idc(9) || compat_idc(10)) {
        r.u(1, "general_max_12bit_constraint_flag");
        r.u(1, "general_max_10bit_constraint_flag");
        r.u(1, "general_max_8bit_constraint_flag");
        r.u(1, "general_max_422chroma_constraint_flag");
        r.u(1, "general_max_420chroma_constraint_flag");
        r.u(1, "general_max_monochrome_constraint_flag");
        r.u(1, "general_intra_constraint_flag");
        r.u(1, "general_one_picture_only_constraint_flag");
        r.u(1, "general_lower_bit_rate_constraint_flag");
        if (compat_idc(5) || compat_idc(9) || compat_idc(10)) {
            r.u(1, "general_max_14bit_constraint_flag");
            r.skip(33, "general_reserved_zero_33bits");
        } else {
            r.skip(34, "general_reserved_zero_34bits");
        }
    } else if (compat_idc(2)) {
        r.skip(7, "general_reserved_zero_7bits");
        r.u(1, "general_one_picture_only_constraint_flag");
        r.skip(35, "general_reserved_zero_35bits");
    } else {
        r.skip(43, "general_reserved_zero_43bits");
    }
    if (compat_idc(1) || compat_idc(2) || compat_idc(3) || compat_idc(4) ||
        compat_idc(5) || compat_idc(9)) {
        r.u(1, "general_inbld_flag");
    } else {
        r.u(1, "general_reserved_zero_bit");
    }
}

void parseProfileTierLevel(FieldReader &r, H265ProfileTierLevel &ptl,
                           int max_sub_layers_minus1, const char *name)
{
    Section s(r.sb(), name);
    parseProfileTierLevelData(r, &ptl, nullptr);
    uint32_t level = r.u(8, "general_level_idc");
    ptl.level_idc = static_cast<int>(level);
    r.note("  level_name", h265LevelName(ptl.level_idc));

    bool sub_profile[8] = {};
    bool sub_level[8] = {};
    for (int i = 0; i < max_sub_layers_minus1 && i < 8; i++) {
        std::string p = "sub_layer_profile_present_flag[" + std::to_string(i) + "]";
        std::string l = "sub_layer_level_present_flag[" + std::to_string(i) + "]";
        r.flag(p.c_str(), sub_profile[i]);
        r.flag(l.c_str(), sub_level[i]);
    }
    if (max_sub_layers_minus1 > 0) {
        for (int i = max_sub_layers_minus1; i < 8; i++)
            r.skip(2, "reserved_zero_2bits");
    }
    for (int i = 0; i < max_sub_layers_minus1 && i < 8; i++) {
        std::string base = "sub_layer[" + std::to_string(i) + "].";
        if (sub_profile[i]) {
            H265ProfileTierLevel tmp;
            Section ps(r.sb(), (base + "profile_tier_level").c_str());
            parseProfileTierLevelData(r, &tmp, nullptr);
        }
        if (sub_level[i]) {
            uint32_t lv = r.u(8, (base + "level_idc").c_str());
            r.note((base + "level_name").c_str(), h265LevelName(static_cast<int>(lv)));
        }
    }
}

void parseScalingListData(FieldReader &r, const char *name)
{
    Section s(r.sb(), name);
    for (int size_id = 0; size_id < 4; size_id++) {
        int matrix_count = (size_id == 3) ? 2 : 6;
        for (int matrix_id = 0; matrix_id < matrix_count; matrix_id++) {
            bool pred_mode = false;
            std::string base = "scaling_list[" + std::to_string(size_id) + "][" +
                               std::to_string(matrix_id) + "].";
            r.flag((base + "scaling_list_pred_mode_flag").c_str(), pred_mode);
            if (!pred_mode) {
                r.ue((base + "scaling_list_pred_matrix_id_delta").c_str());
            } else {
                int coef_num = std::min(64, 1 << (4 + size_id));
                if (size_id > 1)
                    r.se((base + "scaling_list_dc_coef_minus8").c_str());
                for (int i = 0; i < coef_num && !r.fail(); i++)
                    r.se((base + "scaling_list_delta_coef").c_str());
            }
        }
    }
}

H265Rps parseShortTermRps(FieldReader &r, const H265Sps &sps, int st_rps_idx,
                          bool slice_header, const std::string &name)
{
    Section s(r.sb(), name.c_str());
    H265Rps out;

    bool predict = false;
    if (st_rps_idx != 0)
        r.flag("inter_ref_pic_set_prediction_flag", predict);

    if (predict) {
        int ref_idx;
        if (slice_header) {
            int delta_idx = static_cast<int>(r.ue("delta_idx_minus1")) + 1;
            ref_idx = static_cast<int>(sps.st_rps.size()) - delta_idx;
        } else {
            ref_idx = st_rps_idx - 1;
        }
        if (ref_idx < 0 || ref_idx >= static_cast<int>(sps.st_rps.size())) {
            r.error("invalid reference RPS index");
            return out;
        }
        const H265Rps &ref = sps.st_rps[ref_idx];
        bool sign = false;
        r.flag("delta_rps_sign", sign);
        int abs_delta = static_cast<int>(r.ue("abs_delta_rps_minus1")) + 1;
        int delta_rps = sign ? -abs_delta : abs_delta;
        for (int j = 0; j <= ref.num_delta_pocs; j++) {
            bool used = false;
            std::string un = "used_by_curr_pic_flag[" + std::to_string(j) + "]";
            r.flag(un.c_str(), used);
            bool use_delta = true;
            if (!used) {
                std::string dn = "use_delta_flag[" + std::to_string(j) + "]";
                r.flag(dn.c_str(), use_delta);
            }
            if (used || use_delta) {
                int dp = delta_rps;
                if (j < ref.num_delta_pocs && j < static_cast<int>(ref.delta_pocs.size()))
                    dp += ref.delta_pocs[j];
                out.delta_pocs.push_back(dp);
                out.used.push_back(used ? 1 : 0);
                if (dp < 0)
                    out.num_negative++;
            }
            if (out.delta_pocs.size() >= 64) {
                r.error("RPS picture count out of range");
                break;
            }
        }
        out.num_delta_pocs = static_cast<int>(out.delta_pocs.size());
    } else {
        int neg = static_cast<int>(r.ue("num_negative_pics"));
        int pos = static_cast<int>(r.ue("num_positive_pics"));
        if (neg > 32 || pos > 32) {
            r.error("RPS picture count out of range");
            return out;
        }
        int prev = 0;
        for (int i = 0; i < neg; i++) {
            int delta = static_cast<int>(r.ue("delta_poc_s0_minus1")) + 1;
            bool used = false;
            r.flag("used_by_curr_pic_s0_flag", used);
            prev -= delta;
            out.delta_pocs.push_back(prev);
            out.used.push_back(used ? 1 : 0);
        }
        prev = 0;
        for (int i = 0; i < pos; i++) {
            int delta = static_cast<int>(r.ue("delta_poc_s1_minus1")) + 1;
            bool used = false;
            r.flag("used_by_curr_pic_s1_flag", used);
            prev += delta;
            out.delta_pocs.push_back(prev);
            out.used.push_back(used ? 1 : 0);
        }
        out.num_negative = neg;
        out.num_delta_pocs = neg + pos;
    }
    return out;
}

void parseHrdParameters(FieldReader &r, int max_sub_layers_minus1, const char *name)
{
    Section s(r.sb(), name);
    bool nal = false, vcl = false, sub_pic = false;
    r.flag("nal_hrd_parameters_present_flag", nal);
    r.flag("vcl_hrd_parameters_present_flag", vcl);
    if (nal || vcl) {
        r.flag("sub_pic_hrd_params_present_flag", sub_pic);
        if (sub_pic) {
            r.u(8, "tick_divisor_minus2");
            r.u(5, "du_cpb_removal_delay_increment_length_minus1");
            r.u(1, "sub_pic_cpb_params_in_pic_timing_sei_flag");
            r.u(5, "dpb_output_delay_du_length_minus1");
        }
        r.u(4, "bit_rate_scale");
        r.u(4, "cpb_size_scale");
        if (sub_pic)
            r.u(4, "cpb_size_du_scale");
        r.u(5, "initial_cpb_removal_delay_length_minus1");
        r.u(5, "au_cpb_removal_delay_length_minus1");
        r.u(5, "dpb_output_delay_length_minus1");
    }
    for (int i = 0; i <= max_sub_layers_minus1 && i < 8; i++) {
        bool fixed_general = false;
        r.flag("fixed_pic_rate_general_flag", fixed_general);
        bool fixed_within = fixed_general;
        if (!fixed_general)
            r.flag("fixed_pic_rate_within_cvs_flag", fixed_within);
        bool low_delay = false;
        int cpb_cnt = 0;
        if (fixed_within) {
            r.ue("elemental_duration_in_tc_minus1");
        } else {
            r.flag("low_delay_hrd_flag", low_delay);
        }
        if (!low_delay) {
            cpb_cnt = static_cast<int>(r.ue("cpb_cnt_minus1"));
            if (cpb_cnt > 31) {
                r.error("cpb_cnt_minus1 out of range");
                return;
            }
        }
        if (nal || vcl) {
            for (int j = 0; j <= cpb_cnt; j++) {
                Section c(r.sb(), ("cpb[" + std::to_string(j) + "]").c_str());
                r.ue("bit_rate_value_minus1");
                r.ue("cpb_size_value_minus1");
                if (sub_pic) {
                    r.ue("cpb_size_du_value_minus1");
                    r.ue("bit_rate_du_value_minus1");
                }
                r.u(1, "cbr_flag");
            }
        }
    }
}

void parseH265Vui(FieldReader &r, H265Sps &sps)
{
    Section s(r.sb(), "vui_parameters");
    bool aspect = false;
    r.flag("aspect_ratio_info_present_flag", aspect);
    if (aspect) {
        uint32_t idc = r.u(8, "aspect_ratio_idc");
        if (idc == 255) {
            r.u(16, "sar_width");
            r.u(16, "sar_height");
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
        r.u(3, "video_format");
        r.u(1, "video_full_range_flag");
        bool colour = false;
        r.flag("colour_description_present_flag", colour);
        if (colour) {
            r.u(8, "colour_primaries");
            r.u(8, "transfer_characteristics");
            r.u(8, "matrix_coeffs");
        }
    }
    bool chroma_loc = false;
    r.flag("chroma_loc_info_present_flag", chroma_loc);
    if (chroma_loc) {
        r.ue("chroma_sample_loc_type_top_field");
        r.ue("chroma_sample_loc_type_bottom_field");
    }
    r.u(1, "neutral_chroma_indication_flag");
    bool field_seq = false;
    r.flag("field_seq_flag", field_seq);
    sps.field_seq_flag = field_seq;
    r.u(1, "frame_field_info_present_flag");
    bool default_window = false;
    r.flag("default_display_window_flag", default_window);
    if (default_window) {
        r.ue("def_disp_win_left_offset");
        r.ue("def_disp_win_right_offset");
        r.ue("def_disp_win_top_offset");
        r.ue("def_disp_win_bottom_offset");
    }
    bool timing = false;
    r.flag("vui_timing_info_present_flag", timing);
    sps.timing_info_present = timing;
    if (timing) {
        sps.num_units_in_tick = r.u(32, "vui_num_units_in_tick");
        sps.time_scale = r.u(32, "vui_time_scale");
        bool proportional = false;
        r.flag("vui_poc_proportional_to_timing_flag", proportional);
        if (proportional)
            r.ue("vui_num_ticks_poc_diff_one_minus1");
        bool hrd = false;
        r.flag("vui_hrd_parameters_present_flag", hrd);
        if (hrd)
            parseHrdParameters(r, sps.max_sub_layers_minus1, "hrd_parameters( )");
    }
    bool restriction = false;
    r.flag("bitstream_restriction_flag", restriction);
    if (restriction) {
        r.u(1, "tiles_fixed_structure_flag");
        r.u(1, "motion_vectors_over_pic_boundaries_flag");
        r.u(1, "restricted_ref_pic_lists_flag");
        r.ue("min_spatial_segmentation_idc");
        r.ue("max_bytes_per_pic_denom");
        r.ue("max_bits_per_min_cu_denom");
        r.ue("log2_max_mv_length_horizontal");
        r.ue("log2_max_mv_length_vertical");
    }
    sps.vui_present = true;
}

} // namespace

int H265Sps::display_width() const
{
    int chroma = chroma_array_type();
    int sub_w = (chroma == 1 || chroma == 2) ? 2 : 1;
    return width - sub_w * (conf_left + conf_right);
}

int H265Sps::display_height() const
{
    int chroma = chroma_array_type();
    int sub_h = (chroma == 1) ? 2 : 1;
    return height - sub_h * (conf_top + conf_bottom);
}

void parseH265Vps(BitReader &br, SyntaxBuilder *sb)
{
    FieldReader r(br, sb);
    Section s(sb, "video_parameter_set_rbsp");
    r.u(4, "vps_video_parameter_set_id");
    r.u(1, "vps_base_layer_internal_flag");
    r.u(1, "vps_base_layer_available_flag");
    r.u(6, "vps_max_layers_minus1");
    int max_sub = static_cast<int>(r.u(3, "vps_max_sub_layers_minus1"));
    r.u(1, "vps_temporal_id_nesting_flag");
    r.skip(16, "vps_reserved_0xffff_16bits");
    H265ProfileTierLevel ptl;
    parseProfileTierLevel(r, ptl, max_sub, "profile_tier_level( )");
    r.note("note", "remaining VPS fields not shown");
}

void parseH265Sps(BitReader &br, SyntaxBuilder *sb, H265Sps &sps)
{
    FieldReader r(br, sb);
    Section s(sb, "seq_parameter_set_rbsp");

    sps.vps_id = static_cast<int>(r.u(4, "sps_video_parameter_set_id"));
    int max_sub = static_cast<int>(r.u(3, "sps_max_sub_layers_minus1"));
    if (max_sub > 6) {
        r.error("sps_max_sub_layers_minus1 out of range");
        return;
    }
    sps.max_sub_layers_minus1 = max_sub;
    r.flag("sps_temporal_id_nesting_flag", sps.temporal_id_nesting);
    parseProfileTierLevel(r, sps.ptl, max_sub, "profile_tier_level( )");
    sps.sps_id = static_cast<int>(r.ue("sps_seq_parameter_set_id"));
    sps.chroma_format_idc = static_cast<int>(r.ue("chroma_format_idc"));
    if (sps.chroma_format_idc > 3) {
        r.error("chroma_format_idc out of range");
        return;
    }
    r.note("  chroma_format", h265ChromaFormatName(sps.chroma_format_idc));
    if (sps.chroma_format_idc == 3)
        r.flag("separate_colour_plane_flag", sps.separate_colour_plane);
    sps.width = static_cast<int>(r.ue("pic_width_in_luma_samples"));
    sps.height = static_cast<int>(r.ue("pic_height_in_luma_samples"));
    if (sps.width <= 0 || sps.height <= 0 || sps.width > 16384 ||
        sps.height > 16384) {
        r.error("picture size out of range");
        return;
    }
    r.flag("conformance_window_flag", sps.conformance_window);
    if (sps.conformance_window) {
        sps.conf_left = static_cast<int>(r.ue("conf_win_left_offset"));
        sps.conf_right = static_cast<int>(r.ue("conf_win_right_offset"));
        sps.conf_top = static_cast<int>(r.ue("conf_win_top_offset"));
        sps.conf_bottom = static_cast<int>(r.ue("conf_win_bottom_offset"));
        if (sps.conf_left < 0 || sps.conf_right < 0 || sps.conf_top < 0 ||
            sps.conf_bottom < 0 ||
            static_cast<int64_t>(sps.conf_left) + sps.conf_right > sps.width ||
            static_cast<int64_t>(sps.conf_top) + sps.conf_bottom > sps.height) {
            r.error("conformance window out of range");
            return;
        }
    }
    r.note("  display_size", std::to_string(sps.display_width()) + "x" +
                                 std::to_string(sps.display_height()));
    sps.bit_depth_luma_minus8 = static_cast<int>(r.ue("bit_depth_luma_minus8"));
    sps.bit_depth_chroma_minus8 = static_cast<int>(r.ue("bit_depth_chroma_minus8"));
    sps.log2_max_poc_lsb_minus4 = static_cast<int>(r.ue("log2_max_pic_order_cnt_lsb_minus4"));
    if (sps.log2_max_poc_lsb_minus4 > 12) {
        r.error("log2_max_pic_order_cnt_lsb_minus4 out of range");
        return;
    }
    bool sub_layer_order = false;
    r.flag("sps_sub_layer_ordering_info_present_flag", sub_layer_order);
    int start = sub_layer_order ? 0 : max_sub;
    for (int i = start; i <= max_sub; i++) {
        std::string base = "layer[" + std::to_string(i) + "].";
        uint32_t buffering = r.ue((base + "sps_max_dec_pic_buffering_minus1").c_str());
        uint32_t reorder = r.ue((base + "sps_max_num_reorder_pics").c_str());
        uint32_t latency = r.ue((base + "sps_max_latency_increase_plus1").c_str());
        if (i == max_sub) {
            sps.max_dec_pic_buffering_minus1 = static_cast<int>(buffering);
            sps.max_num_reorder_pics = static_cast<int>(reorder);
            sps.max_latency_increase_plus1 = static_cast<int>(latency);
        }
    }
    sps.log2_min_cb_minus3 = static_cast<int>(r.ue("log2_min_luma_coding_block_size_minus3"));
    sps.log2_diff_max_min_cb =
        static_cast<int>(r.ue("log2_diff_max_min_luma_coding_block_size"));
    sps.log2_min_tb_minus2 =
        static_cast<int>(r.ue("log2_min_luma_transform_block_size_minus2"));
    sps.log2_diff_max_min_tb =
        static_cast<int>(r.ue("log2_diff_max_min_luma_transform_block_size"));
    if (sps.log2_min_cb_minus3 > 3 || sps.log2_diff_max_min_cb > 6 ||
        sps.log2_min_tb_minus2 > 3 || sps.log2_diff_max_min_tb > 5) {
        r.error("coding block size fields out of range");
        return;
    }
    r.ue("max_transform_hierarchy_depth_inter");
    r.ue("max_transform_hierarchy_depth_intra");
    r.flag("scaling_list_enabled_flag", sps.scaling_list_enabled);
    if (sps.scaling_list_enabled) {
        bool present = false;
        r.flag("sps_scaling_list_data_present_flag", present);
        if (present)
            parseScalingListData(r, "scaling_list_data( )");
    }
    r.flag("amp_enabled_flag", sps.amp_enabled);
    r.flag("sample_adaptive_offset_enabled_flag", sps.sao_enabled);
    r.flag("pcm_enabled_flag", sps.pcm_enabled);
    if (sps.pcm_enabled) {
        sps.pcm_bit_depth_luma_minus1 = static_cast<int>(r.u(4, "pcm_sample_bit_depth_luma_minus1"));
        sps.pcm_bit_depth_chroma_minus1 =
            static_cast<int>(r.u(4, "pcm_sample_bit_depth_chroma_minus1"));
        sps.pcm_log2_min_cb_minus3 =
            static_cast<int>(r.ue("log2_min_pcm_luma_coding_block_size_minus3"));
        sps.pcm_log2_diff_max_min_cb =
            static_cast<int>(r.ue("log2_diff_max_min_pcm_luma_coding_block_size"));
        r.flag("pcm_loop_filter_disabled_flag", sps.pcm_loop_filter_disabled);
    }
    int st_count = static_cast<int>(r.ue("num_short_term_ref_pic_sets"));
    if (st_count > 64) {
        r.error("num_short_term_ref_pic_sets out of range");
        return;
    }
    sps.st_rps.resize(st_count);
    for (int i = 0; i < st_count && !r.fail(); i++) {
        sps.st_rps[i] = parseShortTermRps(
            r, sps, i, false, "st_ref_pic_set[" + std::to_string(i) + "]");
    }
    r.flag("long_term_ref_pics_present_flag", sps.long_term_ref_pics_present);
    if (sps.long_term_ref_pics_present) {
        sps.num_long_term_ref_pics_sps =
            static_cast<int>(r.ue("num_long_term_ref_pics_sps"));
        if (sps.num_long_term_ref_pics_sps > 32) {
            r.error("num_long_term_ref_pics_sps out of range");
            return;
        }
        sps.lt_poc_lsb_sps.resize(sps.num_long_term_ref_pics_sps);
        sps.lt_used_sps.resize(sps.num_long_term_ref_pics_sps);
        for (int i = 0; i < sps.num_long_term_ref_pics_sps; i++) {
            auto &poc = sps.lt_poc_lsb_sps[i];
            poc = static_cast<int>(r.u(sps.log2_max_poc_lsb_minus4 + 4, "lt_ref_pic_poc_lsb_sps"));
            bool used = false;
            r.flag("used_by_curr_pic_lt_sps_flag", used);
            sps.lt_used_sps[i] = used ? 1 : 0;
        }
    }
    r.flag("sps_temporal_mvp_enabled_flag", sps.temporal_mvp_enabled);
    r.flag("strong_intra_smoothing_enabled_flag", sps.strong_intra_smoothing);
    bool vui = false;
    r.flag("vui_parameters_present_flag", vui);
    if (vui && !r.fail())
        parseH265Vui(r, sps);
    bool extension = false;
    r.flag("sps_extension_present_flag", extension);
    if (extension) {
        bool range = false, multilayer = false, sps3d = false, scc = false;
        r.flag("sps_range_extension_flag", range);
        r.flag("sps_multilayer_extension_flag", multilayer);
        r.flag("sps_3d_extension_flag", sps3d);
        r.flag("sps_scc_extension_flag", scc);
        uint32_t ext4 = r.u(4, "sps_extension_4bits");
        if (range) {
            r.u(1, "transform_skip_rotation_enabled_flag");
            r.u(1, "transform_skip_context_enabled_flag");
            r.u(1, "implicit_rdpcm_enabled_flag");
            r.u(1, "explicit_rdpcm_enabled_flag");
            r.u(1, "extended_precision_processing_flag");
            r.u(1, "intra_smoothing_disabled_flag");
            r.u(1, "high_precision_offsets_enabled_flag");
            r.u(1, "persistent_rice_adaptation_enabled_flag");
            r.u(1, "cabac_bypass_alignment_enabled_flag");
            (void)ext4;
            r.note("note", "sps_extension_4bits: " + std::to_string(ext4));
        } else if (multilayer || sps3d || scc) {
            r.note("note", "multilayer/3D/SCC SPS extension not parsed");
        } else if (ext4) {
            r.skip(static_cast<int>(ext4), "sps_extension_data");
        }
    }
    sps.parsed_fully = !r.fail();
}

void parseH265Pps(BitReader &br, SyntaxBuilder *sb, H265Pps &pps)
{
    FieldReader r(br, sb);
    Section s(sb, "pic_parameter_set_rbsp");
    pps.pps_id = static_cast<int>(r.ue("pps_pic_parameter_set_id"));
    pps.sps_id = static_cast<int>(r.ue("pps_seq_parameter_set_id"));
    r.flag("dependent_slice_segments_enabled_flag",
           pps.dependent_slice_segments_enabled);
    r.flag("output_flag_present_flag", pps.output_flag_present);
    pps.num_extra_slice_header_bits =
        static_cast<int>(r.u(3, "num_extra_slice_header_bits"));
    r.flag("sign_data_hiding_enabled_flag", pps.sign_data_hiding_enabled);
    r.flag("cabac_init_present_flag", pps.cabac_init_present);
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
    pps.init_qp_minus26 = r.se("init_qp_minus26");
    r.flag("constrained_intra_pred_flag", pps.constrained_intra_pred);
    r.flag("transform_skip_enabled_flag", pps.transform_skip_enabled);
    r.flag("cu_qp_delta_enabled_flag", pps.cu_qp_delta_enabled);
    if (pps.cu_qp_delta_enabled)
        pps.diff_cu_qp_delta_depth = static_cast<int>(r.ue("diff_cu_qp_delta_depth"));
    pps.cb_qp_offset = r.se("pps_cb_qp_offset");
    pps.cr_qp_offset = r.se("pps_cr_qp_offset");
    r.flag("pps_slice_chroma_qp_offsets_present_flag",
           pps.slice_chroma_qp_offsets_present);
    r.flag("weighted_pred_flag", pps.weighted_pred);
    r.flag("weighted_bipred_flag", pps.weighted_bipred);
    r.flag("transquant_bypass_enabled_flag", pps.transquant_bypass_enabled);
    r.flag("tiles_enabled_flag", pps.tiles_enabled);
    r.flag("entropy_coding_sync_enabled_flag", pps.entropy_coding_sync_enabled);
    if (pps.tiles_enabled) {
        pps.num_tile_columns_minus1 = static_cast<int>(r.ue("num_tile_columns_minus1"));
        pps.num_tile_rows_minus1 = static_cast<int>(r.ue("num_tile_rows_minus1"));
        r.flag("uniform_spacing_flag", pps.uniform_spacing);
        if (!pps.uniform_spacing) {
            if (pps.num_tile_columns_minus1 > 63 || pps.num_tile_rows_minus1 > 63) {
                r.error("tile count out of range");
                return;
            }
            for (int i = 0; i < pps.num_tile_columns_minus1; i++)
                r.ue("column_width_minus1");
            for (int i = 0; i < pps.num_tile_rows_minus1; i++)
                r.ue("row_height_minus1");
        }
        r.flag("loop_filter_across_tiles_enabled_flag",
               pps.loop_filter_across_tiles_enabled);
    }
    r.flag("pps_loop_filter_across_slices_enabled_flag",
           pps.loop_filter_across_slices_enabled);
    r.flag("deblocking_filter_control_present_flag",
           pps.deblocking_filter_control_present);
    if (pps.deblocking_filter_control_present) {
        r.flag("deblocking_filter_override_enabled_flag",
               pps.deblocking_filter_override_enabled);
        r.flag("pps_deblocking_filter_disabled_flag", pps.deblocking_filter_disabled);
        if (!pps.deblocking_filter_disabled) {
            pps.beta_offset_div2 = r.se("pps_beta_offset_div2");
            pps.tc_offset_div2 = r.se("pps_tc_offset_div2");
        }
    }
    bool scaling = false;
    r.flag("pps_scaling_list_data_present_flag", scaling);
    if (scaling)
        parseScalingListData(r, "scaling_list_data( )");
    r.flag("lists_modification_present_flag", pps.lists_modification_present);
    pps.log2_parallel_merge_level_minus2 =
        static_cast<int>(r.ue("log2_parallel_merge_level_minus2"));
    r.flag("slice_segment_header_extension_present_flag",
           pps.slice_segment_header_extension_present);
    bool extension = false;
    r.flag("pps_extension_present_flag", extension);
    if (extension) {
        bool range = false, multilayer = false, pps3d = false, scc = false;
        r.flag("pps_range_extension_flag", range);
        r.flag("pps_multilayer_extension_flag", multilayer);
        r.flag("pps_3d_extension_flag", pps3d);
        r.flag("pps_scc_extension_flag", scc);
        uint32_t ext4 = r.u(4, "pps_extension_4bits");
        if (range) {
            r.ue("log2_max_transform_skip_block_size_minus2");
            r.u(1, "cross_component_prediction_enabled_flag");
            bool chroma_list = false;
            r.flag("chroma_qp_offset_list_enabled_flag", chroma_list);
            pps.chroma_qp_offset_list_enabled = chroma_list;
            if (chroma_list) {
                pps.diff_cu_chroma_qp_offset_depth =
                    static_cast<int>(r.ue("diff_cu_chroma_qp_offset_depth"));
                int len = static_cast<int>(r.ue("chroma_qp_offset_list_len_minus1"));
                if (len > 5) {
                    r.error("chroma_qp_offset_list_len_minus1 out of range");
                    return;
                }
                for (int i = 0; i <= len; i++) {
                    r.se("cb_qp_offset_list");
                    r.se("cr_qp_offset_list");
                }
            }
            r.ue("log2_sao_offset_scale_luma");
            r.ue("log2_sao_offset_scale_chroma");
        } else if (multilayer || pps3d || scc) {
            r.note("note", "multilayer/3D/SCC PPS extension not parsed");
        } else if (ext4) {
            r.skip(static_cast<int>(ext4), "pps_extension_data");
        }
    }
    pps.parsed_fully = !r.fail();
}

void parseH265SliceSegmentHeader(BitReader &br, SyntaxBuilder *sb, H265State &state,
                                 H265SliceInfo &info)
{
    FieldReader r(br, sb);
    Section s(sb, "slice_segment_header");
    const H265Sps &sps = state.sps;
    const H265Pps &pps = state.pps;

    info.valid = false;
    if (!state.have_sps || !state.have_pps) {
        r.error("no active SPS/PPS for this slice");
        return;
    }
    info.nal_type = state.nal_type;
    info.temporal_id_plus1 = state.temporal_id_plus1;
    info.irap = state.nal_type >= 16 && state.nal_type <= 23;
    info.idr = state.nal_type == 19 || state.nal_type == 20;

    r.flag("first_slice_segment_in_pic_flag", info.first_slice_segment_in_pic);
    if (info.irap) {
        bool no_output = false;
        r.flag("no_output_of_prior_pics_flag", no_output);
    }
    info.pps_id = static_cast<int>(r.ue("slice_pic_parameter_set_id"));
    if (info.pps_id != pps.pps_id)
        r.error("slice references a different PPS than the active one");

    bool dependent = false;
    if (!info.first_slice_segment_in_pic) {
        if (pps.dependent_slice_segments_enabled)
            r.flag("dependent_slice_segment_flag", dependent);
        int addr_bits = 1;
        int ctbs = sps.pic_size_in_ctbs();
        while ((1 << addr_bits) < ctbs)
            addr_bits++;
        info.slice_segment_address =
            static_cast<int>(r.u(addr_bits, "slice_segment_address"));
    }
    info.dependent_slice_segment = dependent;

    bool sao_luma = false, sao_chroma = false;
    bool deblocking_disabled = pps.deblocking_filter_disabled;
    bool temporal_mvp = false;
    int num_pic_total_curr = 0;

    if (!dependent) {
        for (int i = 0; i < pps.num_extra_slice_header_bits; i++) {
            std::string n = "slice_reserved_flag[" + std::to_string(i) + "]";
            r.u(1, n.c_str());
        }
        uint32_t st = r.ue("slice_type");
        if (st > 2) {
            r.error("slice_type out of range");
            return;
        }
        info.slice_type = st == 0 ? SliceType::B : (st == 1 ? SliceType::P : SliceType::I);
        r.note("  slice_type_name", std::string(1, sliceTypeChar(info.slice_type)) + " slice");
        if (pps.output_flag_present)
            r.u(1, "pic_output_flag");
        if (sps.separate_colour_plane)
            r.u(2, "colour_plane_id");
        if (!info.idr) {
            info.pic_order_cnt_lsb = static_cast<int>(
                r.u(sps.log2_max_poc_lsb_minus4 + 4, "slice_pic_order_cnt_lsb"));
            bool rps_sps = false;
            r.flag("short_term_ref_pic_set_sps_flag", rps_sps);
            H265Rps slice_rps;
            if (!rps_sps) {
                slice_rps = parseShortTermRps(r, sps, static_cast<int>(sps.st_rps.size()),
                                              true, "st_ref_pic_set( num_st_rps )");
            } else if (sps.st_rps.size() > 1) {
                int bits = 1;
                while ((size_t{1} << bits) < sps.st_rps.size())
                    bits++;
                int idx = static_cast<int>(r.u(bits, "short_term_ref_pic_set_idx"));
                if (idx >= 0 && idx < static_cast<int>(sps.st_rps.size()))
                    slice_rps = sps.st_rps[idx];
            } else if (sps.st_rps.size() == 1) {
                slice_rps = sps.st_rps[0];
            }
            num_pic_total_curr = 0;
            for (uint8_t used : slice_rps.used)
                num_pic_total_curr += used ? 1 : 0;

            int num_long_term_sps = 0, num_long_term_pics = 0;
            std::vector<int> lt_used;
            if (sps.long_term_ref_pics_present) {
                if (sps.num_long_term_ref_pics_sps > 0)
                    num_long_term_sps =
                        static_cast<int>(r.ue("num_long_term_sps"));
                num_long_term_pics = static_cast<int>(r.ue("num_long_term_pics"));
                if (num_long_term_sps > sps.num_long_term_ref_pics_sps ||
                    num_long_term_sps + num_long_term_pics > 32) {
                    r.error("long-term reference count out of range");
                    return;
                }
                for (int i = 0; i < num_long_term_sps + num_long_term_pics; i++) {
                    bool used = false;
                    if (i < num_long_term_sps) {
                        int idx = 0;
                        if (sps.num_long_term_ref_pics_sps > 1) {
                            int bits = 1;
                            while ((1 << bits) < sps.num_long_term_ref_pics_sps)
                                bits++;
                            idx = static_cast<int>(r.u(bits, "lt_idx_sps"));
                        }
                        if (idx >= 0 && idx < static_cast<int>(sps.lt_used_sps.size()))
                            used = sps.lt_used_sps[idx] != 0;
                    } else {
                        r.u(sps.log2_max_poc_lsb_minus4 + 4, "poc_lsb_lt");
                        r.flag("used_by_curr_pic_lt_flag", used);
                    }
                    bool msb_present = false;
                    r.flag("delta_poc_msb_present_flag", msb_present);
                    if (msb_present)
                        r.ue("delta_poc_msb_cycle_lt");
                    lt_used.push_back(static_cast<uint8_t>(used));
                }
            }
            int num_pic_total_curr_lt = num_pic_total_curr;
            for (uint8_t used : lt_used)
                num_pic_total_curr_lt += used ? 1 : 0;
            num_pic_total_curr = num_pic_total_curr_lt;
            if (sps.temporal_mvp_enabled)
                r.flag("slice_temporal_mvp_enabled_flag", temporal_mvp);

            if (num_pic_total_curr < 0)
                num_pic_total_curr = 0;
        }
        if (sps.sao_enabled) {
            r.flag("slice_sao_luma_flag", sao_luma);
            if (sps.chroma_array_type() != 0)
                r.flag("slice_sao_chroma_flag", sao_chroma);
        }
        info.sao_luma = sao_luma;
        info.sao_chroma = sao_chroma;

        int num_ref_l0 = pps.num_ref_idx_l0_default_active_minus1;
        int num_ref_l1 = pps.num_ref_idx_l1_default_active_minus1;
        if (info.slice_type == SliceType::P || info.slice_type == SliceType::B) {
            bool override_flag = false;
            r.flag("num_ref_idx_active_override_flag", override_flag);
            if (override_flag) {
                num_ref_l0 = static_cast<int>(r.ue("num_ref_idx_l0_active_minus1"));
                if (info.slice_type == SliceType::B)
                    num_ref_l1 = static_cast<int>(r.ue("num_ref_idx_l1_active_minus1"));
            }
            if (num_ref_l0 < 0 || num_ref_l0 > 31 || num_ref_l1 < 0 ||
                num_ref_l1 > 31) {
                r.error("num_ref_idx_active_minus1 out of range");
                return;
            }
            int total = num_pic_total_curr;
            if (pps.lists_modification_present && total > 1) {
                int bits = 1;
                while ((1 << bits) < total)
                    bits++;
                bool l0_mod = false;
                r.flag("ref_pic_list_modification_flag_l0", l0_mod);
                if (l0_mod) {
                    for (int i = 0; i <= num_ref_l0; i++)
                        r.u(bits, "list_entry_l0");
                }
                if (info.slice_type == SliceType::B) {
                    bool l1_mod = false;
                    r.flag("ref_pic_list_modification_flag_l1", l1_mod);
                    if (l1_mod) {
                        for (int i = 0; i <= num_ref_l1; i++)
                            r.u(bits, "list_entry_l1");
                    }
                }
            }
            if (info.slice_type == SliceType::B)
                r.u(1, "mvd_l1_zero_flag");
            if (pps.cabac_init_present)
                r.u(1, "cabac_init_flag");
            bool collocated_from_l0 = true;
            if (temporal_mvp) {
                if (info.slice_type == SliceType::B)
                    r.flag("collocated_from_l0_flag", collocated_from_l0);
                if ((collocated_from_l0 && num_ref_l0 > 0) ||
                    (!collocated_from_l0 && num_ref_l1 > 0))
                    r.ue("collocated_ref_idx");
            }
            if ((pps.weighted_pred && info.slice_type == SliceType::P) ||
                (pps.weighted_bipred && info.slice_type == SliceType::B)) {
                Section w(r.sb(), "pred_weight_table");
                r.ue("luma_log2_weight_denom");
                if (sps.chroma_array_type() != 0)
                    r.se("delta_chroma_log2_weight_denom");
                std::vector<uint8_t> luma_l0(num_ref_l0 + 1, 0);
                std::vector<uint8_t> chroma_l0(num_ref_l0 + 1, 0);
                for (int i = 0; i <= num_ref_l0; i++) {
                    bool f = false;
                    std::string n = "luma_weight_l0_flag[" + std::to_string(i) + "]";
                    r.flag(n.c_str(), f);
                    luma_l0[i] = f ? 1 : 0;
                }
                if (sps.chroma_array_type() != 0) {
                    for (int i = 0; i <= num_ref_l0; i++) {
                        bool f = false;
                        std::string n = "chroma_weight_l0_flag[" + std::to_string(i) + "]";
                        r.flag(n.c_str(), f);
                        chroma_l0[i] = f ? 1 : 0;
                    }
                }
                for (int i = 0; i <= num_ref_l0; i++) {
                    if (luma_l0[i]) {
                        r.se("delta_luma_weight_l0");
                        r.se("luma_offset_l0");
                    }
                    if (chroma_l0[i]) {
                        for (int j = 0; j < 2; j++) {
                            r.se("delta_chroma_weight_l0");
                            r.se("delta_chroma_offset_l0");
                        }
                    }
                }
                if (info.slice_type == SliceType::B) {
                    std::vector<uint8_t> luma_l1(num_ref_l1 + 1, 0);
                    std::vector<uint8_t> chroma_l1(num_ref_l1 + 1, 0);
                    for (int i = 0; i <= num_ref_l1; i++) {
                        bool f = false;
                        std::string n = "luma_weight_l1_flag[" + std::to_string(i) + "]";
                        r.flag(n.c_str(), f);
                        luma_l1[i] = f ? 1 : 0;
                    }
                    if (sps.chroma_array_type() != 0) {
                        for (int i = 0; i <= num_ref_l1; i++) {
                            bool f = false;
                            std::string n = "chroma_weight_l1_flag[" + std::to_string(i) + "]";
                            r.flag(n.c_str(), f);
                            chroma_l1[i] = f ? 1 : 0;
                        }
                    }
                    for (int i = 0; i <= num_ref_l1; i++) {
                        if (luma_l1[i]) {
                            r.se("delta_luma_weight_l1");
                            r.se("luma_offset_l1");
                        }
                        if (chroma_l1[i]) {
                            for (int j = 0; j < 2; j++) {
                                r.se("delta_chroma_weight_l1");
                                r.se("delta_chroma_offset_l1");
                            }
                        }
                    }
                }
            }
            r.ue("five_minus_max_num_merge_cand");
            if (sps.use_integer_mv)
                r.u(1, "slice_use_integer_mv_flag");
        }
        info.num_ref_idx_l0_active_minus1 = num_ref_l0;
        info.num_ref_idx_l1_active_minus1 = num_ref_l1;

        info.slice_qp_delta = r.se("slice_qp_delta");
        if (pps.slice_chroma_qp_offsets_present) {
            r.se("slice_cb_qp_offset");
            r.se("slice_cr_qp_offset");
        }
        if (pps.chroma_qp_offset_list_enabled) {
            bool cu_offset = false;
            r.flag("cu_chroma_qp_offset_enabled_flag", cu_offset);
        }
        if (pps.deblocking_filter_control_present) {
            bool override_flag = false;
            if (pps.deblocking_filter_override_enabled)
                r.flag("slice_deblocking_filter_override_flag", override_flag);
            if (override_flag) {
                bool disabled = false;
                r.flag("slice_deblocking_filter_disabled_flag", disabled);
                deblocking_disabled = disabled;
                if (!disabled) {
                    r.se("slice_beta_offset_div2");
                    r.se("slice_tc_offset_div2");
                }
            }
        }
        info.deblocking_filter_disabled = deblocking_disabled;
        if (pps.loop_filter_across_slices_enabled &&
            (sao_luma || sao_chroma || !deblocking_disabled)) {
            bool across = false;
            r.flag("slice_loop_filter_across_slices_enabled_flag", across);
        }
    }

    if (pps.tiles_enabled || pps.entropy_coding_sync_enabled) {
        uint32_t num_entry = r.ue("num_entry_point_offsets");
        if (num_entry > 0) {
            if (num_entry > 100000) {
                r.error("num_entry_point_offsets out of range");
                return;
            }
            int offset_len_minus1 = static_cast<int>(r.ue("offset_len_minus1"));
            if (offset_len_minus1 > 31) {
                r.error("offset_len_minus1 out of range");
                return;
            }
            for (uint32_t i = 0; i < num_entry && !r.fail(); i++)
                r.u(offset_len_minus1 + 1, "entry_point_offset_minus1");
        }
    }
    if (pps.slice_segment_header_extension_present) {
        uint32_t len = r.ue("slice_segment_header_extension_length");
        if (len > 4096) {
            r.error("slice_segment_header_extension_length out of range");
            return;
        }
        for (uint32_t i = 0; i < len && !r.fail(); i++)
            r.u(8, "slice_segment_header_extension_data_byte");
    }
    uint64_t align_pos = r.br().pos();
    if (!r.br().byte_align()) {
        if (sb)
            sb->addError("byte alignment failed");
    } else if (sb) {
        sb->add("byte_alignment", "(header end)", align_pos, r.br().pos() - align_pos);
    }
    info.header_bits = r.br().pos();
    info.valid = !r.br().fail();
}

std::string h265ProfileName(int profile_idc)
{
    switch (profile_idc) {
    case 1: return "Main";
    case 2: return "Main 10";
    case 3: return "Main Still Picture";
    case 4: return "Range Extensions";
    case 5: return "High Throughput";
    case 6: return "Multiview Main";
    case 7: return "Scalable Main";
    case 8: return "3D Main";
    case 9: return "Screen-Extended Main";
    case 10: return "High Throughput Screen-Extended";
    default: return "Profile " + std::to_string(profile_idc);
    }
}

std::string h265TierName(bool high) { return high ? "High" : "Main"; }

std::string h265LevelName(int level_idc)
{
    int major = level_idc / 30;
    int minor = (level_idc % 30) / 3;
    return std::to_string(major) + "." + std::to_string(minor);
}

std::string h265ChromaFormatName(int idc)
{
    switch (idc) {
    case 0: return "monochrome";
    case 1: return "4:2:0";
    case 2: return "4:2:2";
    case 3: return "4:4:4";
    default: return "reserved";
    }
}

std::string h265NalGroupName(int nal_type)
{
    if (nal_type >= 32) return "Non-VCL";
    if (nal_type >= 16 && nal_type <= 23) return "IRAP";
    return "VCL";
}

} // namespace bs
