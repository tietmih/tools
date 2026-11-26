#include "core/nal.h"

#include <vector>

namespace bs {

const char* h264NalTypeName(int type) {
  static const char* names[] = {
      "Unspecified",
      "Slice (non-IDR)",
      "Slice Data Partition A",
      "Slice Data Partition B",
      "Slice Data Partition C",
      "Slice (IDR)",
      "SEI (Supplemental Enhancement)",
      "SPS (Sequence Parameter Set)",
      "PPS (Picture Parameter Set)",
      "AUD (Access Unit Delimiter)",
      "End of Sequence",
      "End of Stream",
      "Filler Data",
      "SPS Extension",
      "Prefix NAL",
      "Subset SPS",
      "DPS (Depth Parameter Set)",
      "Reserved",
      "Reserved",
      "Reserved",
      "Slice Extension",
      "Slice Extension (Depth)",
      "Reserved",
      "Reserved",
  };
  if (type >= 0 && type < static_cast<int>(sizeof(names) / sizeof(names[0])))
    return names[type];
  return "Reserved/Unspecified";
}

const char* h265NalTypeName(int type) {
  static const char* names[] = {
      "TRAIL_N",
      "TRAIL_R",
      "TSA_N",
      "TSA_R",
      "STSA_N",
      "STSA_R",
      "RADL_N",
      "RADL_R",
      "RASL_N",
      "RASL_R",
      "Reserved (VCL)",
      "Reserved (VCL)",
      "Reserved (VCL)",
      "Reserved (VCL)",
      "Reserved (VCL)",
      "Reserved (VCL)",
      "BLA_W_LP",
      "BLA_W_RADL",
      "BLA_N_LP",
      "IDR_W_RADL",
      "IDR_N_LP",
      "CRA_NUT",
      "Reserved (IRAP)",
      "Reserved (IRAP)",
      "Reserved (IRAP)",
      "Reserved (VCL)",
      "Reserved (VCL)",
      "Reserved (VCL)",
      "Reserved (VCL)",
      "Reserved (VCL)",
      "Reserved (VCL)",
      "Reserved (VCL)",
      "VPS (Video Parameter Set)",
      "SPS (Sequence Parameter Set)",
      "PPS (Picture Parameter Set)",
      "AUD (Access Unit Delimiter)",
      "EOS (End of Sequence)",
      "EOB (End of Bitstream)",
      "FD (Filler Data)",
      "PREFIX_SEI",
      "SUFFIX_SEI",
      "Reserved (Non-VCL)",
      "Reserved (Non-VCL)",
      "Reserved (Non-VCL)",
      "Reserved (Non-VCL)",
      "Reserved (Non-VCL)",
      "Reserved (Non-VCL)",
      "AP (Aggregation Packet)",
      "FU (Fragmentation Unit)",
  };
  if (type >= 0 && type < static_cast<int>(sizeof(names) / sizeof(names[0])))
    return names[type];
  if (type >= 0 && type <= 63) return "Reserved (Non-VCL)";
  return "Invalid";
}

void NalSplitter::split(const uint8_t* data, size_t size, bool hevc,
                        std::vector<NalUnit>& out) {
  out.clear();
  if (!data || size < 4) return;

  // Find all start code positions: 00 00 01 or 00 00 00 01.
  struct Start {
    size_t pos;
    int len;
  };
  std::vector<Start> starts;
  size_t i = 0;
  while (i + 2 < size) {
    if (data[i] == 0 && data[i + 1] == 0) {
      if (data[i + 2] == 1) {
        starts.push_back({i, 3});
        i += 3;
        continue;
      }
      if (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1) {
        starts.push_back({i, 4});
        i += 4;
        continue;
      }
    }
    i++;
  }
  if (starts.empty()) return;

  for (size_t s = 0; s < starts.size(); s++) {
    NalUnit nal;
    nal.offset = starts[s].pos;
    nal.start_code_len = starts[s].len;
    nal.payload_off = starts[s].pos + starts[s].len;
    size_t end = (s + 1 < starts.size()) ? starts[s + 1].pos : size;
    // Trim trailing zero bytes; they belong to the following start code.
    while (end > nal.payload_off && data[end - 1] == 0) end--;
    if (end <= nal.payload_off) continue;
    nal.size = end - nal.payload_off;
    nal.end_of_stream_truncated = (s + 1 == starts.size());

    const uint8_t* p = data + nal.payload_off;
    if (hevc) {
      if (nal.size < 2) continue;
      nal.nal_type = (p[0] >> 1) & 0x3F;
      nal.nal_ref_idc = p[1] & 0x0F;  // nuh_layer_id low bits / temporal id
    } else {
      nal.nal_ref_idc = (p[0] >> 5) & 0x03;
      nal.nal_type = p[0] & 0x1F;
      if (nal.nal_type == 14 || nal.nal_type == 20)
        nal.svc_extension = nal.size >= 4 && (p[1] & 0x80);
    }
    nal.type_name =
        hevc ? h265NalTypeName(nal.nal_type) : h264NalTypeName(nal.nal_type);
    out.push_back(nal);
  }
}

}  // namespace bs
