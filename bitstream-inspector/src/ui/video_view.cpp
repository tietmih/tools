#include "ui/video_view.h"

#include <algorithm>

#include "ui/ui_util.h"

namespace ui {

namespace {
const wchar_t kClassName[] = L"BsVideoView";
}

void VideoView::create(HWND parent, HINSTANCE inst, int id)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &VideoView::wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_CLIENTEDGE, kClassName, L"",
                            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                            inst, this);
    message_ = L"Select a frame to decode";
}

LRESULT CALLBACK VideoView::wndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                    LPARAM lparam)
{
    if (msg == WM_NCCREATE) {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    auto *self =
        reinterpret_cast<VideoView *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self)
        return self->handle(hwnd, msg, wparam, lparam);
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void VideoView::setImage(std::vector<uint8_t> bgra, int width, int height,
                         const std::wstring &note)
{
    image_ = std::move(bgra);
    image_w_ = width;
    image_h_ = height;
    note_ = note;
    message_.clear();
    paintNow();
}

void VideoView::clear(const std::wstring &message)
{
    image_.clear();
    image_w_ = 0;
    image_h_ = 0;
    note_.clear();
    message_ = message;
    paintNow();
}

void VideoView::paintNow()
{
    if (hwnd_)
        InvalidateRect(hwnd_, nullptr, FALSE);
}

void VideoView::paintTo(HDC target, const RECT &rc)
{
    HBRUSH background = CreateSolidBrush(RGB(32, 32, 32));
    FillRect(target, &rc, background);
    DeleteObject(background);

    int client_w = rc.right - rc.left;
    int client_h = rc.bottom - rc.top;
    if (image_.empty() || image_w_ <= 0 || image_h_ <= 0) {
        SetBkMode(target, TRANSPARENT);
        SetTextColor(target, RGB(190, 190, 190));
        SelectObject(target, uiFont());
        RECT text_rc = rc;
        DrawTextW(target,
                  message_.empty() ? L"No decoded frame" : message_.c_str(),
                  -1, &text_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    int margin = px(4);
    double scale = std::min(
        static_cast<double>(client_w - 2 * margin) / image_w_,
        static_cast<double>(client_h - 2 * margin) / image_h_);
    if (scale <= 0.0)
        return;
    int draw_w = std::max(1, static_cast<int>(image_w_ * scale));
    int draw_h = std::max(1, static_cast<int>(image_h_ * scale));
    int x = (client_w - draw_w) / 2;
    int y = (client_h - draw_h) / 2;

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = image_w_;
    info.bmiHeader.biHeight = -image_h_; // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    SetStretchBltMode(target, HALFTONE);
    SetBrushOrgEx(target, 0, 0, nullptr);
    StretchDIBits(target, x, y, draw_w, draw_h, 0, 0, image_w_, image_h_,
                  image_.data(), &info, DIB_RGB_COLORS, SRCCOPY);

    if (!note_.empty()) {
        SelectObject(target, uiFont());
        SetBkMode(target, OPAQUE);
        SetBkColor(target, RGB(0, 0, 0));
        SetTextColor(target, RGB(255, 220, 120));
        RECT note_rc = {x + px(4), y + px(4), x + draw_w - px(4),
                        y + px(24)};
        DrawTextW(target, note_.c_str(), -1, &note_rc,
                  DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    }
}

LRESULT VideoView::handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
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
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace ui
