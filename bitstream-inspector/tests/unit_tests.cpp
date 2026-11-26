// Unit tests for the portable parser core. No external dependencies: run
// `bs_tests` directly or through ctest.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "core/bitreader.h"
#include "core/h264.h"
#include "core/h265.h"
#include "core/nal.h"
#include "core/sei.h"
#include "core/stream.h"

namespace {

using bs::BitReader;
using bs::Rbsp;
using bs::unescapeRbsp;

int g_checks = 0;
int g_failures = 0;

std::string describe(const std::string& v) { return "\"" + v + "\""; }
std::string describe(const char* v) { return describe(std::string(v)); }
template <typename T>
std::string describe(T v) {
  std::ostringstream os;
  os << v;
  return os.str();
}

void reportFailure(const std::string& what, const std::string& expected,
                   const std::string& actual, const char* file, int line) {
  std::fprintf(stderr, "%s:%d: FAIL: %s (expected %s, got %s)\n", file, line,
               what.c_str(), expected.c_str(), actual.c_str());
  g_failures++;
}

#define CHECK(cond)                                                         \
  do {                                                                      \
    g_checks++;                                                             \
    if (!(cond)) {                                                          \
      std::fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond); \
      g_failures++;                                                         \
    }                                                                       \
  } while (0)

#define CHECK_EQ(actual, expected)                                   \
  do {                                                               \
    g_checks++;                                                      \
    auto va_ = (actual);                                             \
    auto ve_ = (expected);                                           \
    if (!(va_ == ve_))                                               \
      reportFailure(#actual, describe(ve_), describe(va_), __FILE__, \
                    __LINE__);                                       \
  } while (0)

#define CHECK_NEAR(actual, expected, eps)                            \
  do {                                                               \
    g_checks++;                                                      \
    double va_ = static_cast<double>(actual);                        \
    double ve_ = static_cast<double>(expected);                      \
    if (std::fabs(va_ - ve_) > (eps))                                \
      reportFailure(#actual, describe(ve_), describe(va_), __FILE__, \
                    __LINE__);                                       \
  } while (0)

std::vector<uint8_t> hexBytes(const char* text) {
  std::vector<uint8_t> out;
  int hi = -1;
  for (const char* p = text; *p; p++) {
    int v;
    if (*p >= '0' && *p <= '9')
      v = *p - '0';
    else if (*p >= 'a' && *p <= 'f')
      v = *p - 'a' + 10;
    else if (*p >= 'A' && *p <= 'F')
      v = *p - 'A' + 10;
    else
      continue;  // whitespace, ':' and '_' are separators
    if (hi < 0)
      hi = v;
    else {
      out.push_back(static_cast<uint8_t>((hi << 4) | v));
      hi = -1;
    }
  }
  return out;
}

std::vector<uint8_t> withStartCode(
    const std::vector<std::vector<uint8_t>>& nals) {
  std::vector<uint8_t> out;
  for (const auto& nal : nals) {
    out.insert(out.end(), {0, 0, 0, 1});
    out.insert(out.end(), nal.begin(), nal.end());
  }
  return out;
}

struct BitWriter {
  std::vector<uint8_t> data;
  int bits = 0;

  void bit(uint32_t v) {
    if ((bits & 7) == 0) data.push_back(0);
    if (v) data.back() |= static_cast<uint8_t>(0x80u >> (bits & 7));
    bits++;
  }
  void u(uint32_t v, int n) {
    for (int i = n - 1; i >= 0; i--) bit((v >> i) & 1u);
  }
  void ue(uint32_t v) {
    uint32_t x = v + 1;
    int len = 0;
    while ((x >> len) != 0) len++;
    for (int i = 1; i < len; i++) bit(0);
    u(x, len);
  }
  void se(int32_t v) {
    ue(v <= 0 ? static_cast<uint32_t>(-2 * v)
              : static_cast<uint32_t>(2 * v - 1));
  }
};

