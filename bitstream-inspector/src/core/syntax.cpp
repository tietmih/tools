#include "core/syntax.h"

#include <cstdio>

namespace bs {

void SyntaxBuilder::open(const std::string& name) {
  stack_.push_back(std::make_unique<SyntaxNode>());
  stack_.back()->name = name;
}

void SyntaxBuilder::close() {
  if (stack_.empty()) return;
  auto node = std::move(stack_.back());
  stack_.pop_back();
  if (stack_.empty())
    roots_.push_back(std::move(*node));
  else
    stack_.back()->children.push_back(std::move(*node));
}

void SyntaxBuilder::add(const std::string& name, const std::string& value,
                        uint64_t bit_pos, uint64_t bit_len) {
  SyntaxNode n;
  n.name = name;
  n.value = value;
  n.bit_pos = bit_pos;
  n.bit_len = bit_len;
  if (stack_.empty())
    roots_.push_back(std::move(n));
  else
    stack_.back()->children.push_back(std::move(n));
}

void SyntaxBuilder::addInt(const std::string& name, int64_t value,
                           uint64_t bit_pos, uint64_t bit_len) {
  add(name, std::to_string(value), bit_pos, bit_len);
}

void SyntaxBuilder::addUInt(const std::string& name, uint64_t value,
                            uint64_t bit_pos, uint64_t bit_len) {
  add(name, std::to_string(value), bit_pos, bit_len);
}

void SyntaxBuilder::addHex(const std::string& name, uint64_t value, int width,
                           uint64_t bit_pos, uint64_t bit_len) {
  add(name, formatHex(value, width), bit_pos, bit_len);
}

void SyntaxBuilder::addError(const std::string& message) {
  SyntaxNode n;
  n.name = "error";
  n.value = message;
  n.is_error = true;
  if (stack_.empty())
    roots_.push_back(std::move(n));
  else
    stack_.back()->children.push_back(std::move(n));
}

void SyntaxBuilder::addRaw(const SyntaxNode& node) {
  if (stack_.empty())
    roots_.push_back(node);
  else
    stack_.back()->children.push_back(node);
}

std::string formatHex(uint64_t value, int width) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "0x%0*llX", width,
                static_cast<unsigned long long>(value));
  return buf;
}

}  // namespace bs
