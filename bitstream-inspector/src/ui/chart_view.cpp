#include "ui/chart_view.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>

#include "ui/frame_list.h"
#include "ui/ui_util.h"

namespace ui {

using namespace bs;

namespace {
const wchar_t kClassName[] = L"BsChartView";

struct Plot {
    int left;
    int right;
    int top;
    int bottom;
};
} // namespace

void ChartView::create(HWND parent, HINSTANCE inst, int id)
{
    parent_ = parent;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &ChartView::wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_CLIENTEDGE, kClassName, L"",
                            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                            inst, this);
}

LRESULT CALLBACK ChartView::wndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                    LPARAM lparam)
{
    if (msg == WM_NCCREATE) {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    auto *self =
        reinterpret_cast<ChartView *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self)
        return self->handle(hwnd, msg, wparam, lparam);
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void ChartView::setStream(const Stream *stream)
{
    stream_ = stream;
    selected_ = -1;
    if (hwnd_)
        InvalidateRect(hwnd_, nullptr, FALSE);
}

void ChartView::setSelected(int frame_index)
{
    selected_ = frame_index;
    if (hwnd_)
        InvalidateRect(hwnd_, nullptr, FALSE);
}

void ChartView::invalidate()
{
    if (hwnd_)
        InvalidateRect(hwnd_, nullptr, FALSE);
}

void ChartView::onClick(HWND hwnd, int x, int y)
{
    (void)y;
    if (!stream_ || stream_->frames().empty())
        return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    Plot plot = {px(10), rc.right - px(10), px(30), rc.bottom - px(22)};
    int width = plot.right - plot.left;
    if (width <= 0 || x < plot.left || x >= plot.right)
        return;
    size_t count = stream_->frames().size();
    size_t index = static_cast<size_t>(x - plot.left) * count /
                   static_cast<size_t>(width);
    if (index >= count)
        index = count - 1;
    SendMessageW(parent_, WM_APP_FRAME_FROM_CHART, static_cast<WPARAM>(index), 0);
}

void ChartView::paintTo(HDC target, const RECT &rc)
{
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, std::max(1, (int)rc.right),
                                            std::max(1, (int)rc.bottom));
    HGDIOBJ old_bmp = SelectObject(dc, bitmap);
    FillRect(dc, &rc, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    SetBkMode(dc, TRANSPARENT);

    if (!stream_ || stream_->frames().empty()) {
        SelectObject(dc, uiFont());
        SetTextColor(dc, RGB(130, 130, 130));
        RECT text_rc = rc;
        DrawTextW(dc, L"Open an H.264/H.265 Annex B elementary stream",
                  -1, &text_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old_bmp);
        DeleteObject(bitmap);
        DeleteDC(dc);
        return;
    }
    const auto &frames = stream_->frames();

    Plot plot = {px(10), rc.right - px(10), px(34), rc.bottom - px(22)};
    int width = plot.right - plot.left;
    int height = plot.bottom - plot.top;
    if (width <= 4 || height <= 4) {
        SelectObject(dc, old_bmp);
        DeleteObject(bitmap);
        DeleteDC(dc);
        return;
    }

    size_t count = frames.size();
    size_t max_size = 1;
    for (const auto &f : frames)
        max_size = std::max(max_size, f.byte_size);

    // Grid.
    HPEN grid_pen = CreatePen(PS_SOLID, 1, RGB(228, 228, 228));
    HGDIOBJ old_pen = SelectObject(dc, grid_pen);
    for (int i = 1; i < 4; i++) {
        int y = plot.bottom - height * i / 4;
        MoveToEx(dc, plot.left, y, nullptr);
        LineTo(dc, plot.right, y);
    }
    SelectObject(dc, old_pen);
    DeleteObject(grid_pen);

    HBRUSH brushes[7] = {
        CreateSolidBrush(RGB(196, 48, 48)),   // IDR
        CreateSolidBrush(RGB(216, 128, 32)),  // I
        CreateSolidBrush(RGB(52, 108, 200)),  // P
        CreateSolidBrush(RGB(46, 148, 88)),   // B
        CreateSolidBrush(RGB(150, 150, 150)), // other
        CreateSolidBrush(RGB(150, 80, 170)),  // CRA
        CreateSolidBrush(RGB(210, 90, 60)),   // BLA
    };
    auto brush_index = [&](const FrameInfo &f) {
        if (f.idr)
            return 0;
        if (stream_->codec() == Codec::H265 && f.irap)
            return f.nal_type == 21 ? 5 : 6;
        switch (f.picture_type) {
        case 'I': return 1;
        case 'P': return 2;
        case 'B': return 3;
        default: return 4;
        }
    };

    const int strip_top = px(18);
    const int strip_h = px(10);

    for (int x = plot.left; x < plot.right; x++) {
        size_t first = static_cast<size_t>(x - plot.left) * count /
                       static_cast<size_t>(width);
        size_t last = static_cast<size_t>(x - plot.left + 1) * count /
                      static_cast<size_t>(width);
        if (last <= first)
            last = first + 1;
        if (last > count)
            last = count;

        // Aggregate very wide ranges so painting stays linear in pixels.
        size_t step = std::max<size_t>(1, (last - first) / 64);
        size_t peak = 0;
        int brush = brush_index(frames[first]);
        for (size_t i = first; i < last; i += step) {
            peak = std::max(peak, frames[i].byte_size);
            if (frames[i].idr)
                brush = 0;
        }
        int bar_h = static_cast<int>(static_cast<double>(peak) /
                                     static_cast<double>(max_size) * height);
        if (bar_h < 1 && peak > 0)
            bar_h = 1;
        RECT bar = {x, plot.bottom - bar_h, x + 1, plot.bottom};
        FillRect(dc, &bar, brushes[brush]);

        RECT strip = {x, strip_top, x + 1, strip_top + strip_h};
        FillRect(dc, &strip, brushes[brush]);
    }
    for (auto &brush : brushes)
        DeleteObject(brush);

    // Selected frame marker.
    if (selected_ >= 0 && selected_ < static_cast<int>(count)) {
        int x = plot.left + static_cast<int>(
                                 static_cast<double>(selected_) * width /
                                 static_cast<double>(count));
        HPEN marker = CreatePen(PS_SOLID, 2, RGB(30, 30, 30));
        HGDIOBJ old = SelectObject(dc, marker);
        MoveToEx(dc, x, strip_top, nullptr);
        LineTo(dc, x, plot.bottom);
        SelectObject(dc, old);
        DeleteObject(marker);
    }

    // Labels.
    SelectObject(dc, uiFont());
    SetTextColor(dc, RGB(100, 100, 100));
    std::wstring left_label = L"frame size (bytes)    max " +
                              std::to_wstring(max_size) + L" B";
    TextOutW(dc, plot.left, px(2), left_label.c_str(),
             static_cast<int>(left_label.size()));
    std::wstring count_label = std::to_wstring(count) + L" frames";
    SIZE sz = {};
    GetTextExtentPoint32W(dc, count_label.c_str(),
                          static_cast<int>(count_label.size()), &sz);
    TextOutW(dc, plot.right - sz.cx, px(2), count_label.c_str(),
             static_cast<int>(count_label.size()));

    double fps = stream_->fps();
    if (fps > 0.0) {
        wchar_t fps_label[64];
        swprintf(fps_label, 64, L"%.2f fps", fps);
        TextOutW(dc, plot.right - sz.cx, plot.bottom + px(4), fps_label,
                 static_cast<int>(wcslen(fps_label)));
    }

    BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_bmp);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

LRESULT ChartView::handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
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
    case WM_LBUTTONDOWN:
        onClick(hwnd, static_cast<short>(LOWORD(lparam)),
                static_cast<short>(HIWORD(lparam)));
        return 0;
    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace ui
