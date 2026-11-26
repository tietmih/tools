#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/quality.h"
#include "core/yuv.h"

namespace bs {

// Reads a planar raw YUV reference file (ffmpeg rawvideo layout). Sample
// values are stored as-is: 8-bit for I420/I422/I444, little-endian 16-bit
// low-aligned for P010/P210.
class ReferenceYuv {
 public:
  bool open(const std::string& path, YuvLayout layout, int width, int height,
            std::string& error);
  void close();
  bool readFrame(int index, std::vector<uint8_t>& storage, YuvFrameView& view,
                 std::string& error) const;
  int frameCount() const { return frame_count_; }
  int width() const { return width_; }
  int height() const { return height_; }
  int bitDepth() const { return bit_depth_; }
  int chromaFormat() const { return chroma_format_; }
  YuvLayout layout() const { return layout_; }
  bool isOpen() const { return valid_; }

 private:
  size_t planeWidth(int plane) const;
  size_t planeHeight(int plane) const;
  size_t frameSize() const;

  std::string path_;
  YuvLayout layout_ = YuvLayout::None;
  int width_ = 0;
  int height_ = 0;
  int bit_depth_ = 8;
  int chroma_format_ = 1;
  int frame_count_ = 0;
  bool valid_ = false;
};

}  // namespace bs
