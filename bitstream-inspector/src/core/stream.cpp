#include "core/stream.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "core/fieldreader.h"

namespace bs {

namespace {

const size_t kMaxFileSize = 512u * 1024u * 1024u;
const size_t kNoFrame = static_cast<size_t>(-1);
std::string fmt(const char* format, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, format);
  std::vsnprintf(buf, sizeof(buf), format, ap);
  va_end(ap);
  return buf;
}

const uint8_t* nalPayload(const std::vector<uint8_t>& data, const NalUnit& n) {
  return data.data() + n.payload_off;
}

uint64_t hashBytes(const uint8_t* data, size_t size) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < size; i++) {
    h ^= data[i];
    h *= 1099511628211ull;
  }
  return h;
}

std::string csvQuote(const std::string& value) {
  if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
  std::string out = "\"";
  for (char c : value) {
    if (c == '"')
      out += "\"\"";
    else
      out += c;
  }
  out += '"';
  return out;
}

// Reconstruct a SyntaxNode fragment for the NAL header itself.
SyntaxNode nalHeaderNode(const NalUnit& n, Codec codec) {
  SyntaxNode root;
  root.name = "nal_unit";
  root.nal_header = true;
  SyntaxNode t;
  t.name = "nal_unit_type";
  t.value = std::to_string(n.nal_type);
  t.bit_pos = 0;
  t.bit_len = codec == Codec::H265 ? 6 : 5;
  t.nal_header = true;
  root.children.push_back(t);
  SyntaxNode r;
  r.name = codec == Codec::H265 ? "nuh_layer_id" : "nal_ref_idc";
  r.nal_header = true;
  if (codec == Codec::H265) {
    r.value = "0";
    r.bit_len = 6;
  } else {
    r.value = std::to_string(n.nal_ref_idc);
    r.bit_len = 2;
  }
  r.bit_pos = codec == Codec::H265 ? 6 : 5;
  root.children.push_back(r);
  SyntaxNode name;
  name.name = "  description";
  name.value = n.type_name;
  name.bit_len = 0;
  name.nal_header = true;
  root.children.push_back(name);
  return root;
}

}  // namespace

const char* codecName(Codec c) {
  switch (c) {
    case Codec::H264:
      return "H.264/AVC";
    case Codec::H265:
      return "H.265/HEVC";
    default:
      return "Unknown";
  }
}

bool Stream::openFile(const std::string& path, std::string& error) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    error = "cannot open file: " + path;
    return false;
  }
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size < 0) {
    std::fclose(f);
    error = "cannot determine file size";
    return false;
  }
  if (static_cast<size_t>(size) > kMaxFileSize) {
    std::fclose(f);
    error = "file too large (limit 512 MiB for in-memory analysis)";
    return false;
  }
  std::vector<uint8_t> buf(static_cast<size_t>(size));
  size_t got = size ? std::fread(buf.data(), 1, buf.size(), f) : 0;
  std::fclose(f);
  if (got != buf.size()) {
    error = "short read";
    return false;
  }
  path_ = path;
  return loadBuffer(std::move(buf), error);
}

bool Stream::loadBuffer(std::vector<uint8_t> data, std::string& error) {
  data_ = std::move(data);
  path_.clear();
  nals_.clear();
  frames_.clear();
  h264_ = H264State();
  h265_ = H265State();
  hdr_ = SeiHdrInfo();
  cpb_ = CpbModelResult();
  h265_vps_ = H265Vps();
  have_h265_vps_ = false;
  h264_sps_versions_.clear();
  h264_pps_versions_.clear();
  h264_sps_hashes_.clear();
  h264_pps_hashes_.clear();
  h265_sps_versions_.clear();
  h265_pps_versions_.clear();
  h265_sps_hashes_.clear();
  h265_pps_hashes_.clear();
  h264_cur_sps_ = h264_cur_pps_ = -1;
  h265_cur_sps_ = h265_cur_pps_ = -1;
  h264_sps_idx_ = h264_pps_idx_ = h265_sps_idx_ = h265_pps_idx_ = SIZE_MAX;
  analyze(error);
  return codec_ != Codec::Unknown;
}

bool Stream::looksLikeHevc(const uint8_t* data, size_t size) const {
  // Try decoding the first parameter set both ways; the interpretation that
  // yields a sane SPS wins. Pure header-bit sniffing is not reliable because
  // H.264 SPS NALs also decode as plausible H.265 VPS NAL types.
  std::vector<NalUnit> as264, as265;
  NalSplitter::split(data, size, false, as264);
  NalSplitter::split(data, size, true, as265);

  auto scoreSps264 = [&]() -> int {
    int score = 0;
    int checked = 0;
    for (const auto& n : as264) {
      if (n.nal_type != 7 || n.size < 5) continue;
      Rbsp rbsp = unescapeRbsp(data + n.payload_off + 1, n.size - 1);
      BitReader br(rbsp.data.data(), rbsp.data.size());
      H264Sps sps;
      parseH264Sps(br, nullptr, sps);
      if (br.fail() || !sps.valid) return -10;
      int w = sps.width(), h = sps.height();
      if (w >= 16 && w <= 8192 && h >= 16 && h <= 8192 &&
          sps.log2_max_frame_num_minus4 <= 12 && sps.pic_order_cnt_type <= 2)
        score += 10;
      else
        return -10;
      if (isKnownH264Profile(sps.profile_idc)) score += 5;
      if (checked++ >= 2) break;
    }
    return checked ? score : -100;
  };

  auto scoreSps265 = [&]() -> int {
    int score = 0;
    int checked = 0;
    for (const auto& n : as265) {
      if (n.nal_type != 33 || n.size < 5) continue;
      Rbsp rbsp = unescapeRbsp(data + n.payload_off + 2, n.size - 2);
      BitReader br(rbsp.data.data(), rbsp.data.size());
      H265Sps sps;
      parseH265Sps(br, nullptr, sps);
      if (br.fail() || !sps.parsed_fully) return -10;
      if (sps.width >= 16 && sps.width <= 16384 && sps.height >= 16 &&
          sps.height <= 16384 && sps.max_sub_layers_minus1 <= 6)
        score += 10;
      else
        return -10;
      if (sps.ptl.profile_idc >= 1 && sps.ptl.profile_idc <= 11) score += 5;
      if (checked++ >= 2) break;
    }
    return checked ? score : -100;
  };

  int s264 = scoreSps264();
  int s265 = scoreSps265();
  return s265 > s264;
}

void Stream::analyze(std::string& error) {
  if (data_.empty()) {
    error = "empty file";
    return;
  }
  bool hevc = looksLikeHevc(data_.data(), data_.size());
  codec_ = hevc ? Codec::H265 : Codec::H264;
  if (hevc)
    analyzeH265(error);
  else
    analyzeH264(error);
  if (!frames_.empty()) {
    associateNonVcl();
    computeDisplayOrder();
    computeReferenceLists();
    analyzeSeiHrd();
    computeCpbModel();
  }
}

