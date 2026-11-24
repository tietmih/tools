#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bs {

// Bit reader over an RBSP (emulation-prevention bytes already removed).
// All reads are bounds-checked; on overrun the reader latches a failure and
// returns 0 from then on, so parsers can check fail() at section boundaries.
class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    uint32_t u(int n);
    uint32_t ue();
    int32_t se();
    void skip(uint32_t n);

    uint64_t pos() const { return pos_; }
    uint64_t bits_left() const
    {
        return pos_ < size_ * 8 ? size_ * 8 - pos_ : 0;
    }
    bool byte_align();
    bool fail() const { return fail_; }
    void set_fail() { fail_ = true; }

    // True when there is more than the rbsp_stop_one_bit + zero padding left.
    bool more_rbsp_data() const;

private:
    uint32_t read_bit();
    bool get_bit(uint64_t bit) const;

    const uint8_t *data_;
    size_t size_;
    uint64_t pos_ = 0;
    bool fail_ = false;
};

struct Rbsp {
    std::vector<uint8_t> data;
    // map[i] = byte index in the original NAL payload (header included by caller)
    std::vector<uint32_t> map;
};

// Remove 00 00 03 emulation-prevention bytes from [src, src+size).
Rbsp unescapeRbsp(const uint8_t *src, size_t size);

} // namespace bs
