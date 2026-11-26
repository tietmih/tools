#include "core/reference_yuv.h"

#include <cstdio>

namespace bs {

namespace {

bool layoutInfo(YuvLayout layout, int& bit_depth, int& chroma_format) {
  switch (layout) {
    case YuvLayout::I420:
      bit_depth = 8;
      chroma_format = 1;
      return true;
    case YuvLayout::I422:
      bit_depth = 8;
      chroma_format = 2;
      return true;
    case YuvLayout::I444:
      bit_depth = 8;
      chroma_format = 3;
      return true;
    case YuvLayout::P010:
      bit_depth = 10;
      chroma_format = 1;
      return true;
    case YuvLayout::P210:
      bit_depth = 10;
      chroma_format = 2;
      return true;
    case YuvLayout::P012:
      bit_depth = 12;
      chroma_format = 1;
      return true;
    case YuvLayout::P212:
      bit_depth = 12;
      chroma_format = 2;
      return true;
    default:
      return false;
  }
}

}  // namespace

size_t ReferenceYuv::planeWidth(int plane) const {
  if (plane == 0 || chroma_format_ == 3) return static_cast<size_t>(width_);
  return static_cast<size_t>((width_ + 1) / 2);
}

size_t ReferenceYuv::planeHeight(int plane) const {
  if (plane == 0 || chroma_format_ != 1) return static_cast<size_t>(height_);
  return static_cast<size_t>((height_ + 1) / 2);
}

size_t ReferenceYuv::frameSize() const {
  const size_t bytes = bit_depth_ > 8 ? 2 : 1;
  size_t total = 0;
  for (int p = 0; p < 3; p++) total += planeWidth(p) * planeHeight(p) * bytes;
  return total;
}

bool ReferenceYuv::open(const std::string& path, YuvLayout layout, int width,
                        int height, std::string& error) {
  close();
  if (width <= 0 || height <= 0) {
    error = "invalid reference dimensions";
    return false;
  }
  if (!layoutInfo(layout, bit_depth_, chroma_format_)) {
    error = "unsupported reference pixel format";
    return false;
  }
  FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) {
    error = "cannot open the reference YUV file";
    return false;
  }
  std::fseek(file, 0, SEEK_END);
  const long size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);
  std::fclose(file);
  if (size <= 0) {
    error = "reference YUV file is empty";
    return false;
  }
  path_ = path;
  layout_ = layout;
  width_ = width;
  height_ = height;
  const size_t frame_size = frameSize();
  if (frame_size == 0) {
    error = "invalid reference frame size";
    return false;
  }
  frame_count_ = static_cast<int>(static_cast<size_t>(size) / frame_size);
  if (frame_count_ == 0) {
    error = "reference YUV is shorter than one frame";
    return false;
  }
  valid_ = true;
  return true;
}

void ReferenceYuv::close() {
  path_.clear();
  layout_ = YuvLayout::None;
  width_ = 0;
  height_ = 0;
  bit_depth_ = 8;
  chroma_format_ = 1;
  frame_count_ = 0;
  valid_ = false;
}

bool ReferenceYuv::readFrame(int index, std::vector<uint8_t>& storage,
                             YuvFrameView& view, std::string& error) const {
  if (!valid_) {
    error = "no reference YUV is loaded";
    return false;
  }
  if (index < 0 || index >= frame_count_) {
    error = "reference frame index out of range";
    return false;
  }
  FILE* file = std::fopen(path_.c_str(), "rb");
  if (!file) {
    error = "cannot reopen the reference YUV file";
    return false;
  }
  const size_t frame_size = frameSize();
  if (std::fseek(file, static_cast<long>(frame_size) * index, SEEK_SET) != 0) {
    std::fclose(file);
    error = "cannot seek in the reference YUV file";
    return false;
  }
  storage.resize(frame_size);
  const size_t got = std::fread(storage.data(), 1, frame_size, file);
  std::fclose(file);
  if (got != frame_size) {
    error = "short read from the reference YUV file";
    return false;
  }
  const size_t bytes = bit_depth_ > 8 ? 2 : 1;
  size_t offset = 0;
  for (int p = 0; p < 3; p++) {
    const int pw = static_cast<int>(planeWidth(p));
    const int ph = static_cast<int>(planeHeight(p));
    view.planes[p] = {storage.data() + offset, pw * static_cast<int>(bytes), pw,
                      ph};
    offset += static_cast<size_t>(pw) * ph * bytes;
  }
  view.bit_depth = bit_depth_;
  view.chroma_format = chroma_format_;
  return true;
}

}  // namespace bs
