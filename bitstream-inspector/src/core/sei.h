#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/bitreader.h"
#include "core/hrd.h"
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

struct SeiBufferingPeriod {
  bool present = false;
  int sps_id = -1;
  bool irap_cpb_params_present = false;
  uint32_t cpb_delay_offset = 0;
  uint32_t dpb_delay_offset = 0;
  bool concatenation_flag = false;
  uint32_t au_cpb_removal_delay_delta_minus1 = 0;
  std::vector<uint32_t> nal_initial_cpb_removal_delay;
  std::vector<uint32_t> nal_initial_cpb_removal_offset;
  std::vector<uint32_t> vcl_initial_cpb_removal_delay;
  std::vector<uint32_t> vcl_initial_cpb_removal_offset;
  bool use_alt_cpb_params = false;
};

struct SeiPicTiming {
  bool present = false;
  uint32_t cpb_removal_delay = 0;            // H.264
  uint32_t dpb_output_delay = 0;             // H.264
  uint32_t au_cpb_removal_delay_minus1 = 0;  // H.265
  uint32_t pic_dpb_output_delay = 0;         // H.265
  uint32_t pic_struct = 0;
  bool has_pic_struct = false;
  bool has_delays = false;
};

struct SeiHrdContext {
  bool hevc = false;
  bool has_sps = false;
  bool nal_hrd = false;
  bool vcl_hrd = false;
  int nal_cpb_cnt_minus1 = 0;
  int vcl_cpb_cnt_minus1 = 0;
  int initial_cpb_removal_delay_length = 0;  // bits
  int cpb_removal_delay_length = 0;          // H.264 bits
  int dpb_output_delay_length = 0;           // bits
  int time_offset_length = 0;                // bits
  bool pic_struct_present = false;
  bool low_delay_hrd = false;
  const H265Hrd* h265_hrd = nullptr;
  int highest_tid = 0;
  bool frame_field_info_present = false;
};

struct SeiInfo {
  bool parsed = false;
  std::vector<SeiMessage> messages;
  SeiHdrInfo hdr;
  SeiBufferingPeriod buffering_period;
  SeiPicTiming pic_timing;
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
void parseSei(const Rbsp& rbsp, SyntaxBuilder* sb, bool hevc, SeiInfo& out,
              const SeiHrdContext* ctx = nullptr);

const char* seiPayloadName(uint32_t type, bool hevc);
std::string seiSummary(const SeiInfo& info);

// Merges HDR values from one SEI NAL into the stream-level summary. Later
// values win, matching how decoders refresh metadata.
void mergeSeiHdr(SeiHdrInfo& dst, const SeiHdrInfo& src);

// Human-readable HDR label, empty when the stream does not signal HDR.
std::string formatHdrSummary(const SeiHdrInfo& sei,
                             const ColourDescription& colour);

}  // namespace bs
