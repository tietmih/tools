#include "ui/thumbnail_strip.h"

#include <algorithm>

#include "ui/lang.h"
#include "ui/ui_util.h"

namespace ui {

namespace {
const wchar_t kClassName[] = L"BsThumbnailStrip";
}

void ThumbnailStrip::create(HWND parent, HINSTANCE inst, int id) {
  parent_ = parent;
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &ThumbnailStrip::wndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);
  hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, kClassName, L"", WS_CHILD | WS_VISIBLE | WS_HSCROLL, 0,
      0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst,
      this);
}

LRESULT CALLBACK ThumbnailStrip::wndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                         LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return TRUE;
  }
  auto* self =
      reinterpret_cast<ThumbnailStrip*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self) return self->handle(hwnd, msg, wparam, lparam);
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int ThumbnailStrip::slotWidth() const { return px(80); }

int ThumbnailStrip::slotCount() const {
  if (!stream_ || stream_->frames().empty()) return 0;
  return (static_cast<int>(stream_->frames().size()) + step_ - 1) / step_;
}

int ThumbnailStrip::frameForSlot(int slot) const {
  int frame = slot * step_;
  if (!stream_ || stream_->frames().empty()) return -1;
  return std::min(frame, static_cast<int>(stream_->frames().size()) - 1);
}

int ThumbnailStrip::slotForFrame(int frame) const {
  if (step_ <= 0) return 0;
  return frame / step_;
}

void ThumbnailStrip::setStream(const bs::Stream* stream) {
  stream_ = stream;
  thumbs_.clear();
  requested_.clear();
  scroll_x_ = 0;
  selected_ = -1;
  const int count = stream_ ? static_cast<int>(stream_->frames().size()) : 0;
  step_ = count > 120 ? std::max(1, count / 120) : 1;
  updateScroll();
  invalidate();
}

void ThumbnailStrip::setSelected(int frame_index) {
  selected_ = frame_index;
  if (!stream_ || frame_index < 0 || hwnd_ == nullptr) {
    invalidate();
    return;
  }
  RECT rc;
  GetClientRect(hwnd_, &rc);
  const int slot = slotForFrame(frame_index);
  const int left = slot * slotWidth();
  const int width = static_cast<int>(rc.right);
  if (left < scroll_x_)
    scroll_x_ = left;
  else if (left + slotWidth() > scroll_x_ + width)
    scroll_x_ = left + slotWidth() - width;
  updateScroll();
  invalidate();
}

void ThumbnailStrip::setThumbnail(int frame_index, Thumb thumb) {
  thumbs_[frame_index] = std::move(thumb);
  invalidate();
}

void ThumbnailStrip::clearThumbnails() {
  thumbs_.clear();
  requested_.clear();
  invalidate();
}

void ThumbnailStrip::setRequestHandler(std::function<void(int)> handler) {
  request_ = std::move(handler);
}

