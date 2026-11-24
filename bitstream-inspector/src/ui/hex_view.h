#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/stream.h"

namespace ui {

using bs::Stream;

// Hex dump of one NAL payload. Highlighted bytes come from the syntax field
// selected in the tree; clicking a byte selects it for copying.
class HexView {
public:
    void create(HWND parent, HINSTANCE inst, int id);
    HWND hwnd() const { return hwnd_; }
    void setNal(const Stream *stream, size_t nal_index);
    void highlight(size_t nal_index, uint64_t bit_pos, uint64_t bit_len);
    void highlightBytes(size_t start, size_t end);
    void clear();

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                    LPARAM lparam);
    LRESULT handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void paintTo(HDC target, const RECT &rc);
    void updateScroll(HWND hwnd);
    void revealHighlight();
    int metrics(HDC dc, int client_width, int &char_w, int &line_h,
                int &bytes_per_line) const;
    int hitTest(int x, int y, int char_w, int line_h, int bytes_per_line) const;
    void copyToClipboard(HWND hwnd);

    HWND hwnd_ = nullptr;
    const Stream *stream_ = nullptr;
    size_t nal_index_ = static_cast<size_t>(-1);
    const uint8_t *data_ = nullptr;
    size_t bytes_ = 0;
    bool has_highlight_ = false;
    size_t highlight_start_ = 0;
    size_t highlight_end_ = 0;
    int selected_byte_ = -1;
    int scroll_line_ = 0;
    int wheel_remainder_ = 0;
};

} // namespace ui