void Stream::associateNonVcl() {
  const size_t none = static_cast<size_t>(-1);
  // Non-VCL NALs between two access units are prefixes of the following one;
  // parameter sets before the first slice belong to the first frame.
  size_t pending = 0;
  int last_frame = -1;
  for (size_t i = 0; i < nals_.size(); i++) {
    if (!nals_[i].is_vcl || nals_[i].frame_index == none) continue;
    int frame = static_cast<int>(nals_[i].frame_index);
    for (size_t j = pending; j <= i; j++) {
      // Only parameter sets / SEI may be pulled forward as a prefix.
      // A VCL NAL whose header failed to parse stays orphaned instead of
      // being charged to the next picture.
      if (nals_[j].frame_index != none || nals_[j].is_vcl) continue;
      bool suffix = false;
      if (last_frame >= 0) {
        const int nal_type = nals_[j].unit.nal_type;
        if (codec_ == Codec::H265 && (nal_type == 40 || nal_type == 38))
          suffix = true;
        else if (codec_ == Codec::H264 && nal_type == 12)
          suffix = true;
      }
      nals_[j].frame_index = static_cast<size_t>(suffix ? last_frame : frame);
    }
    pending = i + 1;
    last_frame = frame;
  }
  if (last_frame >= 0) {
    for (size_t j = pending; j < nals_.size(); j++) {
      if (nals_[j].frame_index == none && !nals_[j].is_vcl)
        nals_[j].frame_index = static_cast<size_t>(last_frame);
    }
  }

  for (auto& f : frames_) {
    f.byte_offset = 0;
    f.byte_size = 0;
    f.nal_count = 0;
    f.slice_count = 0;
  }
  for (size_t i = 0; i < nals_.size(); i++) {
    size_t frame = nals_[i].frame_index;
    if (frame == none || frame >= frames_.size()) continue;
    FrameInfo& f = frames_[frame];
    const NalUnit& n = nals_[i].unit;
    if (f.nal_count == 0) f.byte_offset = n.offset;
    f.byte_size = n.payload_off + n.size - f.byte_offset;
    f.nal_count++;
    if (nals_[i].is_vcl) f.slice_count++;
  }
}

void Stream::analyzeH264(std::string& error) {
  std::vector<NalUnit> units;
  NalSplitter::split(data_.data(), data_.size(), false, units);
  if (units.empty()) {
    error = "no Annex B start codes found";
    codec_ = Codec::Unknown;
    return;
  }

  H264State state;  // replay running state
  int decode_index = 0;
  bool have_frame = false;
  FrameInfo cur;
  int last_slice_frame_num = -1;
  bool last_slice_idr = false;

  auto flush_frame = [&]() {
    if (!have_frame) return;
    cur.index = decode_index++;
    frames_.push_back(cur);
    have_frame = false;
  };

  for (size_t i = 0; i < units.size(); i++) {
    const NalUnit& n = units[i];
    NalEntry e;
    e.unit = n;
    const uint8_t* p = nalPayload(data_, n);
    int type = n.nal_type;
    bool vcl = (type >= 1 && type <= 5) || type == 20;
    e.is_vcl = vcl;
    Rbsp rbsp;

    if (type == 7) {
      rbsp = unescapeRbsp(p + 1, n.size - 1);
      BitReader br(rbsp.data.data(), rbsp.data.size());
      H264Sps sps;
      parseH264Sps(br, nullptr, sps);
      if (!br.fail() && sps.valid) {
        uint64_t hash = hashBytes(rbsp.data.data(), rbsp.data.size());
        int ver = h264_cur_sps_;
        if (ver < 0 || h264_sps_hashes_[static_cast<size_t>(ver)] != hash) {
          h264_sps_versions_.push_back(sps);
          h264_sps_hashes_.push_back(hash);
          ver = static_cast<int>(h264_sps_versions_.size()) - 1;
        }
        h264_cur_sps_ = ver;
        state.sps = h264_sps_versions_[static_cast<size_t>(ver)];
        state.have_sps = true;
        h264_sps_idx_ = i;
        e.sps_version = ver;
        e.summary = fmt("%dx%d, profile %s, level %s", sps.width(),
                        sps.height(), h264ProfileName(sps.profile_idc).c_str(),
                        h264LevelName(sps.level_idc).c_str());
      } else {
        e.error = "SPS parse failed";
      }
    } else if (type == 8) {
      rbsp = unescapeRbsp(p + 1, n.size - 1);
      BitReader br(rbsp.data.data(), rbsp.data.size());
      H264Pps pps;
      parseH264Pps(br, nullptr, pps, state.have_sps ? &state.sps : nullptr);
      if (!br.fail() && pps.valid) {
        uint64_t hash = hashBytes(rbsp.data.data(), rbsp.data.size());
        int ver = h264_cur_pps_;
        if (ver < 0 || h264_pps_hashes_[static_cast<size_t>(ver)] != hash) {
          h264_pps_versions_.push_back(pps);
          h264_pps_hashes_.push_back(hash);
          ver = static_cast<int>(h264_pps_versions_.size()) - 1;
        }
        h264_cur_pps_ = ver;
        state.pps = h264_pps_versions_[static_cast<size_t>(ver)];
        state.have_pps = true;
        h264_pps_idx_ = i;
        e.pps_version = ver;
        e.sps_version = h264_cur_sps_;
        e.summary = fmt("pps_id %d -> sps_id %d, %s", pps.pps_id, pps.sps_id,
                        pps.entropy_coding_mode ? "CABAC" : "CAVLC");
      } else {
        e.error = "PPS parse failed";
      }
    } else if (vcl) {
      e.sps_version = h264_cur_sps_;
      e.pps_version = h264_cur_pps_;
      rbsp = unescapeRbsp(p + 1, n.size - 1);
      BitReader br(rbsp.data.data(), rbsp.data.size());
      H264SliceInfo info;
      info.nal_ref_idc = n.nal_ref_idc;
      info.nal_type = type;
      parseH264SliceHeader(br, nullptr, state, info, false);
      if (info.valid) {
        bool new_picture = !have_frame;
        if (have_frame) {
          if (info.first_mb_in_slice == 0)
            new_picture = true;
          else if (info.frame_num != last_slice_frame_num)
            new_picture = true;
          else if (info.idr != last_slice_idr)
            new_picture = true;
        }
        if (new_picture) {
          flush_frame();
          cur = FrameInfo();
          cur.poc = info.poc;
          cur.idr = info.idr;
          cur.irap = info.idr;
          cur.ref = info.nal_ref_idc != 0;
          cur.field = info.field_pic;
          cur.frame_num = info.frame_num;
          cur.nal_type = type;
          cur.byte_offset = n.offset;
          cur.sps_version = h264_cur_sps_;
          cur.pps_version = h264_cur_pps_;
          cur.h264_slice = info;
          cur.picture_type = sliceTypeChar(info.slice_type);
          cur.qp = 26 + state.pps.pic_init_qp_minus26 + info.slice_qp_delta;
          have_frame = true;
        }
        cur.slice_count++;
        cur.slice_header_bits += info.header_bits;
        cur.byte_size = (n.payload_off + n.size) - cur.byte_offset;
        cur.nal_count++;
        e.frame_index = frames_.size();
        last_slice_frame_num = info.frame_num;
        last_slice_idr = info.idr;
        e.summary = fmt("%c slice, frame_num %d, POC %d, slice_qp_delta %d",
                        sliceTypeChar(info.slice_type), info.frame_num,
                        info.poc, info.slice_qp_delta);
      } else {
        e.error = "slice header parse failed";
      }
    } else if (type == 6) {
      rbsp = unescapeRbsp(p + 1, n.size - 1);
      parseSei(rbsp, nullptr, false, e.sei);
      e.summary = "SEI";
      if (!e.sei.messages.empty()) e.summary += ": " + seiSummary(e.sei);
      mergeSeiHdr(hdr_, e.sei.hdr);
    } else if (type == 9) {
      e.summary = "access unit delimiter";
    } else if (type == 12) {
      e.summary = "filler";
    } else if (type == 10) {
      e.summary = "end of sequence";
    } else if (type == 11) {
      e.summary = "end of stream";
    }
    nals_.push_back(std::move(e));
  }
  flush_frame();
  h264_ = state;

  if (frames_.empty())
    error = "no decodable slices found (framing may not be Annex B)";
}

