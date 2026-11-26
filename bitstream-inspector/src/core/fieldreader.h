#pragma once

#include <string>

#include "core/bitreader.h"
#include "core/syntax.h"

namespace bs {

// Wraps BitReader and mirrors every read into a SyntaxBuilder with the exact
// bit range, so the UI can highlight the corresponding bytes.
class FieldReader {
 public:
  FieldReader(BitReader& br, SyntaxBuilder* sb, uint64_t bit_base = 0)
      : br_(br), sb_(sb), bit_base_(bit_base) {}

  uint32_t u(int n, const char* name);
  uint32_t ue(const char* name);
  int32_t se(const char* name);
  void skip(int n, const char* name);
  void flag(const char* name, bool& out);
  void note(const char* name, const std::string& value);

  uint32_t uu(int n) { return br_.u(n); }
  uint32_t ueu() { return br_.ue(); }
  int32_t seu() { return br_.se(); }

  BitReader& br() { return br_; }
  SyntaxBuilder* sb() { return sb_; }
  bool fail() const { return br_.fail(); }
  void error(const std::string& msg) {
    if (sb_) sb_->addError(msg);
  }

 private:
  BitReader& br_;
  SyntaxBuilder* sb_;
  uint64_t bit_base_ = 0;
};

// Opens a named group in the tree and guarantees a matching close. Passing a
// null/empty name makes it a no-op, useful when the caller already opened the
// group.
struct Section {
  SyntaxBuilder* sb;
  bool opened = false;
  Section(SyntaxBuilder* s, const char* name) : sb(s) {
    if (sb && name && *name) {
      sb->open(name);
      opened = true;
    }
  }
  ~Section() {
    if (opened) sb->close();
  }
  Section(const Section&) = delete;
  Section& operator=(const Section&) = delete;
};

}  // namespace bs
