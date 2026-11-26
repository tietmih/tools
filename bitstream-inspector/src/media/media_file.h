#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/stream.h"

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
  // Decode one packet index directly (timestamp matching).
  bool decodePacket(int packet_index, std::vector<uint8_t>& bgra, int& width,
                    int& height, std::string& error);

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

}  // namespace bs
