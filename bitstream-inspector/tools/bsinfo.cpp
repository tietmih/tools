// Host-side validation CLI. Prints a parse digest that can be diffed against
// ffprobe/ffmpeg output for the same elementary stream.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "core/conformance.h"
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

static std::string jsonEscape(const std::string& text) {
  std::string out;
  for (char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

static std::string nodeJson(const SyntaxNode& node) {
  std::string out = "{\"name\":\"" + jsonEscape(node.name) + "\",\"value\":\"" +
                    jsonEscape(node.value) +
                    "\",\"bit\":" + std::to_string(node.bit_pos) +
                    ",\"len\":" + std::to_string(node.bit_len) +
                    ",\"error\":" + (node.is_error ? "true" : "false");
  if (!node.children.empty()) {
    out += ",\"children\":[";
    for (size_t i = 0; i < node.children.size(); i++) {
      if (i) out += ',';
      out += nodeJson(node.children[i]);
    }
    out += ']';
  }
  out += '}';
  return out;
}

static void printStreamJson(const Stream& s, const char* path, long treeIndex) {
  std::printf("{\"file\":\"%s\",", jsonEscape(path).c_str());
  std::printf("\"summary\":\"%s\",", jsonEscape(s.streamSummary()).c_str());
  std::printf("\"codec\":\"%s\",", codecName(s.codec()));
  std::printf("\"frames\":[");
  for (size_t i = 0; i < s.frames().size(); i++) {
    const FrameInfo& f = s.frames()[i];
    if (i) std::printf(",");
    std::printf(
        "{\"index\":%d,\"type\":\"%s\",\"poc\":%d,\"qp\":%d,"
        "\"size\":%zu,\"offset\":%zu,\"slices\":%d,\"nals\":%d,"
        "\"dpb\":%d,\"slice_addresses\":[",
        f.index, s.frameTypeName(f).c_str(), f.poc, f.qp, f.byte_size,
        f.byte_offset, f.slice_count, f.nal_count, f.refs.dpb_count);
    for (size_t k = 0; k < f.slice_addresses.size(); k++)
      std::printf("%s%d", k ? "," : "", f.slice_addresses[k]);
    std::printf("],\"l0\":[");
    for (size_t k = 0; k < f.refs.l0_pocs.size(); k++)
      std::printf("%s%d", k ? "," : "", f.refs.l0_pocs[k]);
    std::printf("],\"l1\":[");
    for (size_t k = 0; k < f.refs.l1_pocs.size(); k++)
      std::printf("%s%d", k ? "," : "", f.refs.l1_pocs[k]);
    std::printf("],\"missing\":[");
    for (size_t k = 0; k < f.refs.missing_pocs.size(); k++)
      std::printf("%s%d", k ? "," : "", f.refs.missing_pocs[k]);
    std::printf("]}");
  }
  std::printf("],\"nals\":[");
  for (size_t i = 0; i < s.nals().size(); i++) {
    const NalEntry& e = s.nals()[i];
    if (i) std::printf(",");
    std::printf(
        "{\"index\":%zu,\"offset\":%zu,\"size\":%zu,\"type\":%d,"
        "\"summary\":\"%s\",\"error\":\"%s\"}",
        i, e.unit.offset, e.unit.size, e.unit.nal_type,
        jsonEscape(e.summary).c_str(), jsonEscape(e.error).c_str());
  }
  std::printf("]");
  if (treeIndex >= 0 && treeIndex < static_cast<long>(s.nals().size())) {
    auto tree = s.syntaxTree(static_cast<size_t>(treeIndex));
    std::printf(",\"tree\":[");
    for (size_t i = 0; i < tree.size(); i++) {
      if (i) std::printf(",");
      std::printf("%s", nodeJson(tree[i]).c_str());
    }
    std::printf("]");
  }
  std::printf("}\n");
}

static bool isStreamFile(const std::filesystem::path& path) {
  std::string ext = path.extension().string();
  for (char& c : ext) c = static_cast<char>(std::tolower(c));
  return ext == ".h264" || ext == ".264" || ext == ".avc" || ext == ".h265" ||
         ext == ".265" || ext == ".hevc";
}

static int runBatchJson(const char* directory) {
  std::error_code ec;
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
    if (entry.is_regular_file() && isStreamFile(entry.path()))
      files.push_back(entry.path());
  }
  if (ec) {
    std::fprintf(stderr, "error: cannot read directory %s\n", directory);
    return 1;
  }
  std::sort(files.begin(), files.end());
  std::printf("[");
  bool first = true;
  for (const auto& path : files) {
    Stream stream;
    std::string error;
    if (!stream.openFile(path.string(), error)) {
      if (!first) std::printf(",");
      first = false;
      std::printf("{\"file\":\"%s\",\"error\":\"%s\"}",
                  jsonEscape(path.string()).c_str(), jsonEscape(error).c_str());
      continue;
    }
    if (!first) std::printf(",");
    first = false;
    printStreamJson(stream, path.string().c_str(), -1);
  }
  std::printf("]\n");
  return 0;
}

int main(int argc, char** argv) {
  for (int i = 1; i + 1 < argc; i++) {
    if (std::strcmp(argv[i], "--batch") == 0) return runBatchJson(argv[i + 1]);
  }
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: bsinfo <file.h264|file.h265> [--frames] [--tree "
                 "<nal-index>] [--cpb] [--refs] [--json] [--batch <dir>] "
                 "[--csv-cpb <path>] [--bits] [--csv-bits <path>] "
                 "[--overlay] [--conformance]\n");
    return 2;
  }
  Stream s;
  std::string err;
  if (!s.openFile(argv[1], err)) {
    std::fprintf(stderr, "error: %s\n", err.c_str());
    return 1;
  }
  bool showFrames = false;
  bool showCpb = false;
  bool showRefs = false;
  bool showBits = false;
  bool showOverlay = false;
  bool showConformance = false;
  bool json = false;
  long treeIndex = -1;
  const char* csvFrames = nullptr;
  const char* csvNals = nullptr;
  const char* csvCpb = nullptr;
  const char* csvBits = nullptr;
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
    else if (std::strcmp(argv[i], "--json") == 0)
      json = true;
    else if (std::strcmp(argv[i], "--overlay") == 0)
      showOverlay = true;
    else if (std::strcmp(argv[i], "--conformance") == 0)
      showConformance = true;
    else if (std::strcmp(argv[i], "--bits") == 0)
      showBits = true;
    else if (std::strcmp(argv[i], "--csv-bits") == 0 && i + 1 < argc)
      csvBits = argv[++i];
    else if (std::strcmp(argv[i], "--csv-cpb") == 0 && i + 1 < argc)
      csvCpb = argv[++i];
  }

  if (json) {
    printStreamJson(s, argv[1], treeIndex);
    return 0;
  }
  std::printf("%s\n", s.streamSummary().c_str());
  std::printf("codec=%s nals=%zu frames=%zu\n", codecName(s.codec()),
              s.nals().size(), s.frames().size());

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
  if (csvBits) writeCsv(csvBits, s.bitAllocationCsv());

  if (showConformance) {
    const uint64_t bitrate = s.frames().empty()
                                 ? 0
                                 : static_cast<uint64_t>(s.windowBitrate(
                                       0, static_cast<int>(s.frames().size())));
    std::vector<ConformanceIssue> issues;
    if (s.codec() == Codec::H264 && s.haveH264Params())
      issues = checkH264Conformance(s.h264Sps(), s.fps(), bitrate);
    else if (s.codec() == Codec::H265 && s.haveH265Params())
      issues = checkH265Conformance(s.h265Sps(), s.fps(), bitrate);
    std::printf("conformance issues=%zu\n", issues.size());
    for (const auto& issue : issues)
      std::printf("%s: %s\n", issue.code.c_str(), issue.message.c_str());
  }

  if (showOverlay) {
    for (const auto& f : s.frames()) {
      std::printf("overlay %d %c lines=%zu\n", f.index, f.picture_type,
                  s.sliceOverlay(f.index).size());
    }
  }

  if (showBits) {
    for (const auto& f : s.frames()) {
      std::printf(
          "bits %d %c total=%llu ps=%llu sei=%llu hdr=%llu payload=%llu "
          "filler=%llu other=%llu\n",
          f.index, f.picture_type,
          static_cast<unsigned long long>(f.bits.total),
          static_cast<unsigned long long>(f.bits.parameter_sets),
          static_cast<unsigned long long>(f.bits.sei),
          static_cast<unsigned long long>(f.bits.slice_headers),
          static_cast<unsigned long long>(f.bits.slice_payload),
          static_cast<unsigned long long>(f.bits.filler),
          static_cast<unsigned long long>(f.bits.other));
    }
    double rate = s.windowBitrate(0, static_cast<int>(s.frames().size()));
    std::printf("window_bitrate_bps=%.3f frames=%zu\n", rate,
                s.frames().size());
  }

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

  if (treeIndex < 0 && !showFrames && !showCpb && !showRefs && !showBits &&
      !showOverlay && !showConformance) {
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