void Stream::analyzeH265(std::string& error) {
  std::vector<NalUnit> units;
  NalSplitter::split(data_.data(), data_.size(), true, units);
  if (units.empty()) {
    error = "no Annex B start codes found";
    codec_ = Codec::Unknown;
    return;
  }

  H265State state;
  int decode_index = 0;
  bool have_frame = false;
  FrameInfo cur;
  int last_addr = -1;

  auto flush_frame = [&]() {
    if (!have_frame) return;
    cur.index = decode_index++;
    frames_.push_back(cur);
    have_frame = false;
  };

  for (size_t i = 0; i < units.size(); i++) {
    const NalUnit& n = units[i];
    NalEntry e;
    e.unit = n;
    const uint8_t* p = nalPayload(data_, n);
    int type = n.nal_type;
    bool vcl = type <= 31;
    e.is_vcl = vcl;
    Rbsp rbsp;
    int temporal_id_plus1 = n.size >= 2 ? (p[1] & 0x07) : 1;

    if (type == 32) {
      rbsp = unescapeRbsp(p + 2, n.size - 2);
      BitReader br(rbsp.data.data(), rbsp.data.size());
      H265Vps vps;
      parseH265Vps(br, nullptr, &vps);
      if (vps.parsed_fully) {
        state.vps = vps;
        state.have_vps = true;
        h265_vps_ = vps;
        have_h265_vps_ = true;
        e.summary = fmt("VPS %d, %d sub-layers", vps.vps_id,
                        vps.max_sub_layers_minus1 + 1);
      } else {
        e.error = "VPS parse failed";
      }
    } else if (type == 33) {
      rbsp = unescapeRbsp(p + 2, n.size - 2);
      BitReader br(rbsp.data.data(), rbsp.data.size());
      H265Sps sps;
      parseH265Sps(br, nullptr, sps);
      if (!br.fail() && sps.parsed_fully) {
        uint64_t hash = hashBytes(rbsp.data.data(), rbsp.data.size());
        int ver = h265_cur_sps_;
        if (ver < 0 || h265_sps_hashes_[static_cast<size_t>(ver)] != hash) {
          h265_sps_versions_.push_back(sps);
          h265_sps_hashes_.push_back(hash);
          ver = static_cast<int>(h265_sps_versions_.size()) - 1;
        }
        h265_cur_sps_ = ver;
        state.sps = h265_sps_versions_[static_cast<size_t>(ver)];
        state.have_sps = true;
        h265_sps_idx_ = i;
        e.sps_version = ver;
        e.summary =
            fmt("%dx%d, %s profile, level %s, %dx%d CTU", sps.width, sps.height,
                h265ProfileName(sps.ptl.profile_idc).c_str(),
                h265LevelName(sps.ptl.level_idc).c_str(), sps.ctb_size(),
                sps.ctb_size());
      } else {
        e.error = "SPS parse failed";
      }
    } else if (type == 34) {
      rbsp = unescapeRbsp(p + 2, n.size - 2);
      BitReader br(rbsp.data.data(), rbsp.data.size());
      H265Pps pps;
      parseH265Pps(br, nullptr, pps);
      if (!br.fail() && pps.parsed_fully) {
        uint64_t hash = hashBytes(rbsp.data.data(), rbsp.data.size());
        int ver = h265_cur_pps_;
        if (ver < 0 || h265_pps_hashes_[static_cast<size_t>(ver)] != hash) {
          h265_pps_versions_.push_back(pps);
          h265_pps_hashes_.push_back(hash);
          ver = static_cast<int>(h265_pps_versions_.size()) - 1;
        }
        h265_cur_pps_ = ver;
        state.pps = h265_pps_versions_[static_cast<size_t>(ver)];
        state.have_pps = true;
        h265_pps_idx_ = i;
        e.pps_version = ver;
        e.sps_version = h265_cur_sps_;
        e.summary = fmt("pps %d -> sps %d, %s", pps.pps_id, pps.sps_id,
                        pps.tiles_enabled ? "tiles" : "no tiles");
      } else {
        e.error = "PPS parse failed";
      }
    } else if (vcl) {
      e.sps_version = h265_cur_sps_;
      e.pps_version = h265_cur_pps_;
      rbsp = unescapeRbsp(p + 2, n.size - 2);
      BitReader br(rbsp.data.data(), rbsp.data.size());
      state.nal_type = type;
      state.temporal_id_plus1 = temporal_id_plus1;
      H265SliceInfo info;
      parseH265SliceSegmentHeader(br, nullptr, state, info);
      if (info.valid) {
        bool new_picture = !have_frame;
        if (have_frame) {
          if (info.first_slice_segment_in_pic)
            new_picture = true;
          else if (info.slice_segment_address <= last_addr)
            new_picture = true;
        }
        if (new_picture) {
          flush_frame();
          cur = FrameInfo();
          cur.idr = info.idr;
          cur.irap = info.irap;
          cur.ref = true;
          cur.frame_num = -1;
          cur.nal_type = type;
          cur.temporal_id = temporal_id_plus1 - 1;
          cur.byte_offset = n.offset;
          cur.sps_version = h265_cur_sps_;
          cur.pps_version = h265_cur_pps_;
          cur.h265_slice = info;
          cur.poc = info.pic_order_cnt_lsb;
          if (info.slice_type == SliceType::I || info.idr) {
            cur.picture_type = 'I';
          } else {
            cur.picture_type = sliceTypeChar(info.slice_type);
          }
          if ((type >= 16 && type <= 21)) cur.picture_type = 'I';
          cur.qp = 26 + state.pps.init_qp_minus26 + info.slice_qp_delta;
          have_frame = true;
        }
        cur.slice_count++;
        cur.slice_header_bits += info.header_bits;
        cur.byte_size = (n.payload_off + n.size) - cur.byte_offset;
        cur.nal_count++;
        e.frame_index = frames_.size();
        last_addr = info.slice_segment_address;
        e.frame_index = frames_.size();
        e.summary =
            fmt("%s (%s), type %c, POC %d, QP %d", n.type_name,
                h265NalGroupName(type).c_str(), sliceTypeChar(info.slice_type),
                info.pic_order_cnt_lsb, cur.qp);
      } else {
        e.error = "slice segment header parse failed";
      }
    } else if (type == 35) {
      e.summary = "access unit delimiter";
    } else if (type == 36) {
      e.summary = "end of sequence";
    } else if (type == 37) {
      e.summary = "end of bitstream";
    } else if (type == 38) {
      e.summary = "filler data";
    } else if (type == 39 || type == 40) {
      rbsp = unescapeRbsp(p + 2, n.size - 2);
      parseSei(rbsp, nullptr, true, e.sei);
      e.summary = type == 39 ? "prefix SEI" : "suffix SEI";
      if (!e.sei.messages.empty()) e.summary += ": " + seiSummary(e.sei);
      mergeSeiHdr(hdr_, e.sei.hdr);
    }
    nals_.push_back(std::move(e));
  }
  flush_frame();
  h265_ = state;

  if (frames_.empty())
    error = "no decodable slices found (framing may not be Annex B)";
}

