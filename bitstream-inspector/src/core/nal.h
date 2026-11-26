#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bs {

struct NalUnit {
  size_t offset = 0;  // byte offset of the start code in the file
  int start_code_len = 0;
  size_t payload_off = 0;  // offset of NAL header byte
  size_t size = 0;  // NAL bytes, header included, trailing zero bytes trimmed
  bool end_of_stream_truncated = false;

  // Annex B parsing result
  uint8_t nal_ref_idc = 0;
  uint8_t nal_type = 0;
  bool svc_extension = false;
  bool avc3_syntax = false;

  // Semantic type: H.264 types 1..23 and H.265 0..63 do not overlap in
  // display because type names are resolved per stream codec.
  const char* type_name = "";
};

struct NalSplitter {
  // Finds all Annex B NALs in a buffer. Trailing zero bytes belonging to the
  // next start code are trimmed from each NAL (spec 7.4.1).
  static void split(const uint8_t* data, size_t size, bool hevc,
                    std::vector<NalUnit>& out);
};

const char* h264NalTypeName(int type);
const char* h265NalTypeName(int type);

}  // namespace bs
