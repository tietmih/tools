#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bs {

// One displayable field or group from a parsed bitstream structure.
// bit_pos/bit_len are relative to the start of the RBSP (NAL header excluded).
struct SyntaxNode {
    std::string name;
    std::string value;
    uint64_t bit_pos = 0;
    uint64_t bit_len = 0;
    bool is_error = false;
    bool nal_header = false; // field lives in the NAL header, not the RBSP
    std::vector<SyntaxNode> children;

    bool is_container() const { return !children.empty(); }
};

// Builds a tree of SyntaxNode while a parser walks the bitstream.
class SyntaxBuilder {
public:
    explicit SyntaxBuilder(std::vector<SyntaxNode> &roots) : roots_(roots) {}

    void open(const std::string &name);
    void close();
    void add(const std::string &name, const std::string &value,
             uint64_t bit_pos, uint64_t bit_len);
    void addInt(const std::string &name, int64_t value,
                uint64_t bit_pos, uint64_t bit_len);
    void addUInt(const std::string &name, uint64_t value,
                 uint64_t bit_pos, uint64_t bit_len);
    void addHex(const std::string &name, uint64_t value, int width,
                uint64_t bit_pos, uint64_t bit_len);
    void addError(const std::string &message);
    void addRaw(const SyntaxNode &node);

    bool empty() const { return stack_.empty(); }
    int depth() const { return static_cast<int>(stack_.size()); }

private:
    std::vector<SyntaxNode> &roots_;
    std::vector<std::unique_ptr<SyntaxNode>> stack_;
};

std::string formatHex(uint64_t value, int width);

} // namespace bs
