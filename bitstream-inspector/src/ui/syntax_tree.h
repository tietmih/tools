#pragma once

#include <windows.h>

#include <commctrl.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/stream.h"

namespace ui {

using bs::Stream;
using bs::SyntaxNode;

constexpr UINT WM_APP_SYNTAX_SELECTED = WM_APP + 12;

class SyntaxTree {
 public:
  struct Selection {
    size_t nal = static_cast<size_t>(-1);
    uint64_t bit_pos = 0;
    uint64_t bit_len = 0;
    bool header = false;
  };

  void create(HWND parent, HINSTANCE inst, int id);
  HWND hwnd() const { return hwnd_; }
  void clear();
  void setFrame(const Stream* stream, int frame_index);
  const Selection& selection() const;
  LRESULT handleNotify(NMHDR* hdr);

 private:
  struct Item {
    size_t nal = static_cast<size_t>(-1);
    uint64_t bit_pos = 0;
    uint64_t bit_len = 0;
    bool is_error = false;
    bool header = false;
  };

  HTREEITEM addNode(HTREEITEM parent, const SyntaxNode& node, size_t nal,
                    int depth);
  LRESULT onCustomDraw(NMTVCUSTOMDRAW* cd);

  HWND hwnd_ = nullptr;
  HWND parent_ = nullptr;
  const Stream* stream_ = nullptr;
  std::vector<Item> items_;
  Selection selection_;
};

}  // namespace ui