void testBitReader() {
  const uint8_t bits[] = {0xB5, 0x2C, 0x80};
  BitReader br(bits, sizeof(bits));
  CHECK_EQ(br.u(4), 0xBu);
  CHECK_EQ(br.u(4), 0x5u);
  CHECK_EQ(br.u(4), 0x2u);
  CHECK_EQ(br.u(4), 0xCu);
  CHECK_EQ(br.u(4), 0x8u);

  for (uint32_t v :
       {0u, 1u, 2u, 3u, 4u, 7u, 8u, 31u, 32u, 255u, 256u, 65535u}) {
    BitWriter w;
    w.ue(v);
    BitReader r(w.data.data(), w.data.size());
    CHECK_EQ(r.ue(), v);
    CHECK(!r.fail());
  }
  for (int32_t v : {-1, 0, 1, -2, 2, -3, 3, 100, -100}) {
    BitWriter w;
    w.se(v);
    BitReader r(w.data.data(), w.data.size());
    CHECK_EQ(r.se(), v);
    CHECK(!r.fail());
  }

  const uint8_t one[] = {0xFF};
  BitReader trunc(one, sizeof(one));
  CHECK_EQ(trunc.u(8), 0xFFu);
  CHECK_EQ(trunc.u(1), 0u);
  CHECK(trunc.fail());
  CHECK_EQ(trunc.ue(), 0u);

  const uint8_t zeros[] = {0, 0, 0, 0};
  BitReader runaway(zeros, sizeof(zeros));
  CHECK_EQ(runaway.ue(), 0u);
  CHECK(runaway.fail());

  // byte_align consumes the rbsp_stop_one_bit and the zero padding.
  const uint8_t align[] = {0x10, 0x80};  // 0001 0000 1000 0000
  BitReader aligned(align, sizeof(align));
  aligned.skip(3);
  CHECK(aligned.byte_align());
  CHECK_EQ(aligned.pos(), 8u);

  const uint8_t bad_align[] = {0x18};  // 0001 1000: padding is not zero
  BitReader bad(bad_align, sizeof(bad_align));
  bad.skip(3);
  CHECK(!bad.byte_align());
  CHECK(bad.fail());
}

void testRbspUnescape() {
  Rbsp r = unescapeRbsp(hexBytes("00 00 03 01").data(), 4);
  CHECK_EQ(r.data.size(), 3u);
  CHECK_EQ(r.data[2], 0x01);
  CHECK_EQ(r.map.size(), 3u);
  CHECK_EQ(r.map[0], 0u);
  CHECK_EQ(r.map[1], 1u);
  CHECK_EQ(r.map[2], 3u);

  r = unescapeRbsp(hexBytes("00 00 03 03").data(), 4);
  CHECK_EQ(r.data.size(), 3u);
  CHECK_EQ(r.data[2], 0x03);

  r = unescapeRbsp(hexBytes("00 00 00 03 01").data(), 5);
  CHECK_EQ(r.data.size(), 4u);
  CHECK_EQ(r.data[2], 0x00);
  CHECK_EQ(r.data[3], 0x01);

  const auto plain = hexBytes("67 42 c0 0a");
  r = unescapeRbsp(plain.data(), plain.size());
  CHECK(r.data == plain);
}

void testNalSplitter() {
  // 4-byte start code, 4-byte start code and 3-byte start code in one buffer.
  const auto buf = hexBytes(
      "00 00 00 01 67 42 "
      "00 00 00 01 68 ce "
      "00 00 01 65 88 99");
  std::vector<bs::NalUnit> nals;
  bs::NalSplitter::split(buf.data(), buf.size(), false, nals);
  CHECK_EQ(nals.size(), 3u);
  if (nals.size() == 3) {
    CHECK_EQ(nals[0].offset, 0u);
    CHECK_EQ(nals[0].start_code_len, 4);
    CHECK_EQ(nals[0].payload_off, 4u);
    CHECK_EQ(nals[0].size, 2u);
    CHECK_EQ(nals[0].nal_type, 7);
    CHECK_EQ(nals[1].offset, 6u);
    CHECK_EQ(nals[1].start_code_len, 4);
    CHECK_EQ(nals[1].nal_type, 8);
    CHECK_EQ(nals[2].offset, 12u);
    CHECK_EQ(nals[2].start_code_len, 3);
    CHECK_EQ(nals[2].nal_type, 5);
    CHECK(std::strcmp(nals[2].type_name, "Slice (IDR)") == 0);
  }

  // Trailing zero bytes belong to the next start code and are trimmed.
  const auto trailing = hexBytes("00 00 00 01 67 42 00 00 00 00 01 68 ce");
  bs::NalSplitter::split(trailing.data(), trailing.size(), false, nals);
  CHECK_EQ(nals.size(), 2u);
  if (nals.size() == 2) {
    CHECK_EQ(nals[0].size, 2u);
    CHECK_EQ(nals[1].offset, 7u);
    CHECK_EQ(nals[1].start_code_len, 4);
  }

  // HEVC NAL header: nal_unit_type is bits 1..6 of the first byte.
  const auto hevc =
      hexBytes("00 00 00 01 40 01 00 00 00 01 42 01 00 00 00 01 44 01");
  bs::NalSplitter::split(hevc.data(), hevc.size(), true, nals);
  CHECK_EQ(nals.size(), 3u);
  if (nals.size() == 3) {
    CHECK_EQ(nals[0].nal_type, 32);
    CHECK_EQ(nals[1].nal_type, 33);
    CHECK_EQ(nals[2].nal_type, 34);
  }

  const auto none = hexBytes("de ad be ef");
  bs::NalSplitter::split(none.data(), none.size(), false, nals);
  CHECK(nals.empty());
}

