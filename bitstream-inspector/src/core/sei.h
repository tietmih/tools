#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/bitreader.h"
#include "core/syntax.h"

namespace bs {

struct SeiMasteringDisplay {
  bool present = false;
  uint16_t primaries_x[3] = {};
  uint16_t primaries_y[3] = {};
  uint16_t white_point_x = 0;
  uint16_t white_point_y = 0;
  uint32_t max_luminance = 0;  // 0.0001 cd/m^2 units
  uint32_t min_luminance = 0;  // 0.0001 cd/m^2 units
};

struct SeiContentLightLevel {
  bool present = false;
  uint16_t max_cll = 0;
  uint16_t max_fall = 0;
};

struct SeiAlternativeTransfer {
  bool present = false;
  uint8_t preferred_transfer_characteristics = 0;
};

struct SeiHdrInfo {
  SeiMasteringDisplay mastering_display;
  SeiContentLightLevel content_light_level;
  SeiAlternativeTransfer alternative_transfer;
  bool hdr10_plus = false;
  bool dolby_vision = false;
};

struct SeiMessage {
  uint32_t payload_type = 0;
  uint32_t payload_size = 0;
  uint64_t bit_pos = 0;  // payload start, RBSP-relative
  std::string name;
};

struct SeiInfo {
  bool parsed = false;
  std::vector<SeiMessage> messages;
  SeiHdrInfo hdr;
};

struct ColourDescription {
  bool present = false;
  int primaries = 2;
  int transfer = 2;
  int matrix = 2;
};

// Parses the SEI messages in an RBSP that starts at the first payload type
// byte. `sb` may be null for lightweight scans. H.264 and H.265 share the
// message framing but not the payload type namespace.
void parseSei(const Rbsp& rbsp, SyntaxBuilder* sb, bool hevc, SeiInfo& out);

const char* seiPayloadName(uint32_t type, bool hevc);
std::string seiSummary(const SeiInfo& info);

// Merges HDR values from one SEI NAL into the stream-level summary. Later
// values win, matching how decoders refresh metadata.
void mergeSeiHdr(SeiHdrInfo& dst, const SeiHdrInfo& src);

// Human-readable HDR label, empty when the stream does not signal HDR.
std::string formatHdrSummary(const SeiHdrInfo& sei,
                             const ColourDescription& colour);

}  // namespace bs