namespace {

void addUnique(std::vector<int>& values, int value) {
  if (std::find(values.begin(), values.end(), value) == values.end())
    values.push_back(value);
}

}  // namespace

void Stream::computeReferenceLists() {
  if (codec_ == Codec::H265) {
    const H265Sps& sps = h265_.sps;
    int modulus = 1;
    if (sps.log2_max_poc_lsb_minus4 >= 0 && sps.log2_max_poc_lsb_minus4 < 30)
      modulus = 1 << (sps.log2_max_poc_lsb_minus4 + 4);
    std::vector<int> prior;
    for (auto& f : frames_) {
      f.refs = FrameRefs();
      if (!f.h265_slice.valid) continue;
      const H265SliceInfo& info = f.h265_slice;
      for (size_t i = 0; i < info.short_term_rps.delta_pocs.size(); i++) {
        if (i >= info.short_term_rps.used.size() ||
            !info.short_term_rps.used[i])
          continue;
        addUnique(f.refs.l0_pocs, f.poc + info.short_term_rps.delta_pocs[i]);
        if (f.picture_type == 'B')
          addUnique(f.refs.l1_pocs, f.poc + info.short_term_rps.delta_pocs[i]);
      }
      for (size_t i = 0; i < info.lt_poc_lsb.size(); i++) {
        if (i >= info.lt_used.size() || !info.lt_used[i]) continue;
        int base = f.poc - (modulus > 0 ? f.poc % modulus : 0);
        int ref = base + info.lt_poc_lsb[i];
        if (ref > f.poc + modulus / 2) ref -= modulus;
        if (ref < f.poc - modulus / 2) ref += modulus;
        if (i < info.lt_msb_present.size() && info.lt_msb_present[i] &&
            i < info.lt_delta_msb_cycle.size())
          ref -= static_cast<int>(info.lt_delta_msb_cycle[i]) * modulus;
        addUnique(f.refs.l0_pocs, ref);
        if (f.picture_type == 'B') addUnique(f.refs.l1_pocs, ref);
      }
      for (int ref : f.refs.l0_pocs) {
        if (std::find(prior.begin(), prior.end(), ref) == prior.end())
          addUnique(f.refs.missing_pocs, ref);
      }
      for (int ref : f.refs.l1_pocs) {
        if (std::find(prior.begin(), prior.end(), ref) == prior.end())
          addUnique(f.refs.missing_pocs, ref);
      }
      f.refs.dpb_count = static_cast<int>(f.refs.l0_pocs.size());
      f.refs.computed = true;
      addUnique(prior, f.poc);
    }
    return;
  }

  if (codec_ != Codec::H264) return;
  struct Entry {
    int poc = 0;
    int frame_num = 0;
    bool long_term = false;
  };
  const H264Sps& sps = h264_.sps;
  const int max_refs = sps.max_num_ref_frames > 0 ? sps.max_num_ref_frames : 16;
  const int max_frame_num = sps.frame_num_modulus();
  std::vector<Entry> dpb;
  for (auto& f : frames_) {
    f.refs = FrameRefs();
    if (!f.h264_slice.valid) continue;
    const H264SliceInfo& info = f.h264_slice;
    if (f.idr) dpb.clear();
    std::vector<Entry*> refs;
    refs.reserve(dpb.size());
    for (auto& entry : dpb) refs.push_back(&entry);
    std::sort(refs.begin(), refs.end(), [](const Entry* a, const Entry* b) {
      if (a->long_term != b->long_term) return !a->long_term;
      return a->frame_num < b->frame_num;
    });
    auto add_entry = [&](std::vector<int>& list, const Entry& entry) {
      addUnique(list, entry.poc);
    };
    for (auto it = refs.rbegin(); it != refs.rend(); ++it) {
      if (!(*it)->long_term) add_entry(f.refs.l0_pocs, **it);
    }
    for (auto* entry : refs) {
      if (entry->long_term) add_entry(f.refs.l0_pocs, *entry);
    }
    if (f.picture_type == 'B') {
      for (auto* entry : refs) {
        if (!entry->long_term) add_entry(f.refs.l1_pocs, *entry);
      }
      for (auto* entry : refs) {
        if (entry->long_term) add_entry(f.refs.l1_pocs, *entry);
      }
    }
    auto apply_mods = [&](const std::vector<H264RefMod>& mods,
                          std::vector<int>& list) {
      for (const H264RefMod& mod : mods) {
        if (mod.idc == 0 || mod.idc == 1) {
          int target = f.frame_num;
          if (mod.idc == 0)
            target -= mod.arg + 1;
          else
            target += mod.arg + 1;
          if (max_frame_num > 0)
            target = (target + max_frame_num) % max_frame_num;
          for (const Entry& entry : dpb) {
            if (!entry.long_term && entry.frame_num == target) {
              addUnique(list, entry.poc);
              break;
            }
          }
        } else if (mod.idc == 2) {
          for (const Entry& entry : dpb) {
            if (entry.long_term && entry.frame_num == mod.arg) {
              addUnique(list, entry.poc);
              break;
            }
          }
        }
      }
    };
    if (info.ref_pic_list_modification_flag_l0)
      apply_mods(info.ref_mod_l0, f.refs.l0_pocs);
    if (info.ref_pic_list_modification_flag_l1)
      apply_mods(info.ref_mod_l1, f.refs.l1_pocs);
    const int l0_active = info.num_ref_idx_l0_active_minus1 + 1;
    const int l1_active = info.num_ref_idx_l1_active_minus1 + 1;
    if (static_cast<int>(f.refs.l0_pocs.size()) > l0_active)
      f.refs.l0_pocs.resize(static_cast<size_t>(l0_active));
    if (static_cast<int>(f.refs.l1_pocs.size()) > l1_active)
      f.refs.l1_pocs.resize(static_cast<size_t>(l1_active));
    for (int ref : f.refs.l0_pocs) {
      bool found = false;
      for (const Entry& entry : dpb) found = found || entry.poc == ref;
      if (!found) addUnique(f.refs.missing_pocs, ref);
    }
    for (int ref : f.refs.l1_pocs) {
      bool found = false;
      for (const Entry& entry : dpb) found = found || entry.poc == ref;
      if (!found) addUnique(f.refs.missing_pocs, ref);
    }
    f.refs.dpb_count = static_cast<int>(dpb.size());
    f.refs.computed = true;

    if (f.ref) {
      Entry entry;
      entry.poc = f.poc;
      entry.frame_num = f.frame_num;
      entry.long_term = f.idr && info.long_term_reference_flag;
      dpb.push_back(entry);
      while (static_cast<int>(dpb.size()) > max_refs) {
        auto it = std::find_if(dpb.begin(), dpb.end(),
                               [](const Entry& e) { return !e.long_term; });
        if (it == dpb.end()) break;
        dpb.erase(it);
      }
      for (const H264Mmco& mmco : info.mmco) {
        if (mmco.op == 1 || mmco.op == 2) {
          int target = mmco.arg1;
          if (mmco.op == 1) {
            target = f.frame_num - (mmco.arg1 + 1);
            if (max_frame_num > 0)
              target = (target + max_frame_num) % max_frame_num;
          }
          dpb.erase(std::remove_if(
                        dpb.begin(), dpb.end(),
                        [&](const Entry& e) { return e.frame_num == target; }),
                    dpb.end());
        } else if (mmco.op == 3) {
          int target = f.frame_num - (mmco.arg1 + 1);
          if (max_frame_num > 0)
            target = (target + max_frame_num) % max_frame_num;
          for (Entry& e : dpb)
            if (e.frame_num == target) e.long_term = true;
        } else if (mmco.op == 5) {
          dpb.clear();
        } else if (mmco.op == 6) {
          if (!dpb.empty()) dpb.back().long_term = true;
        }
      }
    }
  }
}