// First bytes of a real 64x48 Baseline encoder output (test vector generated
// with libx264; see tests/README.md for how the fixture was produced).
const char* kH264Sps =
    "67 42 c0 0a da 11 ec 04 40 00 00 03 00 40 00 00 05 03 c4 89 a8";
const char* kH264Pps = "68 ce 0f c8";
const char* kH264Idr =
    "65 88 84 3a 0c 60 01 6c 42 11 e5 05 1b 8e 26 32 b4 07 fb c0 0a d3 60 "
    "cc 63 6d 42 02 04 32 20 20 46 00 0e 46 5e 14 d5 10 66 12 80 94 5f fe "
    "1c 04 04 32 38 00 13 00 02 19 d1 82 4c 14 c5 3c cd 25";

const char* kH265Vps =
    "40 01 0c 01 ff ff 01 60 00 00 03 00 90 00 00 03 00 00 03 00 1e ba 02 40";
const char* kH265Sps =
    "42 01 01 01 60 00 00 03 00 90 00 00 03 00 00 03 00 1e a0 20 83 16 5b a4 "
    "a4 c2 f0 16 80 80 00 00 03 00 80 00 00 05 04";
const char* kH265Pps = "44 01 c0 73 c0 89";
const char* kH265Idr =
    "28 01 ac 46 97 d8 ad f9 45 2c a3 9b ed 1d be 81 2f 4b fc c9 08 a0 6e 08 "
    "a1 75 ac 52 f8 0f 5b f0 71 20 53 f3 72 46 07 7d 56 12 66 38 fa fa ab ca "
    "98 b7 f6 e1 1b 4c 24 e8 af 7a 9a eb c5 b1 83 32 1c 60 30 92 bc 63 1e c2 "
    "ff 80 43 63 4c d7 e3 6c f7 25 a3 3d df 3b 58 a6 d6 8f f9 88 e9 bf b4";

bs::Rbsp payloadAfterHeader(const std::vector<uint8_t>& nal) {
  return bs::unescapeRbsp(nal.data() + 1, nal.size() - 1);
}

void testH264ParameterSets() {
  const auto sps_bytes = hexBytes(kH264Sps);
  bs::Rbsp sps_rbsp = payloadAfterHeader(sps_bytes);
  bs::BitReader br(sps_rbsp.data.data(), sps_rbsp.data.size());
  bs::H264Sps sps;
  bs::parseH264Sps(br, nullptr, sps);
  CHECK(!br.fail());
  CHECK(sps.valid);
  CHECK_EQ(sps.profile_idc, 66);
  CHECK_EQ(sps.level_idc, 10);
  CHECK_EQ(sps.sps_id, 0);
  CHECK_EQ(sps.log2_max_frame_num_minus4, 0);
  CHECK_EQ(sps.pic_order_cnt_type, 2);
  CHECK_EQ(sps.max_num_ref_frames, 1);
  CHECK_EQ(sps.pic_width_in_mbs_minus1, 3);
  CHECK_EQ(sps.pic_height_in_map_units_minus1, 2);
  CHECK(sps.frame_mbs_only);
  CHECK_EQ(sps.width(), 64);
  CHECK_EQ(sps.height(), 48);
  CHECK_NEAR(sps.fps(), 10.0, 1e-9);

  const auto pps_bytes = hexBytes(kH264Pps);
  bs::Rbsp pps_rbsp = payloadAfterHeader(pps_bytes);
  bs::BitReader pps_br(pps_rbsp.data.data(), pps_rbsp.data.size());
  bs::H264Pps pps;
  bs::parseH264Pps(pps_br, nullptr, pps, &sps);
  CHECK(!pps_br.fail());
  CHECK(pps.valid);
  CHECK_EQ(pps.pps_id, 0);
  CHECK_EQ(pps.sps_id, 0);
  CHECK(!pps.entropy_coding_mode);
  CHECK_EQ(pps.pic_init_qp_minus26, -3);
  CHECK(pps.deblocking_filter_control_present);

  bs::H264State state;
  state.sps = sps;
  state.pps = pps;
  state.have_sps = true;
  state.have_pps = true;

  bs::Rbsp slice_rbsp = payloadAfterHeader(hexBytes(kH264Idr));
  bs::BitReader slice_br(slice_rbsp.data.data(), slice_rbsp.data.size());
  bs::H264SliceInfo info;
  info.nal_type = 5;
  info.nal_ref_idc =
      3;  // dec_ref_pic_marking() is present for reference slices
  bs::parseH264SliceHeader(slice_br, nullptr, state, info);
  CHECK(!slice_br.fail());
  CHECK(info.valid);
  CHECK_EQ(static_cast<int>(info.slice_type),
           static_cast<int>(bs::SliceType::I));
  CHECK(info.idr);
  CHECK_EQ(info.first_mb_in_slice, 0);
  CHECK_EQ(info.pps_id, 0);
  CHECK_EQ(info.frame_num, 0);
  CHECK_EQ(info.poc, 0);
  CHECK_EQ(info.slice_qp_delta, -3);
  CHECK(info.header_bits > 0);
}

