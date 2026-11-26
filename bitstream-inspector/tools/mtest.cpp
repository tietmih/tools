// Validation tool for the container demux / decode layer.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "core/quality.h"
#include "core/stream.h"
#include "core/ts.h"
#include "media/media_file.h"

using namespace bs;

static bool writeYuvPlanes(FILE* f, const DecodedFrame& frame) {
  bool ok = true;
  for (const auto& plane : frame.planes) {
    if (std::fwrite(plane.data(), 1, plane.size(), f) != plane.size())
      ok = false;
  }
  return ok;
}

static bool writeYuv(const char* path, const DecodedFrame& frame) {
  FILE* f = std::fopen(path, "wb");
  if (!f) return false;
  const bool ok = writeYuvPlanes(f, frame);
  std::fclose(f);
  return ok;
}

static bool writePpm(const char* path, const std::vector<uint8_t>& bgra,
                     int width, int height) {
  FILE* f = std::fopen(path, "wb");
  if (!f) return false;
  std::fprintf(f, "P6\n%d %d\n255\n", width, height);
  std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
  for (size_t i = 0, j = 0; i < bgra.size(); i += 4, j += 3) {
    rgb[j + 0] = bgra[i + 2];
    rgb[j + 1] = bgra[i + 1];
    rgb[j + 2] = bgra[i + 0];
  }
  std::fwrite(rgb.data(), 1, rgb.size(), f);
  std::fclose(f);
  return true;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: mtest <file> [--frame N] [--ppm out.ppm] "
                 "[--yuv out.yuv] [--range start:end] [--ts] "
                 "[--ref ref.yuv] [--ref-format fmt] [--quality] "
                 "[--quality-csv out.csv]\n");
    return 2;
  }
  for (int i = 2; i < argc; i++) {
    if (std::strcmp(argv[i], "--ts") != 0) continue;
    TsReport report;
    if (!analyzeTsFile(argv[1], report)) {
      std::fprintf(stderr, "ts analysis failed: %s\n", report.error.c_str());
      return 1;
    }
    std::printf(
        "ts packet_size=%d packets=%llu pids=%zu continuity_errors=%d "
        "transport_errors=%d scrambled=%d pcr_count=%d pcr_jitter_us=%.3f\n",
        report.packet_size, static_cast<unsigned long long>(report.packets),
        report.pids.size(), report.continuity_errors, report.transport_errors,
        report.scrambled_packets, report.pcr_count, report.pcr_jitter_us);
    for (const auto& info : report.pids) {
      std::printf("pid=%d packets=%llu stream_type=%d pmt=%d referenced=%d\n",
                  info.pid, static_cast<unsigned long long>(info.packets),
                  info.stream_type, info.pmt ? 1 : 0, info.referenced ? 1 : 0);
    }
    return 0;
  }

  MediaFile media;
  std::string error;
  if (!media.open(argv[1], error)) {
    std::fprintf(stderr, "open failed: %s\n", error.c_str());
    return 1;
  }
  const ContainerInfo& info = media.info();
  std::printf(
      "format=%s codec=%s %dx%d fps=%.3f duration=%lldms bitrate=%lld raw=%d\n",
      info.format.c_str(), info.codec.c_str(), info.width, info.height,
      info.fps, static_cast<long long>(info.duration_ms),
      static_cast<long long>(info.bit_rate), info.raw_es ? 1 : 0);

  std::vector<uint8_t> es;
  if (!media.extractAnnexB(es, error)) {
    std::fprintf(stderr, "extract failed: %s\n", error.c_str());
    return 1;
  }
  std::printf("es_bytes=%zu packets=%zu\n", es.size(), media.packets().size());

  Stream stream;
  if (!stream.loadBuffer(std::move(es), error)) {
    std::fprintf(stderr, "analyze failed: %s\n", error.c_str());
    return 1;
  }
  media.applyTiming(stream);
  std::printf("%s\n", stream.streamSummary().c_str());
  size_t n = stream.frames().size();
  std::printf("frames=%zu\n", n);
  for (size_t i = 0; i < n && i < 12; i++) {
    const FrameInfo& f = stream.frames()[i];
    std::printf("%3zu %-4s poc=%-4d pkt=%-4d pts=%-10lld dts=%-10lld t=%.3fs\n",
                i, stream.frameTypeName(f).c_str(), f.poc, f.packet_index,
                static_cast<long long>(f.pts), static_cast<long long>(f.dts),
                f.time_sec);
  }

  int frame_index = 0;
  const char* ppm_path = nullptr;
  const char* yuv_path = nullptr;
  const char* range = nullptr;
  const char* ref_path = nullptr;
  const char* ref_format = "yuv420p";
  const char* quality_csv = nullptr;
  bool quality = false;
  for (int i = 2; i < argc; i++) {
    if (std::strcmp(argv[i], "--frame") == 0 && i + 1 < argc)
      frame_index = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--ppm") == 0 && i + 1 < argc)
      ppm_path = argv[++i];
    else if (std::strcmp(argv[i], "--yuv") == 0 && i + 1 < argc)
      yuv_path = argv[++i];
    else if (std::strcmp(argv[i], "--range") == 0 && i + 1 < argc)
      range = argv[++i];
    else if (std::strcmp(argv[i], "--ref") == 0 && i + 1 < argc)
      ref_path = argv[++i];
    else if (std::strcmp(argv[i], "--ref-format") == 0 && i + 1 < argc)
      ref_format = argv[++i];
    else if (std::strcmp(argv[i], "--quality") == 0)
      quality = true;
    else if (std::strcmp(argv[i], "--quality-csv") == 0 && i + 1 < argc)
      quality_csv = argv[++i];
  }
  if (quality && ref_path) {
    int chroma = 1;
    int depth = 8;
    const std::string format = ref_format;
    if (format == "yuv422p") chroma = 2;
    if (format == "yuv444p") chroma = 3;
    if (format == "yuv420p10le") depth = 10;
    if (format == "yuv422p10le") {
      chroma = 2;
      depth = 10;
    }
    if (chroma != 1 && chroma != 2 && chroma != 3) {
      std::fprintf(stderr, "unsupported --ref-format\n");
      return 2;
    }
    FILE* ref_file = std::fopen(ref_path, "rb");
    if (!ref_file) {
      std::fprintf(stderr, "cannot open %s\n", ref_path);
      return 1;
    }
    std::vector<size_t> order(stream.frames().size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
      return stream.frames()[a].display_index <
             stream.frames()[b].display_index;
    });
    std::vector<uint8_t> ref;
    int ref_w = 0, ref_h = 0;
    double mse_y = 0.0, mse_u = 0.0, mse_v = 0.0, mse_avg = 0.0;
    double ssim_sum = 0.0;
    int count = 0;
    int max_value = 255;
    FILE* csv = nullptr;
    if (quality_csv) {
      csv = std::fopen(quality_csv, "wb");
      if (csv) {
        const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
        std::fwrite(bom, 1, 3, csv);
        std::fprintf(csv,
                     "display_index,frame,psnr_y,psnr_u,psnr_v,psnr_avg,"
                     "ssim\r\n");
      }
    }
    for (size_t position = 0; position < order.size(); position++) {
      const int frame_index = static_cast<int>(order[position]);
      DecodedFrame frame;
      bool keyframe_only = false;
      if (!media.decodeFrameAt(stream, frame_index, frame, keyframe_only,
                               error)) {
        std::fprintf(stderr, "decode failed: %s\n", error.c_str());
        std::fclose(ref_file);
        if (csv) std::fclose(csv);
        return 1;
      }
      if (ref_w == 0) {
        ref_w = frame.width;
        ref_h = frame.height;
        if (frame.chroma_format != chroma || frame.bit_depth != depth) {
          std::fprintf(stderr,
                       "reference format does not match decoded "
                       "frame\n");
          std::fclose(ref_file);
          if (csv) std::fclose(csv);
          return 1;
        }
        const size_t cw = (chroma == 3) ? ref_w : (ref_w + 1) / 2;
        const size_t ch = (chroma == 1) ? (ref_h + 1) / 2 : ref_h;
        const size_t bytes = depth > 8 ? 2 : 1;
        const size_t frame_size =
            (static_cast<size_t>(ref_w) * ref_h + 2 * cw * ch) * bytes;
        ref.resize(frame_size);
      }
      if (std::fread(ref.data(), 1, ref.size(), ref_file) != ref.size()) {
        std::fprintf(stderr, "reference YUV ended before frame %zu\n",
                     position);
        break;
      }
      const size_t cw = (chroma == 3) ? ref_w : (ref_w + 1) / 2;
      const size_t ch = (chroma == 1) ? (ref_h + 1) / 2 : ref_h;
      const size_t bytes = depth > 8 ? 2 : 1;
      YuvFrameView test;
      YuvFrameView reference;
      test.bit_depth = reference.bit_depth = depth;
      test.chroma_format = reference.chroma_format = chroma;
      for (int p = 0; p < 3; p++) {
        const int pw = p == 0 ? ref_w : static_cast<int>(cw);
        const int ph = p == 0 ? ref_h : static_cast<int>(ch);
        test.planes[p] = {frame.planes[static_cast<size_t>(p)].data(),
                          frame.strides[static_cast<size_t>(p)], pw, ph};
      }
      size_t offset = 0;
      for (int p = 0; p < 3; p++) {
        const int pw = p == 0 ? ref_w : static_cast<int>(cw);
        const int ph = p == 0 ? ref_h : static_cast<int>(ch);
        reference.planes[p] = {ref.data() + offset,
                               pw * static_cast<int>(bytes), pw, ph};
        offset += static_cast<size_t>(pw) * ph * bytes;
      }
      QualityMetrics metrics;
      std::string quality_error;
      if (!computeQuality(test, reference, metrics, quality_error)) {
        std::fprintf(stderr, "quality failed: %s\n", quality_error.c_str());
        std::fclose(ref_file);
        if (csv) std::fclose(csv);
        return 1;
      }
      mse_y += metrics.mse_y;
      mse_u += metrics.mse_u;
      mse_v += metrics.mse_v;
      mse_avg += metrics.mse_avg;
      max_value = metrics.max_value;
      ssim_sum += metrics.ssim;
      count++;
      std::printf(
          "quality %zu frame=%d psnr_y=%.4f psnr_u=%.4f "
          "psnr_v=%.4f psnr_avg=%.4f ssim=%.6f\n",
          position, frame_index, metrics.psnr_y, metrics.psnr_u, metrics.psnr_v,
          metrics.psnr_avg, metrics.ssim);
      if (csv) {
        std::fprintf(csv, "%zu,%d,%.4f,%.4f,%.4f,%.4f,%.6f\r\n", position,
                     frame_index, metrics.psnr_y, metrics.psnr_u,
                     metrics.psnr_v, metrics.psnr_avg, metrics.ssim);
      }
    }
    std::fclose(ref_file);
    if (csv) std::fclose(csv);
    if (count > 0) {
      auto psnr = [&](double mse) {
        if (mse <= 0.0) return std::numeric_limits<double>::infinity();
        return 10.0 * std::log10(static_cast<double>(max_value) * max_value /
                                 (mse / count));
      };
      std::printf(
          "quality_summary frames=%d psnr_y=%.4f psnr_u=%.4f psnr_v=%.4f "
          "psnr_avg=%.4f ssim=%.6f\n",
          count, psnr(mse_y), psnr(mse_u), psnr(mse_v), psnr(mse_avg),
          ssim_sum / count);
    }
    return count > 0 ? 0 : 1;
  }

  if (range && yuv_path) {
    int first = 0, last = 0;
    if (std::sscanf(range, "%d:%d", &first, &last) != 2 || first < 0 ||
        last < first) {
      std::fprintf(stderr, "invalid --range (expected start:end)\n");
      return 2;
    }
    FILE* out = std::fopen(yuv_path, "wb");
    if (!out) {
      std::fprintf(stderr, "cannot write %s\n", yuv_path);
      return 1;
    }
    int written = 0;
    for (int index = first; index < last; index++) {
      DecodedFrame frame;
      bool keyframe_only = false;
      if (!media.decodeFrameAt(stream, index, frame, keyframe_only, error))
        break;
      if (!writeYuvPlanes(out, frame)) break;
      written++;
    }
    std::fclose(out);
    std::printf("wrote %s (%d frames)\n", yuv_path, written);
    if (written != last - first) {
      std::fprintf(stderr, "decode failed: %s\n", error.c_str());
      return 1;
    }
    return 0;
  }

  if (ppm_path || yuv_path) {
    DecodedFrame frame;
    bool keyframe_only = false;
    if (!media.decodeFrameAt(stream, frame_index, frame, keyframe_only,
                             error)) {
      std::fprintf(stderr, "decode failed: %s\n", error.c_str());
      return 1;
    }
    std::printf(
        "decoded frame=%d %dx%d keyframe_only=%d depth=%d chroma=%d "
        "planes=%zu\n",
        frame_index, frame.width, frame.height, keyframe_only ? 1 : 0,
        frame.bit_depth, frame.chroma_format, frame.planes.size());
    if (ppm_path) {
      if (!writePpm(ppm_path, frame.bgra, frame.width, frame.height)) {
        std::fprintf(stderr, "cannot write %s\n", ppm_path);
        return 1;
      }
      std::printf("wrote %s\n", ppm_path);
    }
    if (yuv_path) {
      if (!writeYuv(yuv_path, frame)) {
        std::fprintf(stderr, "cannot write %s\n", yuv_path);
        return 1;
      }
      std::printf("wrote %s\n", yuv_path);
    }
  }
  return 0;
}
