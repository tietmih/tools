#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ui {

// Displays one decoded frame (BGRA, top-down) with letterboxing. Also used for
// keyframe previews; overlays can be drawn on top later.
class VideoView {
public:
    void create(HWND parent, HINSTANCE inst, int id);
    HWND hwnd() const { return hwnd_; }
    void setImage(std::vector<uint8_t> bgra, int width, int height,
                  const std::wstring &note);
    void clear(const std::wstring &message = L"");
    void paintNow();

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                    LPARAM lparam);
    LRESULT handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void paintTo(HDC target, const RECT &rc);

    HWND hwnd_ = nullptr;
    std::vector<uint8_t> image_;
    int image_w_ = 0;
    int image_h_ = 0;
    std::wstring note_;
    std::wstring message_;
};

} // namespace ui