void testH265ParameterSets() {
  const auto vps_bytes = hexBytes(kH265Vps);
  bs::Rbsp vps_rbsp =
      bs::unescapeRbsp(vps_bytes.data() + 2, vps_bytes.size() - 2);
  bs::BitReader vps_br(vps_rbsp.data.data(), vps_rbsp.data.size());
  bs::parseH265Vps(vps_br, nullptr);
  CHECK(!vps_br.fail());

  const auto sps_bytes = hexBytes(kH265Sps);
  bs::Rbsp sps_rbsp =
      bs::unescapeRbsp(sps_bytes.data() + 2, sps_bytes.size() - 2);
  bs::BitReader sps_br(sps_rbsp.data.data(), sps_rbsp.data.size());
  bs::H265Sps sps;
  bs::parseH265Sps(sps_br, nullptr, sps);
  CHECK(!sps_br.fail());
  CHECK(sps.parsed_fully);
  CHECK_EQ(sps.vps_id, 0);
  CHECK_EQ(sps.sps_id, 0);
  CHECK_EQ(sps.max_sub_layers_minus1, 0);
  CHECK_EQ(sps.ptl.profile_idc, 1);
  CHECK_EQ(sps.chroma_format_idc, 1);
  CHECK_EQ(sps.width, 64);
  CHECK_EQ(sps.height, 48);
  CHECK(!sps.conformance_window);
  CHECK_EQ(sps.bit_depth_luma_minus8, 0);
  CHECK_EQ(sps.ctb_log2(), 5);
  CHECK(!sps.sao_enabled);
  CHECK(!sps.amp_enabled);
  CHECK(!sps.pcm_enabled);
  CHECK(sps.temporal_mvp_enabled);
  CHECK(sps.strong_intra_smoothing);
  CHECK(sps.vui_present);

  const auto pps_bytes = hexBytes(kH265Pps);
  bs::Rbsp pps_rbsp =
      bs::unescapeRbsp(pps_bytes.data() + 2, pps_bytes.size() - 2);
  bs::BitReader pps_br(pps_rbsp.data.data(), pps_rbsp.data.size());
  bs::H265Pps pps;
  bs::parseH265Pps(pps_br, nullptr, pps);
  CHECK(!pps_br.fail());
  CHECK(pps.parsed_fully);
  CHECK_EQ(pps.pps_id, 0);
  CHECK_EQ(pps.sps_id, 0);
  CHECK(!pps.tiles_enabled);
  CHECK(!pps.entropy_coding_sync_enabled);
  CHECK(pps.cu_qp_delta_enabled);
  CHECK(pps.loop_filter_across_slices_enabled);
  CHECK(!pps.deblocking_filter_control_present);

  bs::H265State state;
  state.sps = sps;
  state.pps = pps;
  state.have_sps = true;
  state.have_pps = true;
  state.nal_type = 20;  // IDR_N_LP
  state.temporal_id_plus1 = 1;

  const auto slice_bytes = hexBytes(kH265Idr);
  bs::Rbsp slice_rbsp =
      bs::unescapeRbsp(slice_bytes.data() + 2, slice_bytes.size() - 2);
  bs::BitReader slice_br(slice_rbsp.data.data(), slice_rbsp.data.size());
  bs::H265SliceInfo info;
  bs::parseH265SliceSegmentHeader(slice_br, nullptr, state, info);
  CHECK(!slice_br.fail());
  CHECK(info.valid);
  CHECK(info.first_slice_segment_in_pic);
  CHECK_EQ(info.pps_id, 0);
  CHECK_EQ(static_cast<int>(info.slice_type),
           static_cast<int>(bs::SliceType::I));
  CHECK(info.idr);
  CHECK(info.irap);
  CHECK_EQ(info.slice_qp_delta, 4);
  CHECK(info.header_bits > 0);
}

