#include "core/fieldreader.h"

namespace bs {

uint32_t FieldReader::u(int n, const char* name) {
  uint64_t p = br_.pos();
  uint32_t v = br_.u(n);
  if (sb_) sb_->addUInt(name, v, bit_base_ + p, br_.pos() - p);
  return v;
}

uint32_t FieldReader::ue(const char* name) {
  uint64_t p = br_.pos();
  uint32_t v = br_.ue();
  if (sb_) sb_->addUInt(name, v, bit_base_ + p, br_.pos() - p);
  return v;
}

int32_t FieldReader::se(const char* name) {
  uint64_t p = br_.pos();
  int32_t v = br_.se();
  if (sb_) sb_->addInt(name, v, bit_base_ + p, br_.pos() - p);
  return v;
}

void FieldReader::skip(int n, const char* name) {
  uint64_t p = br_.pos();
  br_.skip(n);
  if (sb_)
    sb_->add(name, "(" + std::to_string(n) + " bits)", bit_base_ + p,
             br_.pos() - p);
}

void FieldReader::flag(const char* name, bool& out) {
  uint32_t v = u(1, name);
  out = (v != 0);
}

void FieldReader::note(const char* name, const std::string& value) {
  if (sb_) sb_->add(name, value, bit_base_ + br_.pos(), 0);
}

}  // namespace bs
