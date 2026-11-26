#include "ui/compare_window.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "core/stream.h"
#include "media/media_file.h"
#include "ui/lang.h"
#include "ui/ui_util.h"
#include "ui/video_view.h"

namespace ui {

namespace {
const wchar_t kClassName[] = L"BsCompareWindow";
constexpr int IDC_LEFT = 3101;
constexpr int IDC_RIGHT = 3102;
constexpr int IDC_PREV = 3103;
constexpr int IDC_NEXT = 3104;
constexpr int IDC_STATUS = 3105;

struct State {
  HINSTANCE inst = nullptr;
  HWND hwnd = nullptr;
  HWND prev = nullptr;
  HWND next = nullptr;
  HWND status = nullptr;
  VideoView left_view;
  VideoView right_view;
  bs::Stream left;
  bs::Stream right;
  std::unique_ptr<bs::MediaFile> left_media;
  std::unique_ptr<bs::MediaFile> right_media;
  std::vector<int> left_order;
  std::vector<int> right_order;
  int frame = 0;

  ~State() = default;

  bool load(const std::wstring& path, bs::Stream& stream,
            std::unique_ptr<bs::MediaFile>& media, std::string& error) {
    auto candidate = std::make_unique<bs::MediaFile>();
    std::vector<uint8_t> es;
    if (!candidate->open(utf8(path), error)) return false;
    if (!candidate->extractAnnexB(es, error)) return false;
    if (!stream.loadBuffer(std::move(es), error)) return false;
    candidate->applyTiming(stream);
    media = std::move(candidate);
    return true;
  }

  void updateStatus() {
    if (!status) return;
    const size_t count = std::min(left_order.size(), right_order.size());
    std::wstring text = tr(L"display frame ", L"显示帧 ") +
                        std::to_wstring(frame + 1) + L"/" +
                        std::to_wstring(count);
    if (frame >= 0 &&
        frame < static_cast<int>(
                    std::min(left.frames().size(), right.frames().size()))) {
      const auto& a = left.frames()[static_cast<size_t>(left_order[frame])];
      const auto& b = right.frames()[static_cast<size_t>(right_order[frame])];
      text +=
          L"   POC " + std::to_wstring(a.poc) + L" / " + std::to_wstring(b.poc);
    }
    SetWindowTextW(status, text.c_str());
  }

  void showFrame() {
    const size_t count = std::min(left_order.size(), right_order.size());
    if (count == 0) return;
    frame = std::max(0, std::min(frame, static_cast<int>(count) - 1));
    auto render = [&](bs::Stream& stream, bs::MediaFile* media,
                      std::vector<int>& order, VideoView& view,
                      const wchar_t* fallback) {
      if (!media || frame >= static_cast<int>(order.size())) return;
      bs::DecodedFrame decoded;
      bool keyframe_only = false;
      std::string error;
      if (!media->decodeFrameAt(stream, order[static_cast<size_t>(frame)],
                                decoded, keyframe_only, error)) {
        view.clear(wide(error));
        return;
      }
      ImageData image;
      image.bgra = std::move(decoded.bgra);
      image.width = decoded.width;
      image.height = decoded.height;
      image.bit_depth = decoded.bit_depth;
      image.chroma_format = decoded.chroma_format;
      image.matrix = decoded.matrix;
      image.full_range = decoded.full_range;
      image.layout = decoded.layout;
      image.planes = std::move(decoded.planes);
      image.strides = std::move(decoded.strides);
      view.setImage(std::move(image), fallback);
    };
    render(left, left_media.get(), left_order, left_view,
           tr(L"left stream", L"左侧码流"));
    render(right, right_media.get(), right_order, right_view,
           tr(L"right stream", L"右侧码流"));
    updateStatus();
  }
};

LRESULT CALLBACK compareProc(HWND hwnd, UINT msg, WPARAM wparam,
                             LPARAM lparam) {
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
    case WM_CREATE:
      state->hwnd = hwnd;
      state->left_view.create(hwnd, state->inst, IDC_LEFT);
      state->right_view.create(hwnd, state->inst, IDC_RIGHT);
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
      SendMessageW(state->prev, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()),
                   TRUE);
      SendMessageW(state->next, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()),
                   TRUE);
      SendMessageW(state->status, WM_SETFONT,
                   reinterpret_cast<WPARAM>(uiFont()), TRUE);
      state->showFrame();
      return 0;
    case WM_SIZE: {
      RECT rc;
      GetClientRect(hwnd, &rc);
      const int pad = px(6);
      const int button_h = px(28);
      MoveWindow(state->prev, pad, pad, px(80), button_h, TRUE);
      MoveWindow(state->next, pad + px(86), pad, px(80), button_h, TRUE);
      MoveWindow(state->status, pad + px(172), pad,
                 std::max(0, static_cast<int>(rc.right) - pad - px(172)),
                 button_h, TRUE);
      const int top = pad + button_h + pad;
      const int height = std::max(0, static_cast<int>(rc.bottom) - top - pad);
      const int width = std::max(0, (static_cast<int>(rc.right) - 3 * pad) / 2);
      MoveWindow(state->left_view.hwnd(), pad, top, width, height, TRUE);
      MoveWindow(state->right_view.hwnd(), 2 * pad + width, top, width, height,
                 TRUE);
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(wparam) == IDC_PREV) {
        state->frame--;
        state->showFrame();
        return 0;
      }
      if (LOWORD(wparam) == IDC_NEXT) {
        state->frame++;
        state->showFrame();
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

std::vector<int> displayOrder(const bs::Stream& stream) {
  std::vector<int> order(stream.frames().size());
  for (size_t i = 0; i < order.size(); i++) order[i] = static_cast<int>(i);
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    return stream.frames()[static_cast<size_t>(a)].display_index <
           stream.frames()[static_cast<size_t>(b)].display_index;
  });
  return order;
}

}  // namespace

void CompareWindow::open(HWND owner, HINSTANCE inst, const std::wstring& first,
                         const std::wstring& second) {
  auto* state = new State;
  state->inst = inst;
  std::string error;
  if (!state->load(first, state->left, state->left_media, error) ||
      !state->load(second, state->right, state->right_media, error)) {
    MessageBoxW(owner, wide(error).c_str(), tr(L"Compare failed", L"对比失败"),
                MB_OK | MB_ICONERROR);
    delete state;
    return;
  }
  state->left_order = displayOrder(state->left);
  state->right_order = displayOrder(state->right);
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = compareProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);
  HWND hwnd = CreateWindowExW(
      WS_EX_APPWINDOW, kClassName, tr(L"Compare Streams", L"码流对比"),
      WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 720,
      owner, nullptr, inst, state);
  if (!hwnd) {
    delete state;
    return;
  }
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
}

}  // namespace ui
