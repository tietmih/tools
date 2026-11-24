#include "ui/hex_view.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "ui/lang.h"
#include "ui/ui_util.h"

namespace ui {

using namespace bs;

namespace {

const wchar_t kClassName[] = L"BsHexView";

const int kMaxBytes = 4 * 1024 * 1024;

} // namespace

void HexView::create(HWND parent, HINSTANCE inst, int id)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &HexView::wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, kClassName, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        inst, this);
}

LRESULT CALLBACK HexView::wndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam)
{
    if (msg == WM_NCCREATE) {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    auto *self =
        reinterpret_cast<HexView *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self)
        return self->handle(hwnd, msg, wparam, lparam);
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void HexView::clear()
{
    stream_ = nullptr;
    nal_index_ = static_cast<size_t>(-1);
    data_ = nullptr;
    bytes_ = 0;
    has_highlight_ = false;
    selected_byte_ = -1;
    scroll_line_ = 0;
    if (hwnd_) {
        updateScroll(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void HexView::setNal(const Stream *stream, size_t nal_index)
{
    stream_ = stream;
    nal_index_ = nal_index;
    has_highlight_ = false;
    selected_byte_ = -1;
    scroll_line_ = 0;
    size_t size = 0;
    data_ = stream ? stream->nalBytes(nal_index, size) : nullptr;
    bytes_ = std::min(size, static_cast<size_t>(kMaxBytes));
    if (hwnd_) {
        updateScroll(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void HexView::revealHighlight()
{
    if (!hwnd_ || !has_highlight_)
        return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int char_w = 0, line_h = 0, bpl = 16;
    HDC dc = GetDC(hwnd_);
    if (dc) {
        metrics(dc, rc.right, char_w, line_h, bpl);
        ReleaseDC(hwnd_, dc);
    }
    int visible = std::max(1, (static_cast<int>(rc.bottom) - px(12)) /
                                  std::max(1, line_h));
    int row = static_cast<int>(highlight_start_) / std::max(1, bpl);
    if (row < scroll_line_)
        scroll_line_ = row;
    else if (row >= scroll_line_ + visible)
        scroll_line_ = std::max(0, row - visible / 2);
    updateScroll(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void HexView::highlight(size_t nal_index, uint64_t bit_pos, uint64_t bit_len)
{
    has_highlight_ = false;
    if (!stream_ || nal_index != nal_index_ || bit_len == 0 || !data_)
        return;
    size_t start = 0, end = 0;
    if (!stream_->mapBitRange(nal_index, bit_pos, bit_len, start, end))
        return;
    highlight_start_ = std::min(start, bytes_);
    highlight_end_ = std::min(end, bytes_);
    if (highlight_start_ >= highlight_end_)
        return;
    has_highlight_ = true;
    revealHighlight();
}

void HexView::highlightBytes(size_t start, size_t end)
{
    if (!data_ || bytes_ == 0)
        return;
    highlight_start_ = std::min(start, bytes_);
    highlight_end_ = std::min(end, bytes_);
    has_highlight_ = highlight_start_ < highlight_end_;
    if (has_highlight_)
        revealHighlight();
    else
        InvalidateRect(hwnd_, nullptr, FALSE);
}

int HexView::metrics(HDC dc, int client_width, int &char_w, int &line_h,
                     int &bytes_per_line) const
{
    HGDIOBJ old = SelectObject(dc, monoFont());
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    SIZE sz = {};
    GetTextExtentPoint32W(dc, L"00 ", 3, &sz);
    SelectObject(dc, old);

    char_w = std::max(1, static_cast<int>(sz.cx));
    line_h = tm.tmHeight + px(4);

    int margin = px(6);
    int available = (client_width > 0 ? client_width : 800) - 2 * margin;
    bytes_per_line = 4;
    const int candidates[] = {16, 8, 4};
    for (int candidate : candidates) {
        int chars = 11 + candidate * 4; // offset + hex + gap + ascii
        if (chars * char_w <= available) {
            bytes_per_line = candidate;
            break;
        }
    }
    return margin;
}

void HexView::updateScroll(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC dc = GetDC(hwnd);
    int char_w = 1, line_h = 20, bpl = 16;
    metrics(dc, rc.right, char_w, line_h, bpl);
    ReleaseDC(hwnd, dc);
    int lines = (static_cast<int>(bytes_) + bpl - 1) / bpl;
    int visible = std::max(1, (static_cast<int>(rc.bottom) - px(12)) / std::max(1, line_h));
    int max_scroll = std::max(0, lines - visible);
    scroll_line_ = std::min(std::max(0, scroll_line_), max_scroll);

    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = std::max(0, lines - 1);
    si.nPage = static_cast<UINT>(visible);
    si.nPos = scroll_line_;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    ShowScrollBar(hwnd, SB_VERT, lines > visible);
}

void HexView::paintTo(HDC target, const RECT &rc)
{
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, std::max(1, (int)rc.right),
                                            std::max(1, (int)rc.bottom));
    HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
    FillRect(dc, &rc, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    SetBkMode(dc, TRANSPARENT);

    int char_w = 1, line_h = 20, bpl = 16;
    int margin = metrics(dc, rc.right, char_w, line_h, bpl);
    SelectObject(dc, monoFont());

    if (bytes_ == 0 || !data_) {
        SelectObject(dc, uiFont());
        SetTextColor(dc, RGB(120, 120, 120));
        RECT text_rc = rc;
        DrawTextW(dc, tr(L"No NAL selected", L"未选择 NAL"), -1, &text_rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
        return;
    }

    int lines = (static_cast<int>(bytes_) + bpl - 1) / bpl;
    int visible = std::max(1, (static_cast<int>(rc.bottom) - 2 * margin) / std::max(1, line_h));
    int first = std::min(scroll_line_, std::max(0, lines - visible));
    int offset_x = margin;
    int hex_x = margin + 10 * char_w;
    int ascii_x = hex_x + bpl * 3 * char_w + char_w;
    int right_limit = rc.right - margin;

    HBRUSH hl_brush = CreateSolidBrush(RGB(196, 222, 255));
    HBRUSH sel_brush = CreateSolidBrush(RGB(255, 233, 128));
    HPEN sep_pen = CreatePen(PS_SOLID, 1, RGB(215, 215, 215));
    HGDIOBJ old_pen = SelectObject(dc, sep_pen);

    for (int line = first; line < lines && line < first + visible + 1; line++) {
        int y = margin + (line - first) * line_h;
        int base = line * bpl;

        wchar_t offset[16];
        swprintf(offset, 16, L"%08X", static_cast<unsigned>(base));
        SetTextColor(dc, RGB(130, 130, 130));
        TextOutW(dc, offset_x, y, offset, 8);

        // Highlight cells first so text stays readable on top.
        for (int col = 0; col < bpl; col++) {
            int index = base + col;
            if (index >= static_cast<int>(bytes_))
                break;
            int x = hex_x + col * 3 * char_w;
            RECT cell = {x - px(1), y, x + 2 * char_w + px(1), y + line_h};
            if (has_highlight_ &&
                index >= static_cast<int>(highlight_start_) &&
                index < static_cast<int>(highlight_end_)) {
                FillRect(dc, &cell, hl_brush);
            } else if (index == selected_byte_) {
                FillRect(dc, &cell, sel_brush);
            }
            int ax = ascii_x + col * char_w;
            RECT acell = {ax, y, ax + char_w, y + line_h};
            if (has_highlight_ &&
                index >= static_cast<int>(highlight_start_) &&
                index < static_cast<int>(highlight_end_)) {
                FillRect(dc, &acell, hl_brush);
            } else if (index == selected_byte_) {
                FillRect(dc, &acell, sel_brush);
            }
        }

        // Hex text.
        std::wstring hex;
        std::wstring ascii;
        hex.reserve(static_cast<size_t>(bpl) * 3);
        ascii.reserve(static_cast<size_t>(bpl));
        for (int col = 0; col < bpl; col++) {
            int index = base + col;
            if (index >= static_cast<int>(bytes_))
                break;
            wchar_t byte_text[4];
            swprintf(byte_text, 4, L"%02X", data_[index]);
            hex += byte_text;
            hex += L' ';
            unsigned char b = data_[index];
            ascii += (b >= 32 && b < 127) ? static_cast<wchar_t>(b) : L'.';
        }
        SetTextColor(dc, RGB(30, 30, 30));
        TextOutW(dc, hex_x, y, hex.c_str(), static_cast<int>(hex.size()));
        SetTextColor(dc, RGB(90, 90, 90));
        TextOutW(dc, ascii_x, y, ascii.c_str(), static_cast<int>(ascii.size()));
    }

    // Vertical separator.
    int sep_x = ascii_x - char_w;
    MoveToEx(dc, sep_x, margin, nullptr);
    LineTo(dc, sep_x, rc.bottom - margin);
    (void)right_limit;

    SelectObject(dc, old_pen);
    DeleteObject(sep_pen);
    DeleteObject(hl_brush);
    DeleteObject(sel_brush);

    BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

int HexView::hitTest(int x, int y, int char_w, int line_h, int bpl) const
{
    if (bytes_ == 0)
        return -1;
    int margin = px(6);
    int hex_x = margin + 10 * char_w;
    int ascii_x = hex_x + bpl * 3 * char_w + char_w;
    int line = (y - margin) / std::max(1, line_h) + scroll_line_;
    if (line < 0)
        return -1;
    int col = -1;
    if (x >= hex_x && x < hex_x + bpl * 3 * char_w) {
        col = (x - hex_x) / (3 * char_w);
    } else if (x >= ascii_x && x < ascii_x + bpl * char_w) {
        col = (x - ascii_x) / char_w;
    }
    if (col < 0 || col >= bpl)
        return -1;
    int index = line * bpl + col;
    if (index < 0 || index >= static_cast<int>(bytes_))
        return -1;
    return index;
}

void HexView::copyToClipboard(HWND hwnd)
{
    if (!data_)
        return;
    std::wstring text;
    if (selected_byte_ >= 0) {
        wchar_t buf[8];
        swprintf(buf, 8, L"%02X", data_[selected_byte_]);
        text = buf;
    } else if (has_highlight_) {
        for (size_t i = highlight_start_; i < highlight_end_; i++) {
            wchar_t buf[8];
            swprintf(buf, 8, L"%02X ", data_[i]);
            text += buf;
            if (text.size() > 3 * 8192)
                break;
        }
    }
    if (text.empty())
        return;
    OpenClipboard(hwnd);
    EmptyClipboard();
    size_t len = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, len);
    if (mem) {
        auto *dst = static_cast<wchar_t *>(GlobalLock(mem));
        memcpy(dst, text.c_str(), len);
        GlobalUnlock(mem);
        SetClipboardData(CF_UNICODETEXT, mem);
    }
    CloseClipboard();
}

LRESULT HexView::handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        paintTo(dc, rc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_PRINTCLIENT: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        paintTo(reinterpret_cast<HDC>(wparam), rc);
        return 0;
    }
    case WM_SIZE:
        updateScroll(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_VSCROLL: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        HDC dc = GetDC(hwnd);
        int cw = 1, lh = 20, bpl = 16;
        metrics(dc, rc.right, cw, lh, bpl);
        ReleaseDC(hwnd, dc);
        int visible = std::max(1, (static_cast<int>(rc.bottom) - px(12)) / std::max(1, lh));
        int lines = (static_cast<int>(bytes_) + bpl - 1) / bpl;
        int max_scroll = std::max(0, lines - visible);
        int pos = scroll_line_;
        switch (LOWORD(wparam)) {
        case SB_LINEUP: pos -= 1; break;
        case SB_LINEDOWN: pos += 1; break;
        case SB_PAGEUP: pos -= visible; break;
        case SB_PAGEDOWN: pos += visible; break;
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = max_scroll; break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: {
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd, SB_VERT, &si);
            pos = si.nTrackPos;
            break;
        }
        default:
            break;
        }
        pos = std::min(std::max(0, pos), max_scroll);
        if (pos != scroll_line_) {
            scroll_line_ = pos;
            updateScroll(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wparam);
        wheel_remainder_ += delta;
        int lines = wheel_remainder_ / WHEEL_DELTA;
        wheel_remainder_ -= lines * WHEEL_DELTA;
        if (lines != 0) {
            scroll_line_ = std::max(0, scroll_line_ - lines * 3);
            updateScroll(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HDC dc = GetDC(hwnd);
        int cw = 1, lh = 20, bpl = 16;
        metrics(dc, rc.right, cw, lh, bpl);
        ReleaseDC(hwnd, dc);
        selected_byte_ =
            hitTest(static_cast<short>(LOWORD(lparam)),
                    static_cast<short>(HIWORD(lparam)), cw, lh, bpl);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (wparam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            copyToClipboard(hwnd);
            return 0;
        }
        if (wparam == VK_UP || wparam == VK_DOWN || wparam == VK_PRIOR ||
            wparam == VK_NEXT || wparam == VK_HOME || wparam == VK_END) {
            SendMessageW(hwnd, WM_VSCROLL,
                         wparam == VK_UP ? SB_LINEUP
                                         : wparam == VK_DOWN ? SB_LINEDOWN
                                         : wparam == VK_PRIOR ? SB_PAGEUP
                                         : wparam == VK_NEXT ? SB_PAGEDOWN
                                         : wparam == VK_HOME ? SB_TOP
                                                             : SB_BOTTOM,
                         0);
            return 0;
        }
        break;
    case WM_CHAR:
        if (wparam == 3) {
            copyToClipboard(hwnd);
            return 0;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace ui
