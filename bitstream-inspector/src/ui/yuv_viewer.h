#pragma once

#include <windows.h>

namespace ui {

// Standalone raw planar YUV viewer (StreamEye Studio YUV Viewer equivalent).
class YuvViewerWindow {
 public:
  static void open(HWND owner, HINSTANCE inst, int default_width,
                   int default_height);
};

}  // namespace ui