void Stream::computeDisplayOrder() {
  int display = 0;
  size_t start = 0;
  while (start < frames_.size()) {
    size_t end = start + 1;
    while (end < frames_.size()) {
      if (frames_[end].idr || frames_[end].irap) break;
      end++;
    }
    std::vector<size_t> order;
    order.reserve(end - start);
    for (size_t i = start; i < end; i++) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
      return frames_[a].poc < frames_[b].poc;
    });
    for (size_t index : order) {
      frames_[index].gop_start = static_cast<int>(start);
      frames_[index].display_index = display++;
    }
    start = end;
  }
}

bool Stream::makeSeiHrdContext(int sps_version, bool hevc,
                               SeiHrdContext& ctx) const {
  ctx = SeiHrdContext();
  ctx.hevc = hevc;
  if (!hevc) {
    const H264Sps* sps = nullptr;
    if (sps_version >= 0 &&
        sps_version < static_cast<int>(h264_sps_versions_.size())) {
      sps = &h264_sps_versions_[static_cast<size_t>(sps_version)];
    } else if (h264_.have_sps) {
      sps = &h264_.sps;
    }
    if (!sps) return false;
    ctx.has_sps = true;
    ctx.nal_hrd = sps->nal_hrd.present;
    ctx.vcl_hrd = sps->vcl_hrd.present;
    ctx.nal_cpb_cnt_minus1 = static_cast<int>(sps->nal_hrd.cpb_cnt_minus1);
    ctx.vcl_cpb_cnt_minus1 = static_cast<int>(sps->vcl_hrd.cpb_cnt_minus1);
    const H264Hrd& hrd = sps->nal_hrd.present ? sps->nal_hrd : sps->vcl_hrd;
    if (hrd.present) {
      ctx.initial_cpb_removal_delay_length =
          static_cast<int>(hrd.initial_cpb_removal_delay_length_minus1) + 1;
      ctx.cpb_removal_delay_length =
          static_cast<int>(hrd.cpb_removal_delay_length_minus1) + 1;
      ctx.dpb_output_delay_length =
          static_cast<int>(hrd.dpb_output_delay_length_minus1) + 1;
      ctx.time_offset_length = static_cast<int>(hrd.time_offset_length);
    }
    ctx.pic_struct_present = sps->pic_struct_present;
    ctx.low_delay_hrd = sps->low_delay_hrd;
    return true;
  }

  const H265Sps* sps = nullptr;
  if (sps_version >= 0 &&
      sps_version < static_cast<int>(h265_sps_versions_.size())) {
    sps = &h265_sps_versions_[static_cast<size_t>(sps_version)];
  } else if (h265_.have_sps) {
    sps = &h265_.sps;
  }
  if (!sps) return false;
  ctx.has_sps = true;
  ctx.h265_hrd = &sps->hrd;
  ctx.highest_tid = sps->max_sub_layers_minus1;
  ctx.frame_field_info_present = sps->frame_field_info_present;
  return true;
}

void Stream::analyzeSeiHrd() {
  const bool hevc = codec_ == Codec::H265;
  const size_t header = hevc ? 2 : 1;
  for (auto& entry : nals_) {
    if (hevc) {
      if (entry.unit.nal_type != 39 && entry.unit.nal_type != 40) continue;
    } else if (entry.unit.nal_type != 6) {
      continue;
    }
    if (entry.unit.size <= header) continue;
    int sps_version = -1;
    if (entry.frame_index < frames_.size())
      sps_version = frames_[entry.frame_index].sps_version;
    SeiHrdContext ctx;
    if (!makeSeiHrdContext(sps_version, hevc, ctx)) continue;
    const uint8_t* p = data_.data() + entry.unit.payload_off;
    Rbsp rbsp = unescapeRbsp(p + header, entry.unit.size - header);
    parseSei(rbsp, nullptr, hevc, entry.sei, &ctx);
  }
}

