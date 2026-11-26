#!/usr/bin/env python3
"""Compare parsed syntax fields against FFmpeg's trace_headers output.

FFmpeg's H.264/HEVC bitstream filter prints every syntax element it reads,
with its bit position and value. This script pairs the first SPS/PPS/slice
sections of each access unit with the corresponding NALs parsed by bsinfo and
checks that every field present on both sides has the same value.
"""
import argparse
import json
import os
import re
import subprocess
import sys

TRACE_LINE = re.compile(
    r"^\[trace_headers[^\]]*\]\s+(\d+)\s+(\S+)\s+[01]+\s+=\s+(-?\d+)\s*$")
TRACE_TITLE = re.compile(r"^\[trace_headers[^\]]*\]\s+([A-Za-z].*?)\s*$")
TREE_LEAF = re.compile(
    r"^\s*([^=]+?)\s*=\s*(.*?)\s+\[(\d+)\+(\d+)\]\s*$")
BSINFO_NAL = re.compile(
    r"^\[\s*(\d+)\]\s+off=\d+\s+size=\d+\s+type=(\d+)")

H264_SECTIONS = {
    7: "Sequence Parameter Set",
    8: "Picture Parameter Set",
}
H265_SECTIONS = {
    32: "Video Parameter Set",
    33: "Sequence Parameter Set",
    34: "Picture Parameter Set",
}

# FFmpeg spells a few fields slightly differently from the specification.
ALIASES = {
    "gaps_in_frame_num_allowed_flag": "gaps_in_frame_num_value_allowed_flag",
}

# Fields that must be present on both sides for a meaningful comparison.
CORE_FIELDS = {
    "h264": {
        "Sequence Parameter Set": [
            "profile_idc", "level_idc", "seq_parameter_set_id",
            "log2_max_frame_num_minus4", "pic_order_cnt_type",
            "max_num_ref_frames", "pic_width_in_mbs_minus1",
            "pic_height_in_map_units_minus1", "frame_mbs_only_flag",
            "frame_cropping_flag", "nal_hrd_parameters_present_flag",
            "vcl_hrd_parameters_present_flag", "cpb_cnt_minus1",
            "bit_rate_scale", "cpb_size_scale", "bit_rate_value_minus1[0]",
            "cpb_size_value_minus1[0]", "cbr_flag[0]",
            "initial_cpb_removal_delay_length_minus1",
            "cpb_removal_delay_length_minus1",
            "dpb_output_delay_length_minus1", "time_offset_length",
            "low_delay_hrd_flag", "pic_struct_present_flag",
        ],
        "Picture Parameter Set": [
            "pic_parameter_set_id", "seq_parameter_set_id",
            "entropy_coding_mode_flag", "pic_init_qp_minus26",
        ],
        "Slice Header": [
            "first_mb_in_slice", "slice_type", "pic_parameter_set_id",
            "frame_num", "slice_qp_delta",
        ],
    },
    "h265": {
        "Video Parameter Set": [
            "vps_video_parameter_set_id", "vps_max_layers_minus1",
            "vps_max_sub_layers_minus1", "vps_temporal_id_nesting_flag",
            "vps_sub_layer_ordering_info_present_flag",
            "vps_max_dec_pic_buffering_minus1[0]",
            "vps_max_num_reorder_pics[0]", "vps_max_latency_increase_plus1[0]",
            "vps_max_layer_id", "vps_num_layer_sets_minus1",
            "vps_timing_info_present_flag", "vps_extension_flag",
        ],
        "Sequence Parameter Set": [
            "sps_video_parameter_set_id", "sps_max_sub_layers_minus1",
            "chroma_format_idc", "pic_width_in_luma_samples",
            "pic_height_in_luma_samples", "conformance_window_flag",
            "bit_depth_luma_minus8", "bit_depth_chroma_minus8",
            "vui_hrd_parameters_present_flag",
            "nal_hrd_parameters_present_flag",
            "vcl_hrd_parameters_present_flag",
            "sub_pic_hrd_params_present_flag", "bit_rate_scale",
            "cpb_size_scale", "initial_cpb_removal_delay_length_minus1",
            "au_cpb_removal_delay_length_minus1",
            "dpb_output_delay_length_minus1", "fixed_pic_rate_general_flag[0]",
            "elemental_duration_in_tc_minus1[0]", "cpb_cnt_minus1[0]",
            "bit_rate_value_minus1[0]", "cpb_size_value_minus1[0]",
            "cbr_flag[0]",
        ],
        "Picture Parameter Set": [
            "pps_pic_parameter_set_id", "pps_seq_parameter_set_id",
            "tiles_enabled_flag", "entropy_coding_sync_enabled_flag",
        ],
        "Slice Segment Header": [
            "first_slice_segment_in_pic_flag", "slice_pic_parameter_set_id",
            "slice_type", "slice_qp_delta",
        ],
    },
}


