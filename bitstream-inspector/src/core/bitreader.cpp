#include "core/bitreader.h"

namespace bs {

uint32_t BitReader::read_bit() {
  if (fail_ || pos_ >= size_ * 8) {
    fail_ = true;
    return 0;
  }
  uint32_t v = (data_[pos_ >> 3] >> (7 - (pos_ & 7))) & 1u;
  pos_++;
  return v;
}

bool BitReader::get_bit(uint64_t bit) const {
  if (bit >= size_ * 8) return false;
  return ((data_[bit >> 3] >> (7 - (bit & 7))) & 1u) != 0;
}

uint32_t BitReader::u(int n) {
  if (n <= 0) return 0;
  if (n > 32) {
    fail_ = true;
    return 0;
  }
  uint32_t v = 0;
  for (int i = 0; i < n; i++) v = (v << 1) | read_bit();
  return v;
}

uint32_t BitReader::ue() {
  int zeros = 0;
  while (!fail_ && zeros < 32 && read_bit() == 0) zeros++;
  if (fail_) return 0;
  if (zeros >= 32) {
    fail_ = true;
    return 0;
  }
  uint32_t suffix = zeros ? u(zeros) : 0;
  if (fail_) return 0;
  return ((1u << zeros) - 1u) + suffix;
}

int32_t BitReader::se() {
  uint32_t k = ue();
  if (fail_) return 0;
  if (k & 1u) return static_cast<int32_t>((k + 1) / 2);
  return -static_cast<int32_t>(k / 2);
}

void BitReader::skip(uint32_t n) {
  for (uint32_t i = 0; i < n && !fail_; i++) read_bit();
}

bool BitReader::byte_align() {
  if (fail_) return false;
  uint64_t rem = pos_ & 7;
  if (rem) {
    uint32_t stop = read_bit();
    if (fail_) return false;
    if (stop != 1) {
      fail_ = true;
      return false;
    }
    while (pos_ & 7) {
      if (read_bit() != 0) {
        fail_ = true;
        return false;
      }
    }
  }
  return !fail_;
}

bool BitReader::more_rbsp_data() const {
  if (pos_ >= size_ * 8) return false;
  // Locate the last set bit; everything after it is zero padding.
  uint64_t last = size_ * 8;
  while (last > pos_ && !get_bit(last - 1)) last--;
  return last > pos_ + 1;
}

Rbsp unescapeRbsp(const uint8_t* src, size_t size) {
  Rbsp r;
  r.data.reserve(size);
  r.map.reserve(size);
  int zeros = 0;
  for (size_t i = 0; i < size; i++) {
    if (zeros >= 2 && src[i] == 0x03) {
      zeros = 0;
      continue;
    }
    r.data.push_back(src[i]);
    r.map.push_back(static_cast<uint32_t>(i));
    zeros = (src[i] == 0) ? zeros + 1 : 0;
  }
  return r;
}

}  // namespace bs