void appendU16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value >> 8));
  out.push_back(static_cast<uint8_t>(value));
}

void appendU32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value >> 24));
  out.push_back(static_cast<uint8_t>(value >> 16));
  out.push_back(static_cast<uint8_t>(value >> 8));
  out.push_back(static_cast<uint8_t>(value));
}

const bs::SyntaxNode* findNode(const std::vector<bs::SyntaxNode>& nodes,
                               const std::string& name) {
  for (const auto& node : nodes) {
    if (node.name == name) return &node;
    const bs::SyntaxNode* found = findNode(node.children, name);
    if (found) return found;
  }
  return nullptr;
}

std::vector<uint8_t> seiMessage(uint32_t type,
                                const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> out;
  uint32_t t = type;
  while (t >= 255) {
    out.push_back(255);
    t -= 255;
  }
  out.push_back(static_cast<uint8_t>(t));
  uint32_t size = static_cast<uint32_t>(payload.size());
  while (size >= 255) {
    out.push_back(255);
    size -= 255;
  }
  out.push_back(static_cast<uint8_t>(size));
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

void testSeiHdr() {
  CHECK_EQ(std::string(bs::seiPayloadName(137, false)),
           std::string("mastering_display_colour_volume"));
  CHECK_EQ(std::string(bs::seiPayloadName(144, false)),
           std::string("content_light_level_info"));
  CHECK_EQ(std::string(bs::seiPayloadName(147, false)),
           std::string("alternative_transfer_characteristics"));
  CHECK_EQ(std::string(bs::seiPayloadName(51, false)),
           std::string("three_dimensional_reference_displays_info"));
  CHECK_EQ(std::string(bs::seiPayloadName(52, false)),
           std::string("depth_timing"));
  CHECK_EQ(std::string(bs::seiPayloadName(142, false)),
           std::string("colour_remapping_info"));
  CHECK_EQ(std::string(bs::seiPayloadName(137, true)),
           std::string("mastering_display_colour_volume"));

  std::vector<uint8_t> mdcv;
  appendU16(mdcv, 13250);
  appendU16(mdcv, 34500);
  appendU16(mdcv, 7500);
  appendU16(mdcv, 3000);
  appendU16(mdcv, 34000);
  appendU16(mdcv, 16000);
  appendU16(mdcv, 15635);
  appendU16(mdcv, 16450);
  appendU32(mdcv, 10000000);
  appendU32(mdcv, 1);

  std::vector<uint8_t> cll;
  appendU16(cll, 1000);
  appendU16(cll, 400);

  std::vector<uint8_t> atc = {16};
  std::vector<uint8_t> t35 = {0xB5, 0x00, 0x3C, 0x00, 0x01, 0x04, 0x00};

  std::vector<uint8_t> bytes;
  for (const auto& message : {seiMessage(137, mdcv), seiMessage(144, cll),
                              seiMessage(147, atc), seiMessage(4, t35)}) {
    bytes.insert(bytes.end(), message.begin(), message.end());
  }
  bytes.push_back(0x80);

  bs::Rbsp rbsp;
  rbsp.data = bytes;
  bs::SeiInfo info;
  bs::parseSei(rbsp, nullptr, false, info);
  CHECK_EQ(info.messages.size(), 4u);
  CHECK(info.hdr.mastering_display.present);
  CHECK_EQ(info.hdr.mastering_display.primaries_x[0], 13250);
  CHECK_EQ(info.hdr.mastering_display.primaries_y[0], 34500);
  CHECK_EQ(info.hdr.mastering_display.max_luminance, 10000000u);
  CHECK_EQ(info.hdr.mastering_display.min_luminance, 1u);
  CHECK(info.hdr.content_light_level.present);
  CHECK_EQ(info.hdr.content_light_level.max_cll, 1000);
  CHECK_EQ(info.hdr.content_light_level.max_fall, 400);
  CHECK(info.hdr.alternative_transfer.present);
  CHECK_EQ(info.hdr.alternative_transfer.preferred_transfer_characteristics,
           16);
  CHECK(info.hdr.hdr10_plus);

  bs::ColourDescription colour;
  colour.present = true;
  colour.primaries = 9;
  colour.transfer = 16;
  colour.matrix = 9;
  std::string summary = bs::formatHdrSummary(info.hdr, colour);
  CHECK(summary.find("HDR10+") == 0);
  CHECK(summary.find("BT.2020/PQ") != std::string::npos);
  CHECK(summary.find("MaxCLL 1000/400") != std::string::npos);

  bs::SeiInfo hlg;
  hlg.hdr.alternative_transfer.present = true;
  hlg.hdr.alternative_transfer.preferred_transfer_characteristics = 18;
  CHECK(bs::formatHdrSummary(hlg.hdr, colour).find("HLG") == 0);

  bs::SeiInfo hevc;
  bs::parseSei(rbsp, nullptr, true, hevc);
  CHECK_EQ(hevc.messages.size(), 4u);
  CHECK(hevc.hdr.mastering_display.present);
  CHECK_EQ(hevc.hdr.mastering_display.primaries_x[2], 34000);

  std::vector<bs::SyntaxNode> roots;
  bs::SyntaxBuilder builder(roots);
  bs::SeiInfo tree_info;
  bs::parseSei(rbsp, &builder, false, tree_info);
  const bs::SyntaxNode* primaries = findNode(roots, "display_primaries_x[0]");
  CHECK(primaries != nullptr);
  if (primaries) {
    CHECK_EQ(primaries->bit_pos, 16u);
    CHECK_EQ(primaries->bit_len, 16u);
  }
}

void testHrdCpb() {
  const auto h264_sps = hexBytes(
      "67 64 00 0d ac d9 41 41 fb 01 10 00 00 03 00 10 00 00 03 03 2e 28 "
      "00 30 d4 00 61 ae 6c 30 07 8a 14 cb");
  bs::Rbsp h264_rbsp =
      bs::unescapeRbsp(h264_sps.data() + 1, h264_sps.size() - 1);
  bs::BitReader h264_br(h264_rbsp.data.data(), h264_rbsp.data.size());
  bs::H264Sps h264;
  bs::parseH264Sps(h264_br, nullptr, h264);
  CHECK(h264.valid);
  CHECK(h264.nal_hrd.present);
  CHECK(!h264.vcl_hrd.present);
  CHECK_EQ(h264.nal_hrd.cpb_cnt_minus1, 0u);
  CHECK_EQ(h264.nal_hrd.cpb.size(), 1u);
  if (!h264.nal_hrd.cpb.empty()) {
    CHECK_EQ(h264.nal_hrd.cpb[0].bit_rate, 400000u);
    CHECK_EQ(h264.nal_hrd.cpb[0].cpb_size, 800000u);
    CHECK(h264.nal_hrd.cpb[0].cbr);
  }
  CHECK_EQ(h264.nal_hrd.initial_cpb_removal_delay_length_minus1, 19u);
  CHECK_EQ(h264.nal_hrd.cpb_removal_delay_length_minus1, 12u);
  CHECK_EQ(h264.nal_hrd.dpb_output_delay_length_minus1, 6u);

  const auto h265_vps = hexBytes(
      "40 01 0c 01 ff ff 01 60 00 00 03 00 90 00 00 03 00 00 03 00 3c 95 "
      "94 09");
  bs::Rbsp vps_rbsp =
      bs::unescapeRbsp(h265_vps.data() + 2, h265_vps.size() - 2);
  bs::BitReader vps_br(vps_rbsp.data.data(), vps_rbsp.data.size());
  bs::H265Vps vps;
  bs::parseH265Vps(vps_br, nullptr, &vps);
  CHECK(vps.parsed_fully);
  CHECK_EQ(vps.max_sub_layers_minus1, 0);
  CHECK_EQ(vps.sub_layer_ordering_info_present, true);
  CHECK_EQ(vps.max_dec_pic_buffering_minus1.size(), 1u);
  CHECK_EQ(vps.max_dec_pic_buffering_minus1[0], 4);
  CHECK_EQ(vps.max_layer_id, 0);
  CHECK_EQ(vps.num_layer_sets_minus1, 0);
  CHECK(!vps.timing_info_present);

  const auto h265_sps = hexBytes(
      "42 01 01 01 60 00 00 03 00 90 00 00 03 00 00 03 00 3c a0 0a 08 0f "
      "16 59 59 52 93 0b c0 5a 02 00 00 03 00 02 00 00 03 00 32 c1 49 ac "
      "bc 00 61 a8 00 c3 59");
  bs::Rbsp sps_rbsp =
      bs::unescapeRbsp(h265_sps.data() + 2, h265_sps.size() - 2);
  bs::BitReader sps_br(sps_rbsp.data.data(), sps_rbsp.data.size());
  bs::H265Sps sps;
  bs::parseH265Sps(sps_br, nullptr, sps);
  CHECK(sps.parsed_fully);
  CHECK(sps.vui_hrd_present);
  CHECK(sps.hrd.present);
  CHECK(sps.hrd.nal_hrd_present);
  CHECK(!sps.hrd.vcl_hrd_present);
  CHECK_EQ(sps.hrd.sub_layers.size(), 1u);
  if (!sps.hrd.sub_layers.empty() && !sps.hrd.sub_layers[0].nal_cpb.empty()) {
    CHECK_EQ(sps.hrd.sub_layers[0].nal_cpb[0].bit_rate, 400000u);
    CHECK_EQ(sps.hrd.sub_layers[0].nal_cpb[0].cpb_size, 800000u);
    CHECK(sps.hrd.sub_layers[0].nal_cpb[0].cbr);
  }
  CHECK_EQ(sps.hrd.initial_cpb_removal_delay_length_minus1, 19u);
  CHECK_EQ(sps.hrd.au_cpb_removal_delay_length_minus1, 11u);
  CHECK_EQ(sps.hrd.dpb_output_delay_length_minus1, 5u);

  bs::CpbParams params;
  params.bit_rate = 400000;
  params.cpb_size = 800000;
  params.cbr = true;
  params.delay_bits = 13;
  bs::CpbInitial initial;
  initial.present = true;
  initial.initial_delay = 161999;
  initial.initial_offset = 18001;
  std::vector<bs::CpbAuInput> aus(3);
  aus[0].bits = 64000;
  aus[0].has_removal_delay = true;
  aus[0].removal_delay = 0;
  aus[1].bits = 34000;
  aus[1].has_removal_delay = true;
  aus[1].removal_delay = 2;
  aus[2].bits = 20000;
  aus[2].has_removal_delay = true;
  aus[2].removal_delay = 4;
  bs::CpbModelResult model =
      bs::buildCpbModel(params, initial, 1.0 / 50.0, aus);
  CHECK(model.valid);
  CHECK_EQ(model.samples.size(), 3u);
  CHECK_EQ(model.underflow_count, 0);
  CHECK(model.initial_occupancy > 0.0);

  aus[2].bits = 800000;
  model = bs::buildCpbModel(params, initial, 1.0 / 50.0, aus);
  CHECK(model.valid);
  CHECK(model.underflow_count > 0);
  CHECK_EQ(model.first_underflow_index, 2);
}

void testSeiHrdPayloads() {
  bs::SeiHrdContext ctx;
  ctx.hevc = false;
  ctx.has_sps = true;
  ctx.nal_hrd = true;
  ctx.nal_cpb_cnt_minus1 = 0;
  ctx.initial_cpb_removal_delay_length = 20;
  ctx.cpb_removal_delay_length = 13;
  ctx.dpb_output_delay_length = 7;

  BitWriter bp;
  bp.ue(0);
  bp.u(161999, 20);
  bp.u(18001, 20);
  std::vector<uint8_t> bp_bytes = seiMessage(0, bp.data);
  bp_bytes.push_back(0x80);
  bs::Rbsp bp_rbsp;
  bp_rbsp.data = bp_bytes;
  bs::SeiInfo bp_info;
  bs::parseSei(bp_rbsp, nullptr, false, bp_info, &ctx);
  CHECK(bp_info.buffering_period.present);
  CHECK_EQ(bp_info.buffering_period.sps_id, 0);
  CHECK_EQ(bp_info.buffering_period.nal_initial_cpb_removal_delay.size(), 1u);
  if (!bp_info.buffering_period.nal_initial_cpb_removal_delay.empty()) {
    CHECK_EQ(bp_info.buffering_period.nal_initial_cpb_removal_delay[0],
             161999u);
    CHECK_EQ(bp_info.buffering_period.nal_initial_cpb_removal_offset[0],
             18001u);
  }

  BitWriter pt;
  pt.u(0, 13);
  pt.u(4, 7);
  std::vector<uint8_t> pt_bytes = seiMessage(1, pt.data);
  pt_bytes.push_back(0x80);
  bs::Rbsp pt_rbsp;
  pt_rbsp.data = pt_bytes;
  bs::SeiInfo pt_info;
  bs::parseSei(pt_rbsp, nullptr, false, pt_info, &ctx);
  CHECK(pt_info.pic_timing.present);
  CHECK(pt_info.pic_timing.has_delays);
  CHECK_EQ(pt_info.pic_timing.cpb_removal_delay, 0u);
  CHECK_EQ(pt_info.pic_timing.dpb_output_delay, 4u);
}

// The frame struct does not carry dimensions; the SPS does.
void testStreamDetails() {
  const auto h264 = withStartCode(
      {hexBytes(kH264Sps), hexBytes(kH264Pps), hexBytes(kH264Idr)});
  bs::Stream s;
  std::string error;
  CHECK(s.loadBuffer(h264, error));
  CHECK_EQ(s.h264Sps().width(), 64);
  CHECK_EQ(s.h264Sps().height(), 48);
  CHECK(s.streamSummary().find("64x48") != std::string::npos);
  CHECK_EQ(s.h264Pps().pic_init_qp_minus26, -3);
  CHECK_EQ(s.frames().size(), 1u);
  if (s.frames().size() == 1) {
    CHECK_EQ(s.frames()[0].picture_type, 'I');
    CHECK_EQ(s.frames()[0].qp, 20);
    CHECK_EQ(s.frameTypeName(s.frames()[0]), "IDR");
  }

  const std::string csv = s.framesCsv();
  CHECK(csv.find("index,type,idr") == 0);
  CHECK_EQ(static_cast<int>(std::count(csv.begin(), csv.end(), '\n')), 2);

  const auto h265 = withStartCode({hexBytes(kH265Vps), hexBytes(kH265Sps),
                                   hexBytes(kH265Pps), hexBytes(kH265Idr)});
  bs::Stream s265;
  CHECK(s265.loadBuffer(h265, error));
  CHECK_EQ(static_cast<int>(s265.codec()), static_cast<int>(bs::Codec::H265));
  CHECK_EQ(s265.nals().size(), 4u);
  CHECK_EQ(s265.frames().size(), 1u);
  CHECK_EQ(s265.h265Sps().width, 64);
  CHECK_EQ(s265.h265Sps().height, 48);
  CHECK(s265.streamSummary().find("64x48") != std::string::npos);
  if (s265.frames().size() == 1) {
    CHECK(s265.frames()[0].irap);
    CHECK_EQ(s265.frameTypeName(s265.frames()[0]), "IDR");
  }
}

void testMalformedInput() {
  bs::Stream s;
  std::string error;
  CHECK(!s.loadBuffer({}, error));
  CHECK_EQ(error, std::string("empty file"));

  error.clear();
  CHECK(!s.loadBuffer(hexBytes("00 00 01"), error));
  CHECK(!error.empty());

  // No start codes at all.
  std::vector<uint8_t> junk(256, 0x5A);
  error.clear();
  CHECK(!s.loadBuffer(junk, error));

  // A start code followed by a NAL header with no body: parses without
  // frames and must not crash.
  const auto lone = hexBytes("00 00 01 ff ff ff");
  error.clear();
  s.loadBuffer(lone, error);
  CHECK(s.frames().empty());

  // Truncated parameter sets: no crash, no frames.
  const auto truncated =
      withStartCode({hexBytes("67 42"), hexBytes("68 ce"), hexBytes("65 88")});
  error.clear();
  s.loadBuffer(truncated, error);
  CHECK(s.frames().empty());
}

}  // namespace

int main() {
  testBitReader();
  testRbspUnescape();
  testNalSplitter();
  testH264ParameterSets();
  testH265ParameterSets();
  testSeiHdr();
  testHrdCpb();
  testSeiHrdPayloads();
  testStreamDetails();
  testMalformedInput();

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
