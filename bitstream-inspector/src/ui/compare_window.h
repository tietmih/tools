#pragma once

#include <windows.h>

#include <string>

namespace ui {

// A light-weight secondary window that opens two streams side by side and
// synchronises navigation by display order.
class CompareWindow {
 public:
  static void open(HWND owner, HINSTANCE inst, const std::wstring& first,
                   const std::wstring& second);
};

}  // namespace ui
