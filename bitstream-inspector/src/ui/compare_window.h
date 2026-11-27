#pragma once

#include <windows.h>

#include <string>

namespace ui {

// A light-weight secondary window that opens two to four streams in a grid
// and synchronises navigation by display order, POC or time. Difference view
// modes apply to the first two streams.
class CompareWindow {
 public:
  static void open(HWND owner, HINSTANCE inst, const std::wstring& first,
                   const std::wstring& second);
};

}  // namespace ui
