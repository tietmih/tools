#include "media/yuv_convert.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace bs {

namespace {

AVPixelFormat pixelFormat(int chroma, int depth) {
  if (depth == 8) {
    if (chroma == 1) return AV_PIX_FMT_YUV420P;
    if (chroma == 2) return AV_PIX_FMT_YUV422P;
    return AV_PIX_FMT_YUV444P;
  }
  if (depth == 10) {
    if (chroma == 1) return AV_PIX_FMT_YUV420P10LE;
    if (chroma == 2) return AV_PIX_FMT_YUV422P10LE;
    return AV_PIX_FMT_YUV444P10LE;
  }
  if (chroma == 1) return AV_PIX_FMT_YUV420P12LE;
  if (chroma == 2) return AV_PIX_FMT_YUV422P12LE;
  return AV_PIX_FMT_YUV444P12LE;
}

AVPixelFormat pixelFormat(YuvLayout layout) {
  switch (layout) {
    case YuvLayout::I420:
      return AV_PIX_FMT_YUV420P;
    case YuvLayout::I422:
      return AV_PIX_FMT_YUV422P;
    case YuvLayout::I444:
      return AV_PIX_FMT_YUV444P;
    case YuvLayout::P010:
      return AV_PIX_FMT_YUV420P10LE;
    case YuvLayout::P210:
      return AV_PIX_FMT_YUV422P10LE;
    case YuvLayout::P012:
      return AV_PIX_FMT_YUV420P12LE;
    case YuvLayout::P212:
      return AV_PIX_FMT_YUV422P12LE;
    default:
      return AV_PIX_FMT_NONE;
  }
}

int colorspace(int matrix) {
  if (matrix == 5 || matrix == 6 || matrix == 7) return SWS_CS_ITU601;
  if (matrix == 9 || matrix == 10) return SWS_CS_BT2020;
  return SWS_CS_ITU709;
}

}  // namespace

bool yuvToBgra(const YuvFrameView& frame, int matrix, bool full_range,
               std::vector<uint8_t>& bgra, std::string& error) {
  if (!frame.planes[0].data || frame.planes[0].width <= 0 ||
      frame.planes[0].height <= 0) {
    error = "empty YUV frame";
    return false;
  }
  const int width = frame.planes[0].width;
  const int height = frame.planes[0].height;
  const AVPixelFormat format =
      pixelFormat(frame.chroma_format, frame.bit_depth);
  AVFrame av = {};
  av.width = width;
  av.height = height;
  av.format = format;
  for (int p = 0; p < 3; p++) {
    av.data[p] = const_cast<uint8_t*>(frame.planes[p].data);
    av.linesize[p] = frame.planes[p].stride;
  }
  SwsContext* sws = sws_getCachedContext(nullptr, width, height, format, width,
                                         height, AV_PIX_FMT_BGRA, SWS_BILINEAR,
                                         nullptr, nullptr, nullptr);
  if (!sws) {
    error = "cannot create the YUV conversion context";
    return false;
  }
  const int* coefficients = sws_getCoefficients(colorspace(matrix));
  sws_setColorspaceDetails(sws, coefficients, full_range ? 1 : 0, coefficients,
                           1, 0, 1 << 16, 1 << 16);
  bgra.resize(static_cast<size_t>(width) * height * 4);
  uint8_t* dst[4] = {bgra.data(), nullptr, nullptr, nullptr};
  int dst_stride[4] = {width * 4, 0, 0, 0};
  sws_scale(sws, av.data, av.linesize, 0, height, dst, dst_stride);
  sws_freeContext(sws);
  return true;
}

bool yuvToYuv(const YuvFrameView& frame, YuvLayout target,
              std::vector<uint8_t>& out, std::string& error) {
  if (!frame.planes[0].data || frame.planes[0].width <= 0 ||
      frame.planes[0].height <= 0) {
    error = "empty YUV frame";
    return false;
  }
  const AVPixelFormat dst_format = pixelFormat(target);
  if (dst_format == AV_PIX_FMT_NONE) {
    error = "unsupported target pixel format";
    return false;
  }
  const int width = frame.planes[0].width;
  const int height = frame.planes[0].height;
  const AVPixelFormat src_format =
      pixelFormat(frame.chroma_format, frame.bit_depth);
  AVFrame src = {};
  src.width = width;
  src.height = height;
  src.format = src_format;
  for (int p = 0; p < 3; p++) {
    src.data[p] = const_cast<uint8_t*>(frame.planes[p].data);
    src.linesize[p] = frame.planes[p].stride;
  }
  SwsContext* sws =
      sws_getCachedContext(nullptr, width, height, src_format, width, height,
                           dst_format, SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (!sws) {
    error = "cannot create the YUV conversion context";
    return false;
  }
  int chroma = 1;
  int bit_depth = 8;
  switch (target) {
    case YuvLayout::I420:
      chroma = 1;
      break;
    case YuvLayout::I422:
      chroma = 2;
      break;
    case YuvLayout::I444:
      chroma = 3;
      break;
    case YuvLayout::P010:
      bit_depth = 10;
      break;
    case YuvLayout::P210:
      chroma = 2;
      bit_depth = 10;
      break;
    case YuvLayout::P012:
      bit_depth = 12;
      break;
    case YuvLayout::P212:
      chroma = 2;
      bit_depth = 12;
      break;
    default:
      break;
  }
  const int bytes = bit_depth > 8 ? 2 : 1;
  const int cw = chroma == 3 ? width : (width + 1) / 2;
  const int ch = chroma == 1 ? (height + 1) / 2 : height;
  const size_t y_size = static_cast<size_t>(width) * height * bytes;
  const size_t c_size = static_cast<size_t>(cw) * ch * bytes;
  out.resize(y_size + 2 * c_size);
  uint8_t* dst[4] = {out.data(), out.data() + y_size,
                     out.data() + y_size + c_size, nullptr};
  int dst_stride[4] = {width * bytes, cw * bytes, cw * bytes, 0};
  sws_scale(sws, src.data, src.linesize, 0, height, dst, dst_stride);
  sws_freeContext(sws);
  return true;
}

}  // namespace bs
