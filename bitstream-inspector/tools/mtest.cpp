// Validation tool for the container demux / decode layer.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/stream.h"
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
                 "[--yuv out.yuv] [--range start:end]\n");
    return 2;
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
  for (int i = 2; i < argc; i++) {
    if (std::strcmp(argv[i], "--frame") == 0 && i + 1 < argc)
      frame_index = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--ppm") == 0 && i + 1 < argc)
      ppm_path = argv[++i];
    else if (std::strcmp(argv[i], "--yuv") == 0 && i + 1 < argc)
      yuv_path = argv[++i];
    else if (std::strcmp(argv[i], "--range") == 0 && i + 1 < argc)
      range = argv[++i];
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
