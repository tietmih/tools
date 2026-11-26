// Host-side validation CLI. Prints a parse digest that can be diffed against
// ffprobe/ffmpeg output for the same elementary stream.
#include <cstdio>
#include <cstring>
#include <string>

#include "core/stream.h"

using namespace bs;

static void printTree(const SyntaxNode& n, int depth) {
  std::printf("%*s%s", depth * 2, "", n.name.c_str());
  if (!n.value.empty()) std::printf(" = %s", n.value.c_str());
  if (!n.is_container())
    std::printf("   [%llu+%llu]", (unsigned long long)n.bit_pos,
                (unsigned long long)n.bit_len);
  std::printf("\n");
  for (const auto& c : n.children) printTree(c, depth + 1);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: bsinfo <file.h264|file.h265> [--frames] [--tree "
                 "<nal-index>] [--cpb] [--refs] [--csv-cpb <path>]\n");
    return 2;
  }
  Stream s;
  std::string err;
  if (!s.openFile(argv[1], err)) {
    std::fprintf(stderr, "error: %s\n", err.c_str());
    return 1;
  }
  std::printf("%s\n", s.streamSummary().c_str());
  std::printf("codec=%s nals=%zu frames=%zu\n", codecName(s.codec()),
              s.nals().size(), s.frames().size());

  bool showFrames = false;
  bool showCpb = false;
  bool showRefs = false;
  long treeIndex = -1;
  const char* csvFrames = nullptr;
  const char* csvNals = nullptr;
  const char* csvCpb = nullptr;
  for (int i = 2; i < argc; i++) {
    if (std::strcmp(argv[i], "--frames") == 0)
      showFrames = true;
    else if (std::strcmp(argv[i], "--tree") == 0 && i + 1 < argc)
      treeIndex = std::strtol(argv[++i], nullptr, 10);
    else if (std::strcmp(argv[i], "--csv-frames") == 0 && i + 1 < argc)
      csvFrames = argv[++i];
    else if (std::strcmp(argv[i], "--csv-nals") == 0 && i + 1 < argc)
      csvNals = argv[++i];
    else if (std::strcmp(argv[i], "--cpb") == 0)
      showCpb = true;
    else if (std::strcmp(argv[i], "--refs") == 0)
      showRefs = true;
    else if (std::strcmp(argv[i], "--csv-cpb") == 0 && i + 1 < argc)
      csvCpb = argv[++i];
  }

  auto writeCsv = [](const char* path, const std::string& text) {
    FILE* file = std::fopen(path, "wb");
    if (!file) {
      std::fprintf(stderr, "error: cannot write %s\n", path);
      return false;
    }
    const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
    std::fwrite(bom, 1, sizeof(bom), file);
    std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);
    std::printf("wrote %s (%zu bytes)\n", path, text.size() + sizeof(bom));
    return true;
  };
  if (csvFrames) writeCsv(csvFrames, s.framesCsv());
  if (csvNals) writeCsv(csvNals, s.nalsCsv());
  if (csvCpb) writeCsv(csvCpb, s.cpbCsv());

  if (showRefs) {
    for (const auto& f : s.frames()) {
      std::printf("refs %d %c poc=%d dpb=%d l0=", f.index, f.picture_type,
                  f.poc, f.refs.dpb_count);
      for (size_t i = 0; i < f.refs.l0_pocs.size(); i++)
        std::printf("%s%d", i ? "," : "", f.refs.l0_pocs[i]);
      std::printf(" l1=");
      for (size_t i = 0; i < f.refs.l1_pocs.size(); i++)
        std::printf("%s%d", i ? "," : "", f.refs.l1_pocs[i]);
      std::printf(" missing=");
      for (size_t i = 0; i < f.refs.missing_pocs.size(); i++)
        std::printf("%s%d", i ? "," : "", f.refs.missing_pocs[i]);
      std::printf("\n");
    }
  }

  if (showCpb) {
    const CpbModelResult& cpb = s.cpbModel();
    std::printf(
        "cpb valid=%d source=%s cpb_index=%d cbr=%d bit_rate_bps=%llu "
        "cpb_size_bits=%llu initial_occupancy_bits=%.3f samples=%zu "
        "underflows=%d first_underflow=%d overflows=%d estimated=%d "
        "error=%s\n",
        cpb.valid ? 1 : 0, cpb.params.source.c_str(), cpb.params.cpb_index,
        cpb.params.cbr ? 1 : 0,
        static_cast<unsigned long long>(cpb.params.bit_rate),
        static_cast<unsigned long long>(cpb.params.cpb_size),
        cpb.initial_occupancy, cpb.samples.size(), cpb.underflow_count,
        cpb.first_underflow_index, cpb.full_events,
        cpb.estimated_initial ? 1 : 0, cpb.error.c_str());
  }

  if (showFrames) {
    std::printf("%-5s %-4s %-4s %-6s %-8s %-10s %-6s %-4s %-6s %-4s\n", "idx",
                "type", "idr", "poc", "frame#", "offset", "size", "qp",
                "slices", "nals");
    for (const auto& f : s.frames()) {
      std::printf("%-5d %-4c %-4d %-6d %-8d %-10zu %-6zu %-4d %-6d %-4d\n",
                  f.index, f.picture_type, f.idr ? 1 : 0, f.poc, f.frame_num,
                  f.byte_offset, f.byte_size, f.qp, f.slice_count, f.nal_count);
    }
  }

  if (treeIndex >= 0) {
    auto tree = s.syntaxTree(static_cast<size_t>(treeIndex));
    for (const auto& r : tree) printTree(r, 0);
  }

  if (treeIndex < 0 && !showFrames) {
    for (size_t i = 0; i < s.nals().size(); i++) {
      const NalEntry& e = s.nals()[i];
      std::printf("[%4zu] off=%-9zu size=%-7zu type=%-3d %-32s %s%s%s\n", i,
                  e.unit.offset, e.unit.size, e.unit.nal_type, e.unit.type_name,
                  e.summary.c_str(),
                  e.error.empty() ? "" : "  ERROR: ", e.error.c_str());
    }
  }
  return 0;
}
