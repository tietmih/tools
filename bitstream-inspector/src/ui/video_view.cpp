#include "ui/video_view.h"

#include <algorithm>

#include "ui/lang.h"
#include "ui/ui_util.h"

namespace ui {

namespace {
const wchar_t kClassName[] = L"BsVideoView";
}

void VideoView::create(HWND parent, HINSTANCE inst, int id) {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &VideoView::wndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);
  hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, kClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
      parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, this);
  message_ = tr(L"Select a frame to decode", L"选择一帧进行解码");
}

LRESULT CALLBACK VideoView::wndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                    LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return TRUE;
  }
  auto* self =
      reinterpret_cast<VideoView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self) return self->handle(hwnd, msg, wparam, lparam);
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void VideoView::setImage(ImageData image, const std::wstring& note) {
  image_ = std::move(image);
  note_ = note;
  message_.clear();
  probe_ = Probe();
  paintNow();
}

void VideoView::setOverlay(std::vector<bs::OverlayLine> lines) {
  overlay_ = std::move(lines);
  paintNow();
}

void VideoView::clear(const std::wstring& message) {
  image_ = ImageData();
  image_rect_ = {};
  probe_ = Probe();
  note_.clear();
  message_ = message;
  paintNow();
}

void VideoView::paintNow() {
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

int VideoView::samplePlane(int plane, int x, int y) const {
  if (plane < 0 || plane >= static_cast<int>(image_.planes.size())) return 0;
  if (x < 0 || y < 0) return 0;
  const int bytes = image_.bit_depth > 8 ? 2 : 1;
  const int stride = image_.strides[static_cast<size_t>(plane)];
  const auto& data = image_.planes[static_cast<size_t>(plane)];
  if (static_cast<size_t>(y) * stride + static_cast<size_t>(x + 1) * bytes >
      data.size())
    return 0;
  const uint8_t* p = data.data() + static_cast<size_t>(y) * stride +
                     static_cast<size_t>(x) * bytes;
  if (bytes == 2) return p[0] | (p[1] << 8);
  return p[0];
}

bool VideoView::updateProbe(int client_x, int client_y) {
  probe_ = Probe();
  if (image_.bgra.empty() || image_rect_.right <= image_rect_.left ||
      image_rect_.bottom <= image_rect_.top)
    return false;
  if (client_x < image_rect_.left || client_x >= image_rect_.right ||
      client_y < image_rect_.top || client_y >= image_rect_.bottom)
    return false;
  const int draw_w = image_rect_.right - image_rect_.left;
  const int draw_h = image_rect_.bottom - image_rect_.top;
  int x = (client_x - image_rect_.left) * image_.width / draw_w;
  int y = (client_y - image_rect_.top) * image_.height / draw_h;
  if (x < 0 || x >= image_.width || y < 0 || y >= image_.height) return false;
  probe_.x = x;
  probe_.y = y;
  probe_.y_value = samplePlane(0, x, y);
  if (image_.planes.size() >= 3) {
    const int cx =
        image_.chroma_format == 1 || image_.chroma_format == 2 ? x / 2 : x;
    const int cy = image_.chroma_format == 1 ? y / 2 : y;
    probe_.cb = samplePlane(1, cx, cy);
    probe_.cr = samplePlane(2, cx, cy);
  }
  const size_t offset =
      (static_cast<size_t>(y) * image_.width + static_cast<size_t>(x)) * 4;
  if (offset + 3 < image_.bgra.size()) {
    probe_.b = image_.bgra[offset + 0];
    probe_.g = image_.bgra[offset + 1];
    probe_.r = image_.bgra[offset + 2];
  }
  probe_.valid = true;
  return true;
}

void VideoView::paintTo(HDC target, const RECT& rc) {
  HBRUSH background = CreateSolidBrush(RGB(32, 32, 32));
  FillRect(target, &rc, background);
  DeleteObject(background);

  int client_w = rc.right - rc.left;
  int client_h = rc.bottom - rc.top;
  if (image_.bgra.empty() || image_.width <= 0 || image_.height <= 0) {
    SetBkMode(target, TRANSPARENT);
    SetTextColor(target, RGB(190, 190, 190));
    SelectObject(target, uiFont());
    RECT text_rc = rc;
    DrawTextW(target,
              message_.empty() ? tr(L"No decoded frame", L"无解码画面")
                               : message_.c_str(),
              -1, &text_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }

  int margin = px(4);
  double scale =
      std::min(static_cast<double>(client_w - 2 * margin) / image_.width,
               static_cast<double>(client_h - 2 * margin) / image_.height);
  if (scale <= 0.0) return;
  int draw_w = std::max(1, static_cast<int>(image_.width * scale));
  int draw_h = std::max(1, static_cast<int>(image_.height * scale));
  int x = (client_w - draw_w) / 2;
  int y = (client_h - draw_h) / 2;
  image_rect_ = {x, y, x + draw_w, y + draw_h};

  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = image_.width;
  info.bmiHeader.biHeight = -image_.height;  // top-down
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  SetStretchBltMode(target, HALFTONE);
  SetBrushOrgEx(target, 0, 0, nullptr);
  StretchDIBits(target, x, y, draw_w, draw_h, 0, 0, image_.width, image_.height,
                image_.bgra.data(), &info, DIB_RGB_COLORS, SRCCOPY);

  if (!note_.empty()) {
    SelectObject(target, uiFont());
    SetBkMode(target, OPAQUE);
    SetBkColor(target, RGB(0, 0, 0));
    SetTextColor(target, RGB(255, 220, 120));
    RECT note_rc = {x + px(4), y + px(4), x + draw_w - px(4), y + px(24)};
    DrawTextW(target, note_.c_str(), -1, &note_rc,
              DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
  }

  if (!overlay_.empty() && image_.width > 0 && image_.height > 0) {
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 80, 60));
    HGDIOBJ old = SelectObject(target, pen);
    for (const auto& line : overlay_) {
      int x1 = x + line.x1 * draw_w / image_.width;
      int y1 = y + line.y1 * draw_h / image_.height;
      int x2 = x + line.x2 * draw_w / image_.width;
      int y2 = y + line.y2 * draw_h / image_.height;
      MoveToEx(target, x1, y1, nullptr);
      LineTo(target, x2, y2);
    }
    SelectObject(target, old);
    DeleteObject(pen);
  }

  if (probe_.valid) {
    wchar_t text[256];
    swprintf(text, 256, L"X %d  Y %d    Y %d  Cb %d  Cr %d    R %d  G %d  B %d",
             probe_.x, probe_.y, probe_.y_value, probe_.cb, probe_.cr, probe_.r,
             probe_.g, probe_.b);
    SelectObject(target, uiFont());
    SetBkMode(target, OPAQUE);
    SetBkColor(target, RGB(0, 0, 0));
    SetTextColor(target, RGB(255, 255, 0));
    RECT probe_rc = {image_rect_.left + px(4), image_rect_.bottom - px(22),
                     image_rect_.right - px(4), image_rect_.bottom};
    DrawTextW(target, text, -1, &probe_rc,
              DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
  }
}

LRESULT VideoView::handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
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
    case WM_MOUSEMOVE:
      if (!tracking_) {
        TRACKMOUSEEVENT tme = {};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        tracking_ = true;
      }
      if (updateProbe(static_cast<short>(LOWORD(lparam)),
                      static_cast<short>(HIWORD(lparam))))
        InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_MOUSELEAVE:
      tracking_ = false;
      probe_ = Probe();
      InvalidateRect(hwnd, nullptr, FALSE);
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