void Stream::computeCpbModel() {
  cpb_ = CpbModelResult();
  CpbParams params;
  CpbInitial initial;
  double tick = 0.0;
  bool estimated = false;

  if (codec_ == Codec::H264) {
    if (!h264_.have_sps) {
      cpb_.error = "no active SPS";
      return;
    }
    const H264Sps& sps = h264_.sps;
    const H264Hrd* hrd = nullptr;
    if (sps.nal_hrd.present && !sps.nal_hrd.cpb.empty())
      hrd = &sps.nal_hrd;
    else if (sps.vcl_hrd.present && !sps.vcl_hrd.cpb.empty())
      hrd = &sps.vcl_hrd;
    if (hrd) {
      const HrdCpb& cpb0 = hrd->cpb[0];
      params.bit_rate = cpb0.bit_rate;
      params.cpb_size = cpb0.cpb_size;
      params.cbr = cpb0.cbr;
      params.source = sps.nal_hrd.present ? "nal_hrd" : "vcl_hrd";
      params.delay_bits =
          static_cast<int>(hrd->cpb_removal_delay_length_minus1) + 1;
    } else {
      estimated = true;
      params.source = "estimated";
    }
    if (sps.timing_info_present && sps.num_units_in_tick > 0 &&
        sps.time_scale > 0) {
      tick = static_cast<double>(sps.num_units_in_tick) /
             static_cast<double>(sps.time_scale);
    }
  } else if (codec_ == Codec::H265) {
    if (!h265_.have_sps) {
      cpb_.error = "no active SPS";
      return;
    }
    const H265Sps& sps = h265_.sps;
    const HrdCpb* cpb0 = nullptr;
    if (sps.vui_hrd_present && sps.hrd.present && !sps.hrd.sub_layers.empty()) {
      int tid = sps.max_sub_layers_minus1;
      if (tid < 0) tid = 0;
      if (tid >= static_cast<int>(sps.hrd.sub_layers.size()))
        tid = static_cast<int>(sps.hrd.sub_layers.size()) - 1;
      const H265HrdSubLayer& sl = sps.hrd.sub_layers[static_cast<size_t>(tid)];
      if (sps.hrd.nal_hrd_present && !sl.nal_cpb.empty())
        cpb0 = &sl.nal_cpb[0];
      else if (sps.hrd.vcl_hrd_present && !sl.vcl_cpb.empty())
        cpb0 = &sl.vcl_cpb[0];
    }
    if (cpb0) {
      params.bit_rate = cpb0->bit_rate;
      params.cpb_size = cpb0->cpb_size;
      params.cbr = cpb0->cbr;
      params.source = sps.hrd.nal_hrd_present ? "sps_nal_hrd" : "sps_vcl_hrd";
      params.delay_bits =
          static_cast<int>(sps.hrd.au_cpb_removal_delay_length_minus1) + 1;
    } else {
      estimated = true;
      params.source = "estimated";
    }
    if (sps.timing_info_present && sps.num_units_in_tick > 0 &&
        sps.time_scale > 0) {
      tick = static_cast<double>(sps.num_units_in_tick) /
             static_cast<double>(sps.time_scale);
    }
  } else {
    cpb_.error = "unknown codec";
    return;
  }

  const double fps = this->fps();
  if (estimated) {
    if (!(fps > 0.0) || frames_.empty()) {
      cpb_.error = "no frame rate for estimated CPB";
      cpb_.params = params;
      return;
    }
    uint64_t total_bits = 0;
    for (const auto& entry : nals_) {
      total_bits += (static_cast<uint64_t>(entry.unit.size) +
                     static_cast<uint64_t>(entry.unit.start_code_len)) *
                    8;
    }
    const double average = static_cast<double>(total_bits) * fps /
                           static_cast<double>(frames_.size());
    params.bit_rate = static_cast<uint64_t>(average);
    params.cpb_size =
        params.bit_rate > 0 ? params.bit_rate : 1;  // one-second estimate
    params.cbr = false;
    params.delay_bits = 0;
    params.source = "estimated";
    tick = 1.0 / fps;
  }
  if (!(tick > 0.0) && fps > 0.0) tick = 1.0 / fps;
  if (!(tick > 0.0)) {
    cpb_.error = "no HRD clock tick";
    cpb_.params = params;
    return;
  }

  std::vector<CpbAuInput> aus(frames_.size());
  const double fallback = fps > 0.0 ? 1.0 / fps : 0.0;
  for (size_t i = 0; i < frames_.size(); i++) {
    CpbAuInput& au = aus[i];
    au.fallback_interval_sec = fallback;
    uint64_t bits = 0;
    for (const auto& entry : nals_) {
      if (entry.frame_index != i) continue;
      bits += (static_cast<uint64_t>(entry.unit.size) +
               static_cast<uint64_t>(entry.unit.start_code_len)) *
              8;
    }
    au.bits = bits;
    for (const auto& entry : nals_) {
      if (entry.frame_index != i) continue;
      const SeiInfo& sei = entry.sei;
      if (sei.buffering_period.present) {
        const SeiBufferingPeriod& bp = sei.buffering_period;
        initial.present = true;
        au.segment_start = true;
        if (codec_ == Codec::H264) {
          const H264Sps& sps = h264_.sps;
          const bool use_nal =
              sps.nal_hrd.present && !bp.nal_initial_cpb_removal_delay.empty();
          const auto& delays = use_nal ? bp.nal_initial_cpb_removal_delay
                                       : bp.vcl_initial_cpb_removal_delay;
          const auto& offsets = use_nal ? bp.nal_initial_cpb_removal_offset
                                        : bp.vcl_initial_cpb_removal_offset;
          if (!delays.empty()) {
            initial.initial_delay = delays[0];
            if (!offsets.empty()) initial.initial_offset = offsets[0];
          }
        } else {
          const H265Sps& sps = h265_.sps;
          const bool use_nal = sps.hrd.nal_hrd_present &&
                               !bp.nal_initial_cpb_removal_delay.empty();
          const auto& delays = use_nal ? bp.nal_initial_cpb_removal_delay
                                       : bp.vcl_initial_cpb_removal_delay;
          const auto& offsets = use_nal ? bp.nal_initial_cpb_removal_offset
                                        : bp.vcl_initial_cpb_removal_offset;
          if (!delays.empty()) {
            initial.initial_delay = delays[0];
            if (!offsets.empty()) initial.initial_offset = offsets[0];
          }
          if (bp.irap_cpb_params_present || bp.concatenation_flag)
            initial.estimated = true;
        }
      }
      if (sei.pic_timing.present && sei.pic_timing.has_delays) {
        au.has_removal_delay = true;
        au.removal_delay = codec_ == Codec::H264
                               ? sei.pic_timing.cpb_removal_delay
                               : sei.pic_timing.au_cpb_removal_delay_minus1 + 1;
      }
    }
  }
  cpb_ = buildCpbModel(params, initial, tick, aus);
  cpb_.params = params;
}

