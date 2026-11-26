#include "ui/chart_view.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>

#include "ui/frame_list.h"
#include "ui/lang.h"
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
}  // namespace

void ChartView::create(HWND parent, HINSTANCE inst, int id) {
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
  hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, kClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
      parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, this);
}

LRESULT CALLBACK ChartView::wndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                    LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return TRUE;
  }
  auto* self =
      reinterpret_cast<ChartView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self) return self->handle(hwnd, msg, wparam, lparam);
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void ChartView::setStream(const Stream* stream) {
  stream_ = stream;
  selected_ = -1;
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void ChartView::setSelected(int frame_index) {
  selected_ = frame_index;
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void ChartView::setMode(ChartMode mode) {
  mode_ = mode;
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void ChartView::invalidate() {
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void ChartView::onClick(HWND hwnd, int x, int y) {
  (void)y;
  if (!stream_ || stream_->frames().empty()) return;
  RECT rc;
  GetClientRect(hwnd, &rc);
  Plot plot = {px(10), rc.right - px(10), px(30), rc.bottom - px(22)};
  int width = plot.right - plot.left;
  if (width <= 0 || x < plot.left || x >= plot.right) return;
  size_t count = stream_->frames().size();
  size_t index =
      static_cast<size_t>(x - plot.left) * count / static_cast<size_t>(width);
  if (index >= count) index = count - 1;
  SendMessageW(parent_, WM_APP_FRAME_FROM_CHART, static_cast<WPARAM>(index), 0);
}

void ChartView::paintTo(HDC target, const RECT& rc) {
  // PrintWindow/WM_PRINTCLIENT may hand us a DC without a usable bitmap
  // (BITSPIXEL == 0). Fall back to drawing directly on the target DC.
  HDC memdc = CreateCompatibleDC(target);
  HBITMAP bitmap =
      memdc ? CreateCompatibleBitmap(target, std::max(1, (int)rc.right),
                                     std::max(1, (int)rc.bottom))
            : nullptr;
  bool buffered = memdc && bitmap;
  HDC dc = buffered ? memdc : target;
  HGDIOBJ old_bmp = buffered ? SelectObject(dc, bitmap) : nullptr;
  if (memdc && !buffered) DeleteDC(memdc);
  auto finish = [&]() {
    if (!buffered) return;
    BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_bmp);
    DeleteObject(bitmap);
    DeleteDC(dc);
  };
  FillRect(dc, &rc, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
  SetBkMode(dc, TRANSPARENT);

  if (!stream_ || stream_->frames().empty()) {
    SelectObject(dc, uiFont());
    SetTextColor(dc, RGB(130, 130, 130));
    RECT text_rc = rc;
    DrawTextW(dc,
              tr(L"Open an H.264/H.265 Annex B elementary stream",
                 L"打开 H.264/H.265 码流"),
              -1, &text_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    finish();
    return;
  }
  const auto& frames = stream_->frames();

  Plot plot = {px(10), rc.right - px(10), px(34), rc.bottom - px(22)};
  if (mode_ == ChartMode::Bits) {
    paintBits(dc, rc);
    finish();
    return;
  }
  if (mode_ == ChartMode::Cpb) {
    paintCpb(dc, rc);
    finish();
    return;
  }
  int width = plot.right - plot.left;
  int height = plot.bottom - plot.top;
  if (width <= 4 || height <= 4) {
    finish();
    return;
  }

  size_t count = frames.size();
  size_t max_size = 1;
  for (const auto& f : frames) max_size = std::max(max_size, f.byte_size);

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
      CreateSolidBrush(RGB(196, 48, 48)),    // IDR
      CreateSolidBrush(RGB(216, 128, 32)),   // I
      CreateSolidBrush(RGB(52, 108, 200)),   // P
      CreateSolidBrush(RGB(46, 148, 88)),    // B
      CreateSolidBrush(RGB(150, 150, 150)),  // other
      CreateSolidBrush(RGB(150, 80, 170)),   // CRA
      CreateSolidBrush(RGB(210, 90, 60)),    // BLA
  };
  auto brush_index = [&](const FrameInfo& f) {
    if (f.idr) return 0;
    if (stream_->codec() == Codec::H265 && f.irap)
      return f.nal_type == 21 ? 5 : 6;
    switch (f.picture_type) {
      case 'I':
        return 1;
      case 'P':
        return 2;
      case 'B':
        return 3;
      default:
        return 4;
    }
  };

  const int strip_top = px(18);
  const int strip_h = px(10);

  for (int x = plot.left; x < plot.right; x++) {
    size_t first =
        static_cast<size_t>(x - plot.left) * count / static_cast<size_t>(width);
    size_t last = static_cast<size_t>(x - plot.left + 1) * count /
                  static_cast<size_t>(width);
    if (last <= first) last = first + 1;
    if (last > count) last = count;

    // Aggregate very wide ranges so painting stays linear in pixels.
    size_t step = std::max<size_t>(1, (last - first) / 64);
    size_t peak = 0;
    int brush = brush_index(frames[first]);
    for (size_t i = first; i < last; i += step) {
      peak = std::max(peak, frames[i].byte_size);
      if (frames[i].idr) brush = 0;
    }
    int bar_h = static_cast<int>(static_cast<double>(peak) /
                                 static_cast<double>(max_size) * height);
    if (bar_h < 1 && peak > 0) bar_h = 1;
    RECT bar = {x, plot.bottom - bar_h, x + 1, plot.bottom};
    FillRect(dc, &bar, brushes[brush]);

    RECT strip = {x, strip_top, x + 1, strip_top + strip_h};
    FillRect(dc, &strip, brushes[brush]);
  }
  for (auto& brush : brushes) DeleteObject(brush);

  // Selected frame marker.
  if (selected_ >= 0 && selected_ < static_cast<int>(count)) {
    int x = plot.left + static_cast<int>(static_cast<double>(selected_) *
                                         width / static_cast<double>(count));
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
  std::wstring left_label =
      tr(L"frame size (bytes)    max ", L"帧大小（字节）    最大 ") +
      std::to_wstring(max_size) + tr(L" B", L" 字节");
  TextOutW(dc, plot.left, px(2), left_label.c_str(),
           static_cast<int>(left_label.size()));
  std::wstring count_label = std::to_wstring(count) + tr(L" frames", L" 帧");
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

  finish();
}

void ChartView::paintBits(HDC dc, const RECT& rc) {
  SelectObject(dc, uiFont());
  SetBkMode(dc, TRANSPARENT);
  if (!stream_ || stream_->frames().empty()) return;
  Plot plot = {px(10), rc.right - px(10), px(34), rc.bottom - px(22)};
  int width = plot.right - plot.left;
  int height = plot.bottom - plot.top;
  if (width <= 4 || height <= 4) return;
  const auto& frames = stream_->frames();
  uint64_t max_total = 1;
  for (const auto& f : frames) max_total = std::max(max_total, f.bits.total);
  const size_t count = frames.size();
  const COLORREF colors[6] = {RGB(110, 150, 210), RGB(210, 130, 60),
                              RGB(90, 170, 110),  RGB(150, 100, 190),
                              RGB(150, 150, 150), RGB(200, 80, 80)};
  for (int x = plot.left; x < plot.right; x++) {
    size_t first =
        static_cast<size_t>(x - plot.left) * count / static_cast<size_t>(width);
    size_t last = static_cast<size_t>(x - plot.left + 1) * count /
                  static_cast<size_t>(width);
    if (last <= first) last = first + 1;
    if (last > count) last = count;
    uint64_t values[6] = {};
    for (size_t i = first; i < last; i++) {
      values[0] += frames[i].bits.parameter_sets;
      values[1] += frames[i].bits.sei;
      values[2] += frames[i].bits.slice_headers;
      values[3] += frames[i].bits.slice_payload;
      values[4] += frames[i].bits.filler;
      values[5] += frames[i].bits.other;
    }
    int bottom = plot.bottom;
    for (int c = 0; c < 6; c++) {
      if (values[c] == 0) continue;
      int h =
          static_cast<int>(static_cast<double>(values[c]) / max_total * height);
      if (h < 1) h = 1;
      RECT bar = {x, std::max(plot.top, bottom - h), x + 1, bottom};
      HBRUSH brush = CreateSolidBrush(colors[c]);
      FillRect(dc, &bar, brush);
      DeleteObject(brush);
      bottom = bar.top;
    }
  }
  SetTextColor(dc, RGB(100, 100, 100));
  std::wstring title = tr(L"bit allocation (bits)", L"比特分配（bit）");
  TextOutW(dc, plot.left, px(2), title.c_str(), static_cast<int>(title.size()));
  double fps = stream_->fps();
  if (!(fps > 0.0)) fps = 25.0;
  int window = std::max(1, static_cast<int>(fps + 0.5));
  int end = std::min(static_cast<int>(count), std::max(0, selected_) + 1);
  int start = std::max(0, end - window);
  double rate = stream_->windowBitrate(start, end);
  wchar_t footer[128];
  swprintf(footer, 128, L"window %.2f s  %.0f b/s",
           static_cast<double>(end - start) / fps, rate);
  TextOutW(dc, plot.left, plot.bottom + px(4), footer,
           static_cast<int>(wcslen(footer)));
}

void ChartView::paintCpb(HDC dc, const RECT& rc) {
  SelectObject(dc, uiFont());
  SetBkMode(dc, TRANSPARENT);
  const CpbModelResult& model = stream_->cpbModel();
  Plot plot = {px(10), rc.right - px(10), px(34), rc.bottom - px(22)};
  int width = plot.right - plot.left;
  int height = plot.bottom - plot.top;
  if (width <= 4 || height <= 4) return;

  if (!model.valid || model.samples.empty()) {
    SetTextColor(dc, RGB(130, 130, 130));
    std::wstring message =
        model.error.empty()
            ? tr(L"No HRD parameters in the active SPS",
                 L"当前 SPS 没有 HRD 参数")
            : std::wstring(model.error.begin(), model.error.end());
    RECT text_rc = rc;
    DrawTextW(dc, message.c_str(), -1, &text_rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }

  HPEN grid_pen = CreatePen(PS_SOLID, 1, RGB(228, 228, 228));
  HGDIOBJ old_pen = SelectObject(dc, grid_pen);
  for (int i = 0; i <= 4; i++) {
    int y = plot.bottom - height * i / 4;
    MoveToEx(dc, plot.left, y, nullptr);
    LineTo(dc, plot.right, y);
  }
  HPEN full_pen = CreatePen(PS_DASH, 1, RGB(150, 150, 150));
  SelectObject(dc, full_pen);
  MoveToEx(dc, plot.left, plot.top, nullptr);
  LineTo(dc, plot.right, plot.top);
  SelectObject(dc, old_pen);
  DeleteObject(full_pen);
  DeleteObject(grid_pen);

  HBRUSH bar_brush = CreateSolidBrush(RGB(52, 108, 200));
  HBRUSH under_brush = CreateSolidBrush(RGB(196, 48, 48));
  const size_t count = model.samples.size();
  for (int x = plot.left; x < plot.right; x++) {
    size_t first =
        static_cast<size_t>(x - plot.left) * count / static_cast<size_t>(width);
    size_t last = static_cast<size_t>(x - plot.left + 1) * count /
                  static_cast<size_t>(width);
    if (last <= first) last = first + 1;
    if (last > count) last = count;
    double ratio = model.samples[first].occupancy_ratio;
    bool underflow = model.samples[first].underflow;
    bool overflow = model.samples[first].overflow;
    for (size_t i = first; i < last; i++) {
      ratio = std::min(ratio, model.samples[i].occupancy_ratio);
      underflow = underflow || model.samples[i].underflow;
      overflow = overflow || model.samples[i].overflow;
    }
    ratio = std::max(-0.05, std::min(1.0, ratio));
    int top = plot.bottom - static_cast<int>(ratio * height);
    if (top > plot.bottom) top = plot.bottom;
    RECT bar = {x, top, x + 1, plot.bottom};
    FillRect(dc, &bar, underflow ? under_brush : bar_brush);
    if (underflow) {
      RECT mark = {x, plot.bottom - 2, x + 1, plot.bottom};
      FillRect(dc, &mark, under_brush);
    }
    if (overflow) {
      RECT mark = {x, plot.top, x + 1, plot.top + 2};
      FillRect(dc, &mark, under_brush);
    }
  }
  DeleteObject(bar_brush);
  DeleteObject(under_brush);

  if (selected_ >= 0 && selected_ < static_cast<int>(count)) {
    int x = plot.left + static_cast<int>(static_cast<double>(selected_) *
                                         width / static_cast<double>(count));
    HPEN marker = CreatePen(PS_SOLID, 2, RGB(30, 30, 30));
    HGDIOBJ old = SelectObject(dc, marker);
    MoveToEx(dc, x, plot.top, nullptr);
    LineTo(dc, x, plot.bottom);
    SelectObject(dc, old);
    DeleteObject(marker);
  }

  SetTextColor(dc, RGB(100, 100, 100));
  std::wstring title = tr(L"CPB occupancy (%)", L"CPB 占用率（%）");
  TextOutW(dc, plot.left, px(2), title.c_str(), static_cast<int>(title.size()));
  wchar_t params[128];
  swprintf(params, 128, L"%llu b/s  %llu bits",
           static_cast<unsigned long long>(model.params.bit_rate),
           static_cast<unsigned long long>(model.params.cpb_size));
  std::wstring source(model.params.source.begin(), model.params.source.end());
  std::wstring right = source + params;
  SIZE sz = {};
  GetTextExtentPoint32W(dc, right.c_str(), static_cast<int>(right.size()), &sz);
  TextOutW(dc, plot.right - sz.cx, px(2), right.c_str(),
           static_cast<int>(right.size()));
  std::wstring footer = tr(L"underflows: ", L"下溢：") +
                        std::to_wstring(model.underflow_count) + L"    " +
                        tr(L"overflows: ", L"溢出：") +
                        std::to_wstring(model.full_events);
  if (model.params.source == "estimated")
    footer += tr(L"  (estimated)", L"（估算）");
  TextOutW(dc, plot.left, plot.bottom + px(4), footer.c_str(),
           static_cast<int>(footer.size()));
}

LRESULT ChartView::handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
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

}  // namespace ui
