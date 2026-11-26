#include "ui/yuv_viewer.h"

#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "core/reference_yuv.h"
#include "ui/lang.h"
#ifdef BS_HAVE_FFMPEG
#include "media/yuv_convert.h"
#include "ui/ui_util.h"
#include "ui/video_view.h"

namespace ui {

namespace {
const wchar_t kClassName[] = L"BsYuvViewer";
constexpr int IDC_WIDTH = 3201;
constexpr int IDC_HEIGHT = 3202;
constexpr int IDC_FORMAT = 3203;
constexpr int IDC_MATRIX = 3204;
constexpr int IDC_RANGE = 3205;
constexpr int IDC_OPEN = 3206;
constexpr int IDC_PREV = 3207;
constexpr int IDC_NEXT = 3208;
constexpr int IDC_STATUS = 3209;
constexpr int IDC_VIEW = 3210;

struct State {
  HINSTANCE inst = nullptr;
  HWND hwnd = nullptr;
  HWND width = nullptr;
  HWND height = nullptr;
  HWND format = nullptr;
  HWND matrix = nullptr;
  HWND range = nullptr;
  HWND open = nullptr;
  HWND prev = nullptr;
  HWND next = nullptr;
  HWND status = nullptr;
  VideoView view;
  bs::ReferenceYuv yuv;
  std::vector<uint8_t> storage;
  std::wstring path;
  int frame = 0;
  int default_width = 1920;
  int default_height = 1080;

  void updateStatus() {
    if (!status) return;
    std::wstring text = path.empty()
                            ? tr(L"No YUV file loaded", L"未加载 YUV 文件")
                            : std::wstring(path) + L"   " +
                                  std::to_wstring(frame + 1) + L"/" +
                                  std::to_wstring(yuv.frameCount()) + L"   " +
                                  std::to_wstring(yuv.width()) + L"x" +
                                  std::to_wstring(yuv.height());
    SetWindowTextW(status, text.c_str());
  }

  bool renderFrame() {
    if (!yuv.isOpen()) return false;
    bs::YuvFrameView view;
    std::string error;
    if (!yuv.readFrame(frame, storage, view, error)) {
      this->view.clear(wide(error));
      return false;
    }
    const int matrix_index =
        static_cast<int>(SendMessageW(matrix, CB_GETCURSEL, 0, 0));
    const int matrix_value =
        matrix_index == 1 ? 5 : (matrix_index == 3 ? 9 : 1);
    const bool full_range = SendMessageW(range, CB_GETCURSEL, 0, 0) == 1;
    std::vector<uint8_t> bgra;
    if (!bs::yuvToBgra(view, matrix_value, full_range, bgra, error)) {
      this->view.clear(wide(error));
      return false;
    }
    ImageData image;
    image.bgra = std::move(bgra);
    image.width = yuv.width();
    image.height = yuv.height();
    image.bit_depth = yuv.bitDepth();
    image.chroma_format = yuv.chromaFormat();
    image.layout = yuv.layout();
    image.matrix = matrix_value;
    image.full_range = full_range;
    this->view.setImage(std::move(image), tr(L"YUV Viewer", L"YUV 查看器"));
    updateStatus();
    return true;
  }