std::string Stream::cpbCsv() const {
  std::string out =
      "index,type,poc,au_bits,delay_ticks,removal_time_ms,occupancy_before_"
      "bits,occupancy_bits,occupancy_ratio,underflow,overflow,estimated,"
      "segment_start,cpb_index,bit_rate_bps,cpb_size_bits,cbr\r\n";
  if (!cpb_.valid) return out;
  for (size_t i = 0; i < cpb_.samples.size(); i++) {
    const CpbSample& sample = cpb_.samples[i];
    out += std::to_string(sample.frame_index);
    out += ',';
    if (i < frames_.size()) out += frameTypeName(frames_[i]);
    out += ',';
    if (i < frames_.size()) out += std::to_string(frames_[i].poc);
    out += ',';
    out += std::to_string(static_cast<unsigned long long>(sample.au_bits));
    out += ',';
    if (sample.has_removal_delay) out += std::to_string(sample.removal_delay);
    out += ',';
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f", sample.removal_time_sec * 1000.0);
    out += buf;
    out += ',';
    std::snprintf(buf, sizeof(buf), "%.3f", sample.occupancy_before_bits);
    out += buf;
    out += ',';
    std::snprintf(buf, sizeof(buf), "%.3f", sample.occupancy_after_bits);
    out += buf;
    out += ',';
    std::snprintf(buf, sizeof(buf), "%.6f", sample.occupancy_ratio);
    out += buf;
    out += ',';
    out += sample.underflow ? "1" : "0";
    out += ',';
    out += sample.overflow ? "1" : "0";
    out += ',';
    out += sample.estimated ? "1" : "0";
    out += ',';
    out += sample.segment_start ? "1" : "0";
    out += ',';
    out += std::to_string(cpb_.params.cpb_index);
    out += ',';
    out +=
        std::to_string(static_cast<unsigned long long>(cpb_.params.bit_rate));
    out += ',';
    out +=
        std::to_string(static_cast<unsigned long long>(cpb_.params.cpb_size));
    out += ',';
    out += cpb_.params.cbr ? "1" : "0";
    out += "\r\n";
  }
  return out;
}

std::vector<SyntaxNode> Stream::syntaxTree(size_t nal_index) const {
  std::vector<SyntaxNode> roots;
  if (nal_index >= nals_.size()) return roots;
  const NalEntry& e = nals_[nal_index];
  const NalUnit& n = e.unit;
  const uint8_t* p = data_.data() + n.payload_off;
  bool hevc = codec_ == Codec::H265;

  roots.push_back(nalHeaderNode(n, codec_));
  size_t header_bytes = hevc ? 2 : 1;
  if (n.size <= header_bytes) return roots;
  Rbsp rbsp = unescapeRbsp(p + header_bytes, n.size - header_bytes);
  BitReader br(rbsp.data.data(), rbsp.data.size());
  SyntaxBuilder sb(roots);

  if (!hevc) {
    int type = n.nal_type;
    if (type == 7) {
      H264Sps sps;
      parseH264Sps(br, &sb, sps);
    } else if (type == 8) {
      H264Pps pps;
      const H264Sps* sps = nullptr;
      if (e.sps_version >= 0 &&
          e.sps_version < static_cast<int>(h264_sps_versions_.size()))
        sps = &h264_sps_versions_[static_cast<size_t>(e.sps_version)];
      else if (h264_.have_sps)
        sps = &h264_.sps;
      parseH264Pps(br, &sb, pps, sps);
    } else if (type == 6) {
      SeiInfo info;
      SeiHrdContext hrd_ctx;
      int sps_version = -1;
      if (e.frame_index < frames_.size())
        sps_version = frames_[e.frame_index].sps_version;
      const SeiHrdContext* ctx =
          makeSeiHrdContext(sps_version, false, hrd_ctx) ? &hrd_ctx : nullptr;
      parseSei(rbsp, &sb, false, info, ctx);
    } else if (type == 9) {
      Section sec(&sb, "access_unit_delimiter_rbsp");
      uint64_t aud_pos = br.pos();
      uint32_t v = br.u(3);
      sb.addUInt("primary_pic_type", v, aud_pos, 3);
    } else if ((type >= 1 && type <= 5) || type == 20) {
      H264State state = h264_;
      if (e.sps_version >= 0 &&
          e.sps_version < static_cast<int>(h264_sps_versions_.size())) {
        state.sps = h264_sps_versions_[static_cast<size_t>(e.sps_version)];
        state.have_sps = true;
      }
      if (e.pps_version >= 0 &&
          e.pps_version < static_cast<int>(h264_pps_versions_.size())) {
        state.pps = h264_pps_versions_[static_cast<size_t>(e.pps_version)];
        state.have_pps = true;
      }
      H264SliceInfo info;
      info.nal_ref_idc = n.nal_ref_idc;
      info.nal_type = n.nal_type;
      parseH264SliceHeader(br, &sb, state, info, false);
      if (state.have_sps)
        sb.add("  active SPS",
               std::to_string(state.sps.width()) + "x" +
                   std::to_string(state.sps.height()),
               br.pos(), 0);
    }
  } else {
    int type = n.nal_type;
    if (type == 32) {
      parseH265Vps(br, &sb);
    } else if (type == 33) {
      H265Sps sps;
      parseH265Sps(br, &sb, sps);
    } else if (type == 34) {
      H265Pps pps;
      parseH265Pps(br, &sb, pps);
    } else if (type == 35) {
      Section sec(&sb, "access_unit_delimiter_rbsp");
      uint64_t aud_pos = br.pos();
      uint32_t v = br.u(3);
      sb.addUInt("pic_type", v, aud_pos, 3);
    } else if (type == 39 || type == 40) {
      SeiInfo info;
      SeiHrdContext hrd_ctx;
      int sps_version = -1;
      if (e.frame_index < frames_.size())
        sps_version = frames_[e.frame_index].sps_version;
      const SeiHrdContext* ctx =
          makeSeiHrdContext(sps_version, true, hrd_ctx) ? &hrd_ctx : nullptr;
      parseSei(rbsp, &sb, true, info, ctx);
    } else if (type <= 31) {
      H265State state = h265_;
      if (e.sps_version >= 0 &&
          e.sps_version < static_cast<int>(h265_sps_versions_.size())) {
        state.sps = h265_sps_versions_[static_cast<size_t>(e.sps_version)];
        state.have_sps = true;
      }
      if (e.pps_version >= 0 &&
          e.pps_version < static_cast<int>(h265_pps_versions_.size())) {
        state.pps = h265_pps_versions_[static_cast<size_t>(e.pps_version)];
        state.have_pps = true;
      }
      state.nal_type = type;
      state.temporal_id_plus1 = n.size >= 2 ? (p[1] & 0x07) : 1;
      H265SliceInfo info;
      parseH265SliceSegmentHeader(br, &sb, state, info);
      if (state.have_sps)
        sb.add("  active SPS",
               std::to_string(state.sps.display_width()) + "x" +
                   std::to_string(state.sps.display_height()),
               br.pos(), 0);
    }
  }
  return roots;
}

