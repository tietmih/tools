// Compare two raw planar YUV files: PSNR, SSIM, MSE and MSAD per frame.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/quality.h"
#include "core/reference_yuv.h"

using namespace bs;

namespace {

bool parseLayout(const std::string& name, YuvLayout& layout) {
  if (name == "i420" || name == "yuv420p")
    layout = YuvLayout::I420;
  else if (name == "i422" || name == "yuv422p")
    layout = YuvLayout::I422;
  else if (name == "i444" || name == "yuv444p")
    layout = YuvLayout::I444;
  else if (name == "p010" || name == "yuv420p10le")
    layout = YuvLayout::P010;
  else if (name == "p210" || name == "yuv422p10le")
    layout = YuvLayout::P210;
  else if (name == "p012" || name == "yuv420p12le")
    layout = YuvLayout::P012;
  else if (name == "p212" || name == "yuv422p12le")
    layout = YuvLayout::P212;
  else
    return false;
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: yuvcmp <a.yuv> <b.yuv> --format i420 [--width W] "
                 "[--height H] [--csv out.csv] [--limit N]\n");
    return 2;
  }
  const char* a_path = argv[1];
  const char* b_path = argv[2];
  std::string format = "i420";
  int width = 0;
  int height = 0;
  const char* csv_path = nullptr;
  int limit = 0;
  for (int i = 3; i < argc; i++) {
    if (std::strcmp(argv[i], "--format") == 0 && i + 1 < argc)
      format = argv[++i];
    else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc)
      width = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc)
      height = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--csv") == 0 && i + 1 < argc)
      csv_path = argv[++i];
    else if (std::strcmp(argv[i], "--limit") == 0 && i + 1 < argc)
      limit = std::atoi(argv[++i]);
  }
  YuvLayout layout = YuvLayout::None;
  if (!parseLayout(format, layout)) {
    std::fprintf(stderr, "unsupported --format %s\n", format.c_str());
    return 2;
  }
  ReferenceYuv a;
  ReferenceYuv b;
  std::string error;
  if (!a.open(a_path, layout, width, height, error)) {
    std::fprintf(stderr, "open %s: %s\n", a_path, error.c_str());
    return 1;
  }
  if (!b.open(b_path, layout, width, height, error)) {
    std::fprintf(stderr, "open %s: %s\n", b_path, error.c_str());
    return 1;
  }
  int frames = std::min(a.frameCount(), b.frameCount());
  if (a.frameCount() != b.frameCount())
    std::fprintf(stderr, "warning: frame counts differ (%d vs %d)\n",
                 a.frameCount(), b.frameCount());
  if (limit > 0) frames = std::min(frames, limit);
  FILE* csv = nullptr;
  if (csv_path) {
    csv = std::fopen(csv_path, "wb");
    if (!csv) {
      std::fprintf(stderr, "cannot write %s\n", csv_path);
      return 1;
    }
    const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
    std::fwrite(bom, 1, 3, csv);
    std::fprintf(csv, "frame,psnr_y,psnr_avg,ssim,mse_avg,msad_avg\r\n");
  }
  std::vector<uint8_t> storage_a;
  std::vector<uint8_t> storage_b;
  double sum_mse = 0.0;
  double sum_ssim = 0.0;
  int measured = 0;
  int max_value = 255;
  for (int index = 0; index < frames; index++) {
    YuvFrameView view_a;
    YuvFrameView view_b;
    if (!a.readFrame(index, storage_a, view_a, error) ||
        !b.readFrame(index, storage_b, view_b, error)) {
      std::fprintf(stderr, "frame %d: %s\n", index, error.c_str());
      break;
    }
    QualityMetrics metrics;
    if (!computeQuality(view_a, view_b, metrics, error)) {
      std::fprintf(stderr, "frame %d: %s\n", index, error.c_str());
      break;
    }
    sum_mse += metrics.mse_avg;
    sum_ssim += metrics.ssim;
    max_value = metrics.max_value;
    measured++;
    std::printf(
        "frame %d psnr_y=%.4f psnr_avg=%.4f ssim=%.6f mse=%.4f "
        "msad=%.4f\n",
        index, metrics.psnr_y, metrics.psnr_avg, metrics.ssim, metrics.mse_avg,
        metrics.msad_avg);
    if (csv) {
      std::fprintf(csv, "%d,%.4f,%.4f,%.6f,%.4f,%.4f\r\n", index,
                   metrics.psnr_y, metrics.psnr_avg, metrics.ssim,
                   metrics.mse_avg, metrics.msad_avg);
    }
  }
  if (csv) std::fclose(csv);
  if (measured > 0) {
    const double mean_mse = sum_mse / measured;
    const double mean_ssim = sum_ssim / measured;
    std::printf("summary frames=%d", measured);
    if (mean_mse <= 0.0) {
      std::printf(" psnr_avg=inf");
    } else {
      std::printf(" psnr_avg=%.4f",
                  10.0 * std::log10(static_cast<double>(max_value) * max_value /
                                    mean_mse));
    }
    std::printf(" ssim=%.6f\n", mean_ssim);
  }
  return measured > 0 ? 0 : 1;
}
