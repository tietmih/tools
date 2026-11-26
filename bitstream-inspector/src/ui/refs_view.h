#pragma once

#include <windows.h>

#include "core/stream.h"

namespace ui {

constexpr UINT WM_APP_REF_SELECTED = WM_APP + 23;

// Lists L0/L1 reference POCs for the selected frame. Double-clicking a row
// jumps to the referenced frame.
class RefsView {
 public:
  void create(HWND parent, HINSTANCE inst, int id);
  HWND hwnd() const { return hwnd_; }
  void setFrame(const bs::Stream* stream, int frame_index);
  void clear();
  void updateHeaders();

 private:
  static LRESULT CALLBACK subclassProc(HWND hwnd, UINT msg, WPARAM wparam,
                                       LPARAM lparam, UINT_PTR id,
                                       DWORD_PTR ref_data);
  LRESULT handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  void addRow(const wchar_t* list, int poc, int frame, bool missing);

  HWND hwnd_ = nullptr;
  HWND parent_ = nullptr;
};

}  // namespace ui