  void openFile() {
    wchar_t file[32768] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = tr(L"Raw YUV (*.yuv)\\0*.yuv\\0All files (*.*)\\0*.*\\0",
                         L"原始 YUV (*.yuv)\\0*.yuv\\0所有文件 (*.*)\\0*.*\\0");
    ofn.lpstrFile = file;
    ofn.nMaxFile = 32768;
    ofn.lpstrDefExt = L"yuv";
    ofn.lpstrTitle = tr(L"Open raw YUV", L"打开原始 YUV");
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return;
    wchar_t text[32] = {};
    GetWindowTextW(width, text, 32);
    const int w = std::max(1, _wtoi(text));
    GetWindowTextW(height, text, 32);
    const int h = std::max(1, _wtoi(text));
    const int format_index =
        static_cast<int>(SendMessageW(format, CB_GETCURSEL, 0, 0));
    const bs::YuvLayout layouts[] = {bs::YuvLayout::I420, bs::YuvLayout::I422,
                                     bs::YuvLayout::I444, bs::YuvLayout::P010,
                                     bs::YuvLayout::P210, bs::YuvLayout::P012,
                                     bs::YuvLayout::P212};
    if (format_index < 0 ||
        format_index >= static_cast<int>(std::size(layouts)))
      return;
    std::string error;
    if (!yuv.open(utf8(file), layouts[format_index], w, h, error)) {
      MessageBoxW(hwnd, wide(error).c_str(),
                  tr(L"YUV open failed", L"YUV 打开失败"),
                  MB_OK | MB_ICONERROR);
      return;
    }
    path = file;
    frame = 0;
    renderFrame();
  }
};

LRESULT CALLBACK yuvProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  auto* state =
      reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return TRUE;
  }
  if (!state) return DefWindowProcW(hwnd, msg, wparam, lparam);
  switch (msg) {
    case WM_CREATE: {
      state->hwnd = hwnd;
      state->view.create(hwnd, state->inst, IDC_VIEW);
      state->width = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_NUMBER, 0,
          0, 0, 0, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_WIDTH)), state->inst,
          nullptr);
      state->height = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_NUMBER, 0,
          0, 0, 0, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HEIGHT)),
          state->inst, nullptr);
      state->format = CreateWindowExW(
          0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0,
          0, 200, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_FORMAT)),
          state->inst, nullptr);
      const wchar_t* formats[] = {
          L"I420 8-bit",  L"I422 8-bit",  L"I444 8-bit", L"P010 10-bit",
          L"P210 10-bit", L"P012 12-bit", L"P212 12-bit"};
      for (const wchar_t* format : formats)
        SendMessageW(state->format, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(format));
      SendMessageW(state->format, CB_SETCURSEL, 0, 0);
      state->matrix = CreateWindowExW(
          0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0,
          0, 200, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MATRIX)),
          state->inst, nullptr);
      const wchar_t* matrices[] = {L"Auto", L"BT.601", L"BT.709", L"BT.2020"};
      for (const wchar_t* matrix : matrices)
        SendMessageW(state->matrix, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(matrix));
      SendMessageW(state->matrix, CB_SETCURSEL, 2, 0);
      state->range = CreateWindowExW(
          0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0,
          0, 200, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RANGE)), state->inst,
          nullptr);
      SendMessageW(state->range, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(tr(L"Limited", L"Limited")));
      SendMessageW(state->range, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(tr(L"Full", L"Full")));
      SendMessageW(state->range, CB_SETCURSEL, 1, 0);
      state->open = CreateWindowExW(
          0, L"BUTTON", tr(L"Open...", L"打开..."), WS_CHILD | WS_VISIBLE, 0, 0,
          0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_OPEN)),
          state->inst, nullptr);
      state->prev = CreateWindowExW(
          0, L"BUTTON", tr(L"< Prev", L"< 上一帧"), WS_CHILD | WS_VISIBLE, 0, 0,
          0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PREV)),
          state->inst, nullptr);
      state->next = CreateWindowExW(
          0, L"BUTTON", tr(L"Next >", L"下一帧 >"), WS_CHILD | WS_VISIBLE, 0, 0,
          0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_NEXT)),
          state->inst, nullptr);
      state->status = CreateWindowExW(
          0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS)),
          state->inst, nullptr);
      SetWindowTextW(state->width,
                     std::to_wstring(state->default_width).c_str());
      SetWindowTextW(state->height,
                     std::to_wstring(state->default_height).c_str());
      for (HWND control :
           {state->width, state->height, state->format, state->matrix,
            state->range, state->open, state->prev, state->next, state->status})
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()),
                     TRUE);
      state->updateStatus();
      return 0;
    }
    case WM_SIZE: {
      RECT rc;
      GetClientRect(hwnd, &rc);
      const int pad = px(6);
      const int row = pad;
      const int h = px(26);
      MoveWindow(state->open, pad, row, px(76), h, TRUE);
      MoveWindow(state->width, pad + px(82), row, px(70), h, TRUE);
      MoveWindow(state->height, pad + px(156), row, px(70), h, TRUE);
      MoveWindow(state->format, pad + px(230), row, px(110), px(200), TRUE);
      MoveWindow(state->matrix, pad + px(344), row, px(100), px(200), TRUE);
      MoveWindow(state->range, pad + px(448), row, px(80), px(200), TRUE);
      MoveWindow(state->prev, pad + px(532), row, px(70), h, TRUE);
      MoveWindow(state->next, pad + px(606), row, px(70), h, TRUE);
      MoveWindow(state->status, pad + px(680), row,
                 std::max(0, static_cast<int>(rc.right) - pad - px(680)), h,
                 TRUE);
      const int top = row + h + pad;
      MoveWindow(state->view.hwnd(), pad, top,
                 std::max(0, static_cast<int>(rc.right) - 2 * pad),
                 std::max(0, static_cast<int>(rc.bottom) - top - pad), TRUE);
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(wparam) == IDC_OPEN) {
        state->openFile();
        return 0;
      }
      if (LOWORD(wparam) == IDC_PREV) {
        if (state->yuv.isOpen()) {
          state->frame = std::max(0, state->frame - 1);
          state->renderFrame();
        }
        return 0;
      }
      if (LOWORD(wparam) == IDC_NEXT) {
        if (state->yuv.isOpen()) {
          state->frame =
              std::min(state->yuv.frameCount() - 1, state->frame + 1);
          state->renderFrame();
        }
        return 0;
      }
      if ((LOWORD(wparam) == IDC_FORMAT || LOWORD(wparam) == IDC_MATRIX ||
           LOWORD(wparam) == IDC_RANGE) &&
          HIWORD(wparam) == CBN_SELCHANGE) {
        state->renderFrame();
        return 0;
      }
      break;
    case WM_DESTROY:
      delete state;
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

void YuvViewerWindow::open(HWND owner, HINSTANCE inst, int default_width,
                           int default_height) {
  auto* state = new State;
  state->inst = inst;
  state->default_width = std::max(2, default_width);
  state->default_height = std::max(2, default_height);
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = yuvProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);
  HWND hwnd = CreateWindowExW(
      WS_EX_APPWINDOW, kClassName, tr(L"YUV Viewer", L"YUV 查看器"),
      WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 720,
      owner, nullptr, inst, state);
  if (!hwnd) {
    delete state;
    return;
  }
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
}

}  // namespace ui

#else  // BS_HAVE_FFMPEG

namespace ui {
void YuvViewerWindow::open(HWND owner, HINSTANCE, int, int) {
  MessageBoxW(owner,
              tr(L"FFmpeg media layer not built", L"未构建 FFmpeg 媒体层"),
              tr(L"YUV Viewer", L"YUV 查看器"), MB_OK | MB_ICONINFORMATION);
}
}  // namespace ui

#endif
