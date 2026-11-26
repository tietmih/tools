#include "core/quality.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace bs {

namespace {

uint32_t sample(const YuvPlaneView& plane, int x, int y, int bit_depth) {
  const uint8_t* row = plane.data + static_cast<size_t>(y) * plane.stride;
  if (bit_depth > 8) {
    const uint8_t* p = row + static_cast<size_t>(x) * 2;
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8);
  }
  return row[x];
}

double psnrFromMse(double mse, double max_value) {
  if (mse <= 0.0) return std::numeric_limits<double>::infinity();
  return 10.0 * std::log10(max_value * max_value / mse);
}

double ssimPlane(const YuvPlaneView& a, const YuvPlaneView& b, int bit_depth,
                 int max_value) {
  const int w4 = a.width >> 2;
  const int h4 = a.height >> 2;
  if (w4 < 2 || h4 < 2) return 0.0;
  const double c1 = 0.01 * 0.01 * max_value * max_value * 64.0 + 0.5;
  const double c2 = 0.03 * 0.03 * max_value * max_value * 64.0 * 63.0 + 0.5;
  std::vector<double> s1(static_cast<size_t>(w4) * h4);
  std::vector<double> s2(static_cast<size_t>(w4) * h4);
  std::vector<double> s11(static_cast<size_t>(w4) * h4);
  std::vector<double> s22(static_cast<size_t>(w4) * h4);
  std::vector<double> s12(static_cast<size_t>(w4) * h4);
  for (int by = 0; by < h4; by++) {
    for (int bx = 0; bx < w4; bx++) {
      double a1 = 0, a2 = 0, a11 = 0, a22 = 0, a12 = 0;
      for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
          const double av = sample(a, bx * 4 + x, by * 4 + y, bit_depth);
          const double bv = sample(b, bx * 4 + x, by * 4 + y, bit_depth);
          a1 += av;
          a2 += bv;
          a11 += av * av;
          a22 += bv * bv;
          a12 += av * bv;
        }
      }
      const size_t at = static_cast<size_t>(by) * w4 + bx;
      s1[at] = a1;
      s2[at] = a2;
      s11[at] = a11;
      s22[at] = a22;
      s12[at] = a12;
    }
  }
  double total = 0.0;
  for (int by = 0; by < h4 - 1; by++) {
    for (int bx = 0; bx < w4 - 1; bx++) {
      double fs1 = 0, fs2 = 0, fss = 0, f12 = 0;
      for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
          const size_t at = static_cast<size_t>(by + dy) * w4 + (bx + dx);
          fs1 += s1[at];
          fs2 += s2[at];
          fss += s11[at] + s22[at];
          f12 += s12[at];
        }
      }
      const double vars = fss * 64.0 - fs1 * fs1 - fs2 * fs2;
      const double covar = f12 * 64.0 - fs1 * fs2;
      total += (2.0 * fs1 * fs2 + c1) * (2.0 * covar + c2) /
               ((fs1 * fs1 + fs2 * fs2 + c1) * (vars + c2));
    }
  }
  return total / static_cast<double>((w4 - 1) * (h4 - 1));
}

}  // namespace

void makeDiffImage(const YuvFrameView& test, const YuvFrameView& reference,
                   std::vector<uint8_t>& bgra) {
  if (!test.planes[0].data || !reference.planes[0].data) return;
  const int width = test.planes[0].width;
  const int height = test.planes[0].height;
  if (width != reference.planes[0].width ||
      height != reference.planes[0].height)
    return;
  bgra.resize(static_cast<size_t>(width) * height * 4);
  const double scale = test.bit_depth > 8 ? 2.0 : 8.0;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      const double a = sample(test.planes[0], x, y, test.bit_depth);
      const double b = sample(reference.planes[0], x, y, test.bit_depth);
      const double diff = std::fabs(a - b) * scale;
      const int value = std::min(255, static_cast<int>(diff));
      const size_t at = (static_cast<size_t>(y) * width + x) * 4;
      bgra[at + 0] = static_cast<uint8_t>(value / 4);
      bgra[at + 1] = static_cast<uint8_t>(value / 2);
      bgra[at + 2] = static_cast<uint8_t>(value);
      bgra[at + 3] = 255;
    }
  }
}

bool computeQuality(const YuvFrameView& test, const YuvFrameView& reference,
                    QualityMetrics& out, std::string& error) {
  if (test.bit_depth != reference.bit_depth ||
      test.chroma_format != reference.chroma_format) {
    error = "reference bit depth or chroma format differs";
    return false;
  }
  for (int p = 0; p < 3; p++) {
    if (!test.planes[p].data || !reference.planes[p].data) {
      error = "missing YUV plane";
      return false;
    }
    if (test.planes[p].width != reference.planes[p].width ||
        test.planes[p].height != reference.planes[p].height) {
      error = "reference plane dimensions differ";
      return false;
    }
  }
  const double max_value = static_cast<double>((1u << test.bit_depth) - 1u);
  double mse[3] = {};
  double msad[3] = {};
  double area[3] = {};
  double total_area = 0.0;
  for (int p = 0; p < 3; p++) {
    const YuvPlaneView& a = test.planes[p];
    const YuvPlaneView& b = reference.planes[p];
    double sum = 0.0;
    double sum_abs = 0.0;
    for (int y = 0; y < a.height; y++) {
      for (int x = 0; x < a.width; x++) {
        const double d = static_cast<double>(sample(a, x, y, test.bit_depth)) -
                         static_cast<double>(sample(b, x, y, test.bit_depth));
        sum += d * d;
        sum_abs += std::fabs(d);
      }
    }
    area[p] = static_cast<double>(a.width) * a.height;
    mse[p] = sum / area[p];
    msad[p] = sum_abs / area[p];
    total_area += area[p];
  }
  const double w0 = area[0] / total_area;
  const double w1 = area[1] / total_area;
  const double w2 = area[2] / total_area;
  const double avg_mse = mse[0] * w0 + mse[1] * w1 + mse[2] * w2;
  out.mse_y = mse[0];
  out.mse_u = mse[1];
  out.mse_v = mse[2];
  out.mse_avg = avg_mse;
  out.msad_y = msad[0];
  out.msad_u = msad[1];
  out.msad_v = msad[2];
  out.msad_avg = msad[0] * w0 + msad[1] * w1 + msad[2] * w2;
  out.max_value = static_cast<int>(max_value);
  out.psnr_y = psnrFromMse(mse[0], max_value);
  out.psnr_u = psnrFromMse(mse[1], max_value);
  out.psnr_v = psnrFromMse(mse[2], max_value);
  out.psnr_avg = psnrFromMse(avg_mse, max_value);
  double ssim = 0.0;
  for (int p = 0; p < 3; p++) {
    const double plane_ssim =
        ssimPlane(test.planes[p], reference.planes[p], test.bit_depth,
                  static_cast<int>(max_value));
    ssim += plane_ssim * (area[p] / total_area);
  }
  out.ssim = ssim;
  return true;
}

}  // namespace bs
