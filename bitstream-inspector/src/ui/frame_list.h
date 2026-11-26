#pragma once

#include <windows.h>

#include <commctrl.h>

#include <cstdint>
#include <string>

#include "core/stream.h"

namespace ui {

using bs::Stream;

constexpr UINT WM_APP_FRAME_CHANGED = WM_APP + 10;
constexpr UINT WM_APP_FRAME_FROM_CHART = WM_APP + 11;

class FrameList {
 public:
  void create(HWND parent, HINSTANCE inst, int id);
  HWND hwnd() const { return hwnd_; }
  void setStream(const Stream* stream);
  void refresh();
  void updateHeaders();
  void select(int index, bool ensure_visible);
  int selected() const;
  LRESULT handleNotify(NMHDR* hdr);

 private:
  void onGetDispInfo(NMLVDISPINFOW* di);
  LRESULT onCustomDraw(NMLVCUSTOMDRAW* cd);

  HWND hwnd_ = nullptr;
  HWND parent_ = nullptr;
  const Stream* stream_ = nullptr;
  std::wstring text_;
  int last_selected_ = -1;
};

}  // namespace ui
