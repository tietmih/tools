#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bs {

struct YuvPlaneView {
  const uint8_t* data = nullptr;
  int stride = 0;
  int width = 0;
  int height = 0;
};

struct YuvFrameView {
  YuvPlaneView planes[3];
  int bit_depth = 8;
  int chroma_format = 1;  // 1 = 4:2:0, 2 = 4:2:2, 3 = 4:4:4
  int matrix = 2;
  bool full_range = false;
};

struct QualitySample {
  bool valid = false;
  double psnr_y = 0.0;
  double psnr_avg = 0.0;
  double ssim = 0.0;
  double mse_avg = 0.0;
  double msad_avg = 0.0;
};

struct QualityMetrics {
  double psnr_y = 0.0;
  double psnr_u = 0.0;
  double psnr_v = 0.0;
  double psnr_avg = 0.0;
  double ssim = 0.0;
  double mse_y = 0.0;
  double mse_u = 0.0;
  double mse_v = 0.0;
  double mse_avg = 0.0;
  double msad_y = 0.0;
  double msad_u = 0.0;
  double msad_v = 0.0;
  double msad_avg = 0.0;
  int max_value = 255;
};

bool computeQuality(const YuvFrameView& test, const YuvFrameView& reference,
                    QualityMetrics& out, std::string& error);

// Builds an amplified luma-difference heat map (BGRA). Dimensions and chroma
// must match between the two views.
void makeDiffImage(const YuvFrameView& test, const YuvFrameView& reference,
                   std::vector<uint8_t>& bgra);

}  // namespace bs
