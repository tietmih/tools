#pragma once

#include <windows.h>

#include "core/stream.h"

namespace ui {

using bs::Stream;

// Frame-size bar chart with a picture-type strip. Clicking selects a frame.
class ChartView {
public:
    void create(HWND parent, HINSTANCE inst, int id);
    HWND hwnd() const { return hwnd_; }
    void setStream(const Stream *stream);
    void setSelected(int frame_index);
    void invalidate();

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                    LPARAM lparam);
    LRESULT handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void paintTo(HDC target, const RECT &rc);
    void onClick(HWND hwnd, int x, int y);

    HWND hwnd_ = nullptr;
    HWND parent_ = nullptr;
    const Stream *stream_ = nullptr;
    int selected_ = -1;
};

} // namespace ui
