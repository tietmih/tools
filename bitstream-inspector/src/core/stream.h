#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/h264.h"
#include "core/h265.h"
#include "core/nal.h"
#include "core/sei.h"
#include "core/syntax.h"

namespace bs {

enum class Codec {
  Unknown,
  H264,
  H265,
};

const char* codecName(Codec c);

struct FrameInfo {
  int index = 0;  // decode order index within the stream
  int poc = 0;
  char picture_type = '?';  // I/P/B (or IDR shown as I)
  int nal_type = 0;         // VCL nal_unit_type of the first slice
  bool idr = false;
  bool irap = false;
  bool ref = false;
  bool field = false;
  int frame_num = 0;
  int temporal_id = 0;
  size_t byte_offset = 0;
  size_t byte_size = 0;
  int nal_count = 0;
  int slice_count = 0;
  uint64_t slice_header_bits = 0;
  int qp = 0;  // first slice's derived QP (26 + pps.pic_init_qp_minus26 +
               // slice_qp_delta)
  // Filled in when the stream came from a container (or a demuxer with timing).
  int64_t pts = 0;
  int64_t dts = 0;
  bool has_timing = false;
  double time_sec = -1.0;
  int packet_index = -1;
};

struct FrameTiming {
  int64_t pts = 0;
  int64_t dts = 0;
  bool has_timing = false;
  double time_sec = -1.0;
  int packet_index = -1;
};

struct NalEntry {
  NalUnit unit;
  std::string summary;  // one-line description for the NAL list
  std::string error;
  SeiInfo sei;
  size_t frame_index =
      static_cast<size_t>(-1);  // index into frames; npos for non-VCL
  bool is_vcl = false;
  // Parameter-set versions active when this NAL was parsed (-1 = unknown).
  int sps_version = -1;
  int pps_version = -1;
};

class Stream {
 public:
  bool openFile(const std::string& path, std::string& error);
  bool loadBuffer(std::vector<uint8_t> data, std::string& error);

  Codec codec() const { return codec_; }
  const std::vector<NalEntry>& nals() const { return nals_; }
  const std::vector<FrameInfo>& frames() const { return frames_; }
  const std::vector<uint8_t>& data() const { return data_; }
  const std::string& path() const { return path_; }

  // Syntax tree for one NAL, built on demand (slice headers need the state
  // that was active at that point in the stream, so it is replayed).
  std::vector<SyntaxNode> syntaxTree(size_t nal_index) const;

  // Parameter set view helpers.
  const H264Sps& h264Sps() const { return h264_.sps; }
  const H264Pps& h264Pps() const { return h264_.pps; }
  const H265Sps& h265Sps() const { return h265_.sps; }
  const H265Pps& h265Pps() const { return h265_.pps; }
  bool haveH264Params() const { return h264_.have_sps || h264_.have_pps; }
  bool haveH265Params() const { return h265_.have_sps || h265_.have_pps; }
  const SeiHdrInfo& seiHdrInfo() const { return hdr_; }
  std::string hdrSummary() const;

  std::string streamSummary() const;
  double fps() const;
  std::string frameTypeName(const FrameInfo& frame) const;
  // Milliseconds relative to the first frame, or -1 when no timing is known.
  double frameTimeMs(const FrameInfo& frame) const;
  void applyFrameTiming(const std::vector<FrameTiming>& timing);

  // CSV exports (UTF-8 text, CRLF line endings, header row included).
  std::string framesCsv() const;
  std::string nalsCsv() const;

  // Payload bytes of one NAL (header included).
  const uint8_t* nalBytes(size_t nal_index, size_t& size) const;
  // Map a bit range inside the RBSP (after the NAL header) to payload bytes.
  bool mapBitRange(size_t nal_index, uint64_t bit_pos, uint64_t bit_len,
                   size_t& payload_start, size_t& payload_end) const;

 private:
  void analyze(std::string& error);
  void analyzeH264(std::string& error);
  void analyzeH265(std::string& error);
  void associateNonVcl();
  bool looksLikeHevc(const uint8_t* data, size_t size) const;

  std::vector<uint8_t> data_;
  std::string path_;
  Codec codec_ = Codec::Unknown;
  std::vector<NalEntry> nals_;
  std::vector<FrameInfo> frames_;

  // Final (last seen) parameter sets, for the info panel.
  H264State h264_;
  H265State h265_;
  // HDR metadata aggregated from every SEI NAL.
  SeiHdrInfo hdr_;
  // Snapshot history so the syntax tree of an early NAL is rebuilt with the
  // parameter sets that were active at that point, not the last ones seen.
  std::vector<H264Sps> h264_sps_versions_;
  std::vector<H264Pps> h264_pps_versions_;
  std::vector<uint64_t> h264_sps_hashes_;
  std::vector<uint64_t> h264_pps_hashes_;
  std::vector<H265Sps> h265_sps_versions_;
  std::vector<H265Pps> h265_pps_versions_;
  std::vector<uint64_t> h265_sps_hashes_;
  std::vector<uint64_t> h265_pps_hashes_;
  int h264_cur_sps_ = -1;
  int h264_cur_pps_ = -1;
  int h265_cur_sps_ = -1;
  int h265_cur_pps_ = -1;
  // Locations of the last SPS/PPS NALs, used when replaying slice state.
  size_t h264_sps_idx_ = SIZE_MAX;
  size_t h264_pps_idx_ = SIZE_MAX;
  size_t h265_sps_idx_ = SIZE_MAX;
  size_t h265_pps_idx_ = SIZE_MAX;
};

}  // namespace bs
