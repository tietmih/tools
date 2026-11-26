#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/stream.h"
#include "core/yuv.h"

namespace ui {

struct ImageData {
  std::vector<uint8_t> bgra;
  int width = 0;
  int height = 0;
  int bit_depth = 8;
  int chroma_format = 1;  // 1 = 4:2:0, 2 = 4:2:2, 3 = 4:4:4
  int matrix = 2;
  bool full_range = false;
  bs::YuvLayout layout = bs::YuvLayout::None;
  std::vector<std::vector<uint8_t>> planes;  // Y, Cb, Cr
  std::vector<int> strides;
};

// Displays one decoded frame (BGRA, top-down) with letterboxing and a pixel
// probe over the native YUV planes.
class VideoView {
 public:
  void create(HWND parent, HINSTANCE inst, int id);
  HWND hwnd() const { return hwnd_; }
  void setImage(ImageData image, const std::wstring& note);
  void setOverlay(std::vector<bs::OverlayLine> lines);
  // 0 = colour, 1 = luma, 2 = Cb, 3 = Cr.
  void setComponent(int component);
  int component() const { return component_; }
  void clear(const std::wstring& message = L"");
  void paintNow();
  const ImageData& image() const { return image_; }

 private:
  struct Probe {
    bool valid = false;
    int x = 0;
    int y = 0;
    int y_value = 0;
    int cb = 0;
    int cr = 0;
    int r = 0;
    int g = 0;
    int b = 0;
  };

  static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam);
  LRESULT handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  void paintTo(HDC target, const RECT& rc);
  bool updateProbe(int client_x, int client_y);
  int samplePlane(int plane, int x, int y) const;

  HWND hwnd_ = nullptr;
  ImageData image_;
  RECT image_rect_ = {};
  Probe probe_;
  bool tracking_ = false;
  int component_ = 0;
  std::vector<bs::OverlayLine> overlay_;
  std::wstring note_;
  std::wstring message_;
};

}  // namespace ui
