#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/stream.h"
#include "core/yuv.h"

namespace bs {

struct ContainerInfo {
  std::string format;
  std::string codec;
  int width = 0;
  int height = 0;
  double fps = 0.0;
  int64_t duration_ms = 0;
  int64_t bit_rate = 0;
  bool raw_es = false;
  std::string error;
};

struct MediaPacket {
  int64_t pts = 0;
  int64_t dts = 0;
  bool has_pts = false;
  bool has_dts = false;
  bool keyframe = false;
  int64_t pos = -1;
  size_t es_offset = 0;
  size_t es_size = 0;
};

struct DecodedFrame {
  int width = 0;
  int height = 0;
  int chroma_format = 1;  // 1 = 4:2:0, 2 = 4:2:2, 3 = 4:4:4
  int bit_depth = 8;
  bool full_range = false;
  int matrix = 2;  // VUI matrix_coefficients
  YuvLayout layout = YuvLayout::None;
  std::vector<uint8_t> bgra;
  std::vector<std::vector<uint8_t>> planes;  // Y, Cb, Cr
  std::vector<int> strides;
};

// Demuxes a media file and converts the selected H.264/HEVC stream to Annex B.
// Requires the FFmpeg libraries; the rest of the application stays unaware of
// FFmpeg types.
class MediaFile {
 public:
  MediaFile();
  ~MediaFile();
  MediaFile(const MediaFile&) = delete;
  MediaFile& operator=(const MediaFile&) = delete;

  bool open(const std::string& path, std::string& error);
  const ContainerInfo& info() const;
  const std::vector<MediaPacket>& packets() const;
  // Matrix/range used for the BGRA preview only; native YUV is unchanged.
  void setPreviewColour(int matrix, bool full_range);
  void clearPreviewColour();

  // Demux the video stream and convert it to Annex B (with start codes).
  bool extractAnnexB(std::vector<uint8_t>& es, std::string& error);
  // Fill frame PTS/DTS/time/packet index. Call after Stream::loadBuffer.
  void applyTiming(Stream& stream) const;

  // Decode the frame belonging to frame_index. Containers with timestamps
  // decode the exact frame; raw streams without timestamps fall back to the
  // nearest keyframe. Returns BGRA, top-down, width*4 stride.
  bool decodeFrameAt(const Stream& stream, int frame_index,
                     std::vector<uint8_t>& bgra, int& width, int& height,
                     bool& keyframe_only, std::string& error);
  // Decode one frame with both BGRA and native YUV planes.
  bool decodeFrameAt(const Stream& stream, int frame_index, DecodedFrame& frame,
                     bool& keyframe_only, std::string& error);
  bool decodeThumbnailAt(const Stream& stream, int frame_index, int width,
                         int height, std::vector<uint8_t>& bgra,
                         std::string& error);
  // Decode one packet index directly (timestamp matching).
  bool decodePacket(int packet_index, std::vector<uint8_t>& bgra, int& width,
                    int& height, std::string& error);
  // Decode one packet index directly into BGRA and native YUV planes.
  bool decodePacketFrame(int packet_index, DecodedFrame& frame,
                         std::string& error);

 private:
  bool decodeRawFrame(const Stream& stream, int frame_index, DecodedFrame& out,
                      bool& keyframe_only, std::string& error);
  struct Impl;
  Impl* impl_ = nullptr;
};

}  // namespace bs