void ThumbnailStrip::invalidate() {
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void ThumbnailStrip::updateScroll() {
  if (!hwnd_) return;
  RECT rc;
  GetClientRect(hwnd_, &rc);
  const int total = slotCount() * slotWidth();
  const int page = std::max(1, static_cast<int>(rc.right));
  SCROLLINFO info = {};
  info.cbSize = sizeof(info);
  info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  info.nMin = 0;
  info.nMax = std::max(0, total - 1);
  info.nPage = static_cast<UINT>(page);
  const int max_scroll = std::max(0, total - page);
  scroll_x_ = std::min(std::max(0, scroll_x_), max_scroll);
  info.nPos = scroll_x_;
  SetScrollInfo(hwnd_, SB_HORZ, &info, TRUE);
}

void ThumbnailStrip::paintTo(HDC dc, const RECT& rc) {
  HBRUSH background = CreateSolidBrush(RGB(24, 24, 24));
  FillRect(dc, &rc, background);
  DeleteObject(background);
  if (!stream_ || stream_->frames().empty()) return;

  const int slot_w = slotWidth();
  const int thumb_w = slot_w - px(8);
  const int count = slotCount();
  const int first = std::max(0, scroll_x_ / slot_w);
  const int last =
      std::min(count, (scroll_x_ + static_cast<int>(rc.right)) / slot_w + 1);
  int requests = 0;
  for (int slot = first; slot < last; slot++) {
    const int frame_index = frameForSlot(slot);
    if (frame_index < 0) continue;
    const int x = slot * slot_w - scroll_x_ + px(4);
    const int y = px(4);
    const int h = std::max(1, static_cast<int>(rc.bottom) - px(8));
    RECT box = {x, y, x + thumb_w, y + h};
    const bs::FrameInfo& frame =
        stream_->frames()[static_cast<size_t>(frame_index)];
    auto it = thumbs_.find(frame_index);
    if (it != thumbs_.end() && !it->second.bgra.empty()) {
      BITMAPINFO info = {};
      info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      info.bmiHeader.biWidth = it->second.width;
      info.bmiHeader.biHeight = -it->second.height;
      info.bmiHeader.biPlanes = 1;
      info.bmiHeader.biBitCount = 32;
      info.bmiHeader.biCompression = BI_RGB;
      SetStretchBltMode(dc, HALFTONE);
      StretchDIBits(dc, box.left, box.top, box.right - box.left,
                    box.bottom - box.top, 0, 0, it->second.width,
                    it->second.height, it->second.bgra.data(), &info,
                    DIB_RGB_COLORS, SRCCOPY);
    } else {
      HBRUSH placeholder = CreateSolidBrush(RGB(52, 52, 52));
      FillRect(dc, &box, placeholder);
      DeleteObject(placeholder);
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, RGB(160, 160, 160));
      SelectObject(dc, uiFont());
      std::wstring index = L"#" + std::to_wstring(frame_index);
      DrawTextW(dc, index.c_str(), -1, &box,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      if (!requested_[frame_index] && request_ && requests < 8) {
        requested_[frame_index] = true;
        request_(frame_index);
        requests++;
      }
    }
    if (frame.idr || frame.irap || frame.gop_start == frame_index) {
      HPEN pen = CreatePen(PS_SOLID, 2, RGB(210, 80, 60));
      HGDIOBJ old = SelectObject(dc, pen);
      MoveToEx(dc, box.left - px(2), box.top, nullptr);
      LineTo(dc, box.left - px(2), box.bottom);
      SelectObject(dc, old);
      DeleteObject(pen);
    }
    if (frame_index == selected_) {
      HPEN pen = CreatePen(PS_SOLID, 2, RGB(80, 170, 255));
      HGDIOBJ old = SelectObject(dc, pen);
      HGDIOBJ brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
      Rectangle(dc, box.left - 1, box.top - 1, box.right + 1, box.bottom + 1);
      SelectObject(dc, brush);
      SelectObject(dc, old);
      DeleteObject(pen);
    }
  }
}

LRESULT ThumbnailStrip::handle(HWND hwnd, UINT msg, WPARAM wparam,
                               LPARAM lparam) {
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
    case WM_ERASEBKGND:
      return 1;
    case WM_SIZE:
      updateScroll();
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_MOUSEWHEEL: {
      const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
      scroll_x_ -= delta * slotWidth() / WHEEL_DELTA;
      updateScroll();
      return 0;
    }
    case WM_HSCROLL: {
      SCROLLINFO info = {};
      info.cbSize = sizeof(info);
      info.fMask = SIF_ALL;
      GetScrollInfo(hwnd, SB_HORZ, &info);
      int pos = info.nPos;
      switch (LOWORD(wparam)) {
        case SB_LINELEFT:
          pos -= slotWidth();
          break;
        case SB_LINERIGHT:
          pos += slotWidth();
          break;
        case SB_PAGELEFT:
          pos -= static_cast<int>(info.nPage);
          break;
        case SB_PAGERIGHT:
          pos += static_cast<int>(info.nPage);
          break;
        case SB_THUMBTRACK:
          pos = info.nTrackPos;
          break;
        default:
          return 0;
      }
      scroll_x_ = pos;
      updateScroll();
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_LBUTTONDOWN: {
      const int x = static_cast<short>(LOWORD(lparam) & 0xFFFF);
      const int slot = (scroll_x_ + x) / slotWidth();
      const int frame = frameForSlot(slot);
      if (frame >= 0)
        PostMessageW(parent_, WM_APP_THUMBNAIL_SELECTED,
                     static_cast<WPARAM>(frame), 0);
      return 0;
    }
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace ui