double Stream::fps() const {
  if (codec_ == Codec::H264 && h264_.have_sps) return h264_.sps.fps();
  if (codec_ == Codec::H265 && h265_.have_sps) return h265_.sps.fps();
  return 0.0;
}

std::string Stream::frameTypeName(const FrameInfo& frame) const {
  if (frame.idr) return "IDR";
  if (codec_ == Codec::H265 && frame.irap)
    return frame.nal_type == 21 ? "CRA" : "BLA";
  return std::string(1, frame.picture_type);
}

double Stream::frameTimeMs(const FrameInfo& frame) const {
  if (frame.has_timing && frame.time_sec >= 0.0) return frame.time_sec * 1000.0;
  const double stream_fps = fps();
  if (stream_fps > 0.0) return frame.index * 1000.0 / stream_fps;
  return -1.0;
}

void Stream::applyFrameTiming(const std::vector<FrameTiming>& timing) {
  size_t count = std::min(frames_.size(), timing.size());
  for (size_t i = 0; i < count; i++) {
    FrameInfo& f = frames_[i];
    f.pts = timing[i].pts;
    f.dts = timing[i].dts;
    f.has_timing = timing[i].has_timing;
    f.time_sec = timing[i].time_sec;
    f.packet_index = timing[i].packet_index;
  }
}

std::string Stream::framesCsv() const {
  std::string out =
      "index,type,idr,poc,qp,frame_num,size_bytes,file_offset,slices,nals,"
      "time_ms\r\n";
  const double fps = this->fps();
  for (const auto& f : frames_) {
    out += std::to_string(f.index);
    out += ',';
    out += frameTypeName(f);
    out += ',';
    out += f.idr ? "1" : "0";
    out += ',';
    out += std::to_string(f.poc);
    out += ',';
    out += std::to_string(f.qp);
    out += ',';
    if (f.frame_num >= 0) out += std::to_string(f.frame_num);
    out += ',';
    out += std::to_string(static_cast<unsigned long long>(f.byte_size));
    out += ',';
    out += std::to_string(static_cast<unsigned long long>(f.byte_offset));
    out += ',';
    out += std::to_string(f.slice_count);
    out += ',';
    out += std::to_string(f.nal_count);
    out += ',';
    if (fps > 0.0) {
      double ms = frameTimeMs(f);
      if (ms >= 0.0 && ms < 9.0e15)
        out += std::to_string(static_cast<long long>(ms + 0.5));
    }
    out += "\r\n";
  }
  return out;
}

std::string Stream::nalsCsv() const {
  std::string out =
      "index,offset,payload_offset,size_bytes,type,type_name,frame_index,"
      "summary\r\n";
  for (size_t i = 0; i < nals_.size(); i++) {
    const NalEntry& e = nals_[i];
    out += std::to_string(i);
    out += ',';
    out += std::to_string(static_cast<unsigned long long>(e.unit.offset));
    out += ',';
    out += std::to_string(static_cast<unsigned long long>(e.unit.payload_off));
    out += ',';
    out += std::to_string(static_cast<unsigned long long>(e.unit.size));
    out += ',';
    out += std::to_string(static_cast<unsigned>(e.unit.nal_type));
    out += ',';
    out += csvQuote(e.unit.type_name);
    out += ',';
    if (e.frame_index != static_cast<size_t>(-1))
      out += std::to_string(e.frame_index);
    out += ',';
    out += csvQuote(e.summary);
    out += "\r\n";
  }
  return out;
}

const uint8_t* Stream::nalBytes(size_t nal_index, size_t& size) const {
  if (nal_index >= nals_.size()) {
    size = 0;
    return nullptr;
  }
  const NalUnit& n = nals_[nal_index].unit;
  size = n.size;
  return data_.data() + n.payload_off;
}

bool Stream::mapBitRange(size_t nal_index, uint64_t bit_pos, uint64_t bit_len,
                         size_t& payload_start, size_t& payload_end) const {
  if (nal_index >= nals_.size() || bit_len == 0) return false;
  const NalUnit& n = nals_[nal_index].unit;
  size_t header = (codec_ == Codec::H265) ? 2 : 1;
  if (n.size <= header) return false;
  Rbsp rbsp =
      unescapeRbsp(data_.data() + n.payload_off + header, n.size - header);
  uint64_t first = bit_pos / 8;
  uint64_t last = (bit_pos + bit_len - 1) / 8;
  if (first >= rbsp.map.size()) return false;
  if (last >= rbsp.map.size()) last = rbsp.map.size() - 1;
  payload_start = header + rbsp.map[first];
  payload_end = header + rbsp.map[last] + 1;
  return true;
}

ColourDescription Stream::colourDescription() const {
  ColourDescription colour;
  if (codec_ == Codec::H264 && h264_.have_sps) {
    colour.present = h264_.sps.colour_description_present;
    colour.primaries = h264_.sps.colour_primaries;
    colour.transfer = h264_.sps.transfer_characteristics;
    colour.matrix = h264_.sps.matrix_coefficients;
  } else if (codec_ == Codec::H265 && h265_.have_sps) {
    colour.present = h265_.sps.colour_description_present;
    colour.primaries = h265_.sps.colour_primaries;
    colour.transfer = h265_.sps.transfer_characteristics;
    colour.matrix = h265_.sps.matrix_coefficients;
  }
  return colour;
}

bool Stream::videoFullRange() const {
  if (codec_ == Codec::H264 && h264_.have_sps)
    return h264_.sps.video_full_range;
  if (codec_ == Codec::H265 && h265_.have_sps)
    return h265_.sps.video_full_range;
  return false;
}

std::string Stream::hdrSummary() const {
  return formatHdrSummary(hdr_, colourDescription());
}

std::string Stream::streamSummary() const {
  if (codec_ == Codec::Unknown) return "no stream";
  std::string s = codecName(codec_);
  if (codec_ == Codec::H264 && h264_.have_sps) {
    const H264Sps& sps = h264_.sps;
    s += fmt(" | %dx%d | %s@%s", sps.width(), sps.height(),
             h264ProfileName(sps.profile_idc).c_str(),
             h264LevelName(sps.level_idc).c_str());
    double fps = sps.fps();
    if (fps > 0) s += fmt(" | %.3f fps", fps);
  } else if (codec_ == Codec::H265 && h265_.have_sps) {
    const H265Sps& sps = h265_.sps;
    s += fmt(" | %dx%d | %s@%s", sps.display_width(), sps.display_height(),
             h265ProfileName(sps.ptl.profile_idc).c_str(),
             h265LevelName(sps.ptl.level_idc).c_str());
    double fps = sps.fps();
    if (fps > 0) s += fmt(" | %.3f fps", fps);
  }
  std::string hdr = hdrSummary();
  if (!hdr.empty()) s += " | " + hdr;
  s += fmt(" | %zu NAL, %zu frames", nals_.size(), frames_.size());
  return s;
}

}  // namespace bs
