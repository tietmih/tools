#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/stream.h"

namespace ui {

constexpr UINT WM_APP_THUMBNAIL_SELECTED = WM_APP + 21;

class ThumbnailStrip {
 public:
  struct Thumb {
    std::vector<uint8_t> bgra;
    int width = 0;
    int height = 0;
  };

  void create(HWND parent, HINSTANCE inst, int id);
  HWND hwnd() const { return hwnd_; }
  void setStream(const bs::Stream* stream);
  void setSelected(int frame_index);
  void setThumbnail(int frame_index, Thumb thumb);
  void clearThumbnails();
  void setRequestHandler(std::function<void(int)> handler);
  void invalidate();

 private:
  static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam);
  LRESULT handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  void paintTo(HDC dc, const RECT& rc);
  void updateScroll();
  int slotWidth() const;
  int slotCount() const;
  int frameForSlot(int slot) const;
  int slotForFrame(int frame) const;

  HWND hwnd_ = nullptr;
  HWND parent_ = nullptr;
  const bs::Stream* stream_ = nullptr;
  int selected_ = -1;
  int scroll_x_ = 0;
  int step_ = 1;
  std::unordered_map<int, Thumb> thumbs_;
  std::unordered_map<int, bool> requested_;
  std::function<void(int)> request_;
};

}  // namespace ui