def run(cmd):
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True)
    if proc.returncode != 0:
        sys.stderr.write("command failed: " + " ".join(cmd) + "\n")
        sys.stderr.write(proc.stderr[-2000:])
        raise SystemExit(1)
    return proc


def parse_trace(text):
    sections = []
    current = None
    seen_packet = False
    for line in text.splitlines():
        m = TRACE_LINE.match(line)
        if m:
            if current is not None:
                current[1].append((m.group(2), int(m.group(3))))
            continue
        t = TRACE_TITLE.match(line)
        if not t:
            continue
        title = t.group(1)
        if title.startswith("Packet:"):
            seen_packet = True
            current = None
        elif title in ("Video Parameter Set", "Sequence Parameter Set",
                       "Picture Parameter Set", "Slice Header",
                       "Slice Segment Header"):
            current = (title, [])
            if seen_packet:
                sections.append(current)
        else:
            current = None
    return [s for s in sections if s[1]]


def nal_list(bsinfo, path):
    out = run([bsinfo, path]).stdout
    nals = []
    for line in out.splitlines():
        m = BSINFO_NAL.match(line)
        if m:
            nals.append((int(m.group(1)), int(m.group(2))))
    return nals


def tree_fields(bsinfo, path, index):
    out = run([bsinfo, path, "--tree", str(index)]).stdout
    fields = []
    for line in out.splitlines():
        m = TREE_LEAF.match(line)
        if not m:
            continue
        name, value = m.group(1).strip(), m.group(2).strip()
        try:
            number = int(value, 0)
        except ValueError:
            continue
        fields.append((name, number))
    return fields


def is_slice_type(codec, nal_type):
    if codec == "h264":
        return 1 <= nal_type <= 5
    return nal_type <= 31


def section_for(codec, nal_type):
    if codec == "h264":
        if nal_type in H264_SECTIONS:
            return H264_SECTIONS[nal_type]
        if is_slice_type(codec, nal_type):
            return "Slice Header"
        return None
    if nal_type in H265_SECTIONS:
        return H265_SECTIONS[nal_type]
    if is_slice_type(codec, nal_type):
        return "Slice Segment Header"
    return None


def normalize(name):
    return ALIASES.get(name, name)


def compare_stream(ffmpeg, bsinfo, path, codec):
    trace = run([ffmpeg, "-hide_banner", "-i", path, "-c:v", "copy",
                 "-bsf:v", "trace_headers", "-f", "null", "-"])
    trace_sections = parse_trace(trace.stderr)
    nals = nal_list(bsinfo, path)
    ours = []
    for index, nal_type in nals:
        section = section_for(codec, nal_type)
        if section:
            ours.append((section, tree_fields(bsinfo, path, index)))

    name = os.path.basename(path)
    if len(trace_sections) != len(ours):
        return [f"{name}: trace has {len(trace_sections)} sections, "
                f"bsinfo has {len(ours)}"]
    errors = []
    compared = 0
    for (title, theirs), (our_title, our_fields) in zip(trace_sections, ours):
        if title != our_title:
            errors.append(f"{name}: section order mismatch: trace {title}, "
                          f"bsinfo {our_title}")
            continue
        our_map = {}
        for field, value in our_fields:
            our_map.setdefault(normalize(field), value)
        their_map = {}
        for field, value in theirs:
            their_map.setdefault(normalize(field), value)
        for field, value in their_map.items():
            if field in our_map and our_map[field] != value:
                errors.append(f"{name}: {title} {field}: trace {value}, "
                              f"bsinfo {our_map[field]}")
            elif field in our_map:
                compared += 1
        core = CORE_FIELDS[codec].get(title, [])
        for field in core:
            field = normalize(field)
            if field not in their_map:
                continue  # ffmpeg version does not trace this field
            if field not in our_map:
                errors.append(f"{name}: {title} missing {field}")
    if compared < 20:
        errors.append(f"{name}: only {compared} fields compared")
    return errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ffmpeg", default="ffmpeg")
    ap.add_argument("--bsinfo", required=True)
    ap.add_argument("--vectors", required=True)
    args = ap.parse_args()

    manifest = json.load(open(os.path.join(args.vectors, "manifest.json")))
    targets = [v for v in manifest["raw"]
               if v["file"] in ("h264_b.h264", "h265_b.h265",
                                "h264_cbr.h264", "h265_cbr.h265")]
    failures = []
    for vector in targets:
        path = os.path.join(args.vectors, vector["file"])
        errors = compare_stream(args.ffmpeg, args.bsinfo, path, vector["codec"])
        print(f"{vector['file']}: {'ok' if not errors else 'FAILED'}")
        failures.extend(errors)
    for error in failures:
        print("  " + error)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
