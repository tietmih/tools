#include "core/conformance.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bs {

namespace {

struct LevelLimit {
  int code;
  int max_luma;
  int64_t max_sample_rate;
  int max_dpb;
  int max_bitrate_kbps;
};

const LevelLimit kH264[] = {
    {10, 396, 1485, 396, 64},
    {11, 396, 3000, 396, 192},
    {12, 396, 6000, 396, 384},
    {13, 396, 11880, 396, 768},
    {20, 396, 11880, 396, 2000},
    {21, 792, 19800, 792, 4000},
    {22, 1620, 40500, 1620, 4000},
    {30, 1620, 40500, 8100, 10000},
    {31, 3600, 108000, 18000, 14000},
    {32, 5120, 216000, 20480, 20000},
    {40, 8192, 245760, 32768, 20000},
    {41, 8192, 245760, 32768, 50000},
    {42, 8704, 522240, 34816, 50000},
    {50, 22080, 589824, 110400, 135000},
    {51, 36864, 983040, 184320, 240000},
    {52, 36864, 2073600, 184320, 240000},
    {60, 139264, 4177920, 696320, 240000},
    {61, 139264, 8355840, 696320, 480000},
    {62, 139264, 16711680, 696320, 800000},
};

const LevelLimit kH265[] = {
    {30, 36864, 552960, 16, 128},
    {60, 122880, 3686400, 16, 1500},
    {63, 245760, 7372800, 16, 3000},
    {90, 552960, 16588800, 16, 6000},
    {93, 983040, 33177600, 16, 10000},
    {120, 2228224, 66846720, 16, 12000},
    {123, 2228224, 133693440, 16, 20000},
    {150, 8912896, 267386880, 16, 25000},
    {153, 8912896, 534773760, 16, 40000},
    {156, 8912896, 1069547520, 16, 60000},
    {180, 35651584, 1069547520, 16, 60000},
    {183, 35651584, 2139095040, 16, 120000},
    {186, 35651584, 4278190080, 16, 240000},
};

const LevelLimit* findLimit(const LevelLimit* table, size_t count, int code) {
  for (size_t i = 0; i < count; i++)
    if (table[i].code == code) return &table[i];
  return nullptr;
}

void add(std::vector<ConformanceIssue>& issues, const char* code,
         const std::string& message) {
  issues.push_back({code, message});
}

}  // namespace

std::vector<ConformanceIssue> checkH264Conformance(const H264Sps& sps,
                                                   double fps,
                                                   uint64_t bitrate) {
  std::vector<ConformanceIssue> issues;
  const LevelLimit* limit = findLimit(kH264, std::size(kH264), sps.level_idc);
  if (!limit) {
    add(issues, "unknown-level",
        "level_idc " + std::to_string(sps.level_idc) + " has no Annex A limit");
    return issues;
  }
  const int luma = sps.width() * sps.height();
  const int max_luma = limit->max_luma * 256;
  if (luma > max_luma)
    add(issues, "picture-size",
        "luma samples " + std::to_string(luma) + " exceed level limit " +
            std::to_string(max_luma));
  const int mb_w = (sps.width() + 15) / 16;
  const int mb_h = (sps.height() + 15) / 16;
  const double rate = mb_w * static_cast<double>(mb_h) * fps;
  if (fps > 0.0 && rate > limit->max_sample_rate)
    add(issues, "mb-rate",
        "MB/s " + std::to_string(static_cast<long long>(rate)) +
            " exceeds level limit " + std::to_string(limit->max_sample_rate));
  const int dpb_frames =
      static_cast<int>(limit->max_dpb / std::max(1, mb_w * mb_h));
  if (dpb_frames > 0 && sps.vui_present &&
      static_cast<int>(sps.max_dec_frame_buffering) > dpb_frames)
    add(issues, "dpb-size",
        "max_dec_frame_buffering " +
            std::to_string(sps.max_dec_frame_buffering) +
            " exceeds level DPB capacity " + std::to_string(dpb_frames));
  if (bitrate > 0 &&
      bitrate > static_cast<uint64_t>(limit->max_bitrate_kbps) * 1000)
    add(issues, "bitrate",
        "bitrate " + std::to_string(bitrate) + " exceeds level limit " +
            std::to_string(limit->max_bitrate_kbps) + " kbit/s");
  return issues;
}

std::vector<ConformanceIssue> checkH265Conformance(const H265Sps& sps,
                                                   double fps,
                                                   uint64_t bitrate) {
  std::vector<ConformanceIssue> issues;
  const LevelLimit* limit =
      findLimit(kH265, std::size(kH265), sps.ptl.level_idc);
  if (!limit) {
    add(issues, "unknown-level",
        "general_level_idc " + std::to_string(sps.ptl.level_idc) +
            " has no Annex A limit");
    return issues;
  }
  const int luma = sps.width * sps.height;
  if (luma > limit->max_luma)
    add(issues, "picture-size",
        "luma samples " + std::to_string(luma) + " exceed level limit " +
            std::to_string(limit->max_luma));
  const int ctb = std::max(1, sps.ctb_size());
  const int ctb_w = (sps.width + ctb - 1) / ctb;
  const int ctb_h = (sps.height + ctb - 1) / ctb;
  const double rate = ctb_w * static_cast<double>(ctb_h) * fps;
  if (fps > 0.0 && rate > limit->max_sample_rate)
    add(issues, "ctb-rate",
        "CTB/s " + std::to_string(static_cast<long long>(rate)) +
            " exceeds level limit " + std::to_string(limit->max_sample_rate));
  const int dpb_frames = sps.max_dec_pic_buffering_minus1 + 1;
  if (dpb_frames > limit->max_dpb)
    add(issues, "dpb-size",
        "max_dec_pic_buffering " + std::to_string(dpb_frames) +
            " exceeds level limit " + std::to_string(limit->max_dpb));
  if (bitrate > 0 &&
      bitrate > static_cast<uint64_t>(limit->max_bitrate_kbps) * 1000)
    add(issues, "bitrate",
        "bitrate " + std::to_string(bitrate) + " exceeds level limit " +
            std::to_string(limit->max_bitrate_kbps) + " kbit/s");
  return issues;
}

}  // namespace bs
