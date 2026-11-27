#include "ui/compare_window.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <commctrl.h>
#include <commdlg.h>

#include "core/quality.h"
#include "core/stream.h"
#include "ui/lang.h"
#ifdef BS_HAVE_FFMPEG
#include "media/media_file.h"
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
constexpr int IDC_SYNC = 3106;
constexpr int IDC_DIFF = 3107;
constexpr int IDC_PSNR = 3108;
constexpr int IDC_ADD = 3109;
constexpr int IDC_EXTRA_BASE = 3110;

std::vector<int> displayOrder(const bs::Stream& stream);
std::vector<int> epochOrder(const bs::Stream& stream,
                            const std::vector<int>& order);

enum class SyncMode { Frame, Poc, Time };
enum class ViewMode {
  SideBySide,
  Subtraction,
  Temperature,
  PsnrClip,
  SplitH,
  SplitV
};

void makeSubtractImage(const bs::YuvFrameView& a, const bs::YuvFrameView& b,
                       bool clip, std::vector<uint8_t>& bgra) {
  const int width = a.planes[0].width;
  const int height = a.planes[0].height;
  bgra.resize(static_cast<size_t>(width) * height * 4);
  const double scale = a.bit_depth > 8 ? 2.0 : 8.0;
  for (int y = 0; y < height; y++) {
    const uint8_t* row_a =
        a.planes[0].data + static_cast<size_t>(y) * a.planes[0].stride;
    const uint8_t* row_b =
        b.planes[0].data + static_cast<size_t>(y) * b.planes[0].stride;
    for (int x = 0; x < width; x++) {
      int va = 0;
      int vb = 0;
      if (a.bit_depth > 8) {
        va = row_a[x * 2] | (row_a[x * 2 + 1] << 8);
        vb = row_b[x * 2] | (row_b[x * 2 + 1] << 8);
      } else {
        va = row_a[x];
        vb = row_b[x];
      }
      const int value =
          std::min(255, static_cast<int>(std::abs(va - vb) * scale));
      const size_t at = (static_cast<size_t>(y) * width + x) * 4;
      if (clip) {
        const bool hot = value >= 32;
        bgra[at + 0] = 0;
        bgra[at + 1] = 0;
        bgra[at + 2] = hot ? 255 : 0;
      } else {
        bgra[at + 0] = static_cast<uint8_t>(value);
        bgra[at + 1] = static_cast<uint8_t>(value);
        bgra[at + 2] = static_cast<uint8_t>(value);
      }
      bgra[at + 3] = 255;
    }
  }
}

void makeSplitImage(const ImageData& left, const ImageData& right,
                    bool vertical, std::vector<uint8_t>& bgra, int& width,
                    int& height) {
  width = std::min(left.width, right.width);
  height = std::min(left.height, right.height);
  bgra.resize(static_cast<size_t>(width) * height * 4);
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      const bool use_left = vertical ? (x < width / 2) : (y < height / 2);
      const ImageData& source = use_left ? left : right;
      const size_t src = (static_cast<size_t>(y) * source.width + x) * 4;
      const size_t dst = (static_cast<size_t>(y) * width + x) * 4;
      std::memcpy(&bgra[dst], &source.bgra[src], 4);
    }
  }
}

struct ExtraSide {
  bs::Stream stream;
  std::unique_ptr<bs::MediaFile> media;
  std::vector<int> order;
  std::vector<int> epoch;
  VideoView view;
  std::wstring path;
};

struct State {
  HINSTANCE inst = nullptr;
  HWND hwnd = nullptr;
  HWND prev = nullptr;
  HWND next = nullptr;
  HWND status = nullptr;
  HWND sync = nullptr;
  HWND mode = nullptr;
  HWND psnr_box = nullptr;
  HWND add_button = nullptr;
  std::vector<std::unique_ptr<ExtraSide>> extra;
  SyncMode sync_mode = SyncMode::Frame;
  ViewMode view_mode = ViewMode::SideBySide;
  VideoView left_view;
  VideoView right_view;
  bs::Stream left;
  bs::Stream right;
  std::unique_ptr<bs::MediaFile> left_media;
  std::unique_ptr<bs::MediaFile> right_media;
  std::vector<int> left_order;
  std::vector<int> right_order;
  std::vector<int> left_epoch;
  std::vector<int> right_epoch;
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

  int indexForFrame(const bs::Stream& target, const std::vector<int>& order,
                    const std::vector<int>& epoch, int left_display) const {
    if (left_display < 0 || left_display >= static_cast<int>(left_order.size()))
      return -1;
    const int left_frame = left_order[static_cast<size_t>(left_display)];
    const auto& frame = left.frames()[static_cast<size_t>(left_frame)];
    if (sync_mode == SyncMode::Frame) {
      if (left_display >= static_cast<int>(order.size())) return -1;
      return order[static_cast<size_t>(left_display)];
    }
    if (sync_mode == SyncMode::Poc) {
      const int want_epoch = left_epoch[static_cast<size_t>(left_display)];
      for (size_t i = 0; i < order.size(); i++) {
        const int candidate = order[i];
        if (target.frames()[static_cast<size_t>(candidate)].poc != frame.poc)
          continue;
        if (epoch[i] == want_epoch) return candidate;
      }
      for (size_t i = 0; i < order.size(); i++) {
        const int candidate = order[i];
        if (target.frames()[static_cast<size_t>(candidate)].poc == frame.poc)
          return candidate;
      }
      return -1;
    }
    if (!frame.has_timing) return -1;
    int best = -1;
    double best_delta = 1e9;
    for (size_t i = 0; i < order.size(); i++) {
      const int candidate_index = order[i];
      const auto& candidate =
          target.frames()[static_cast<size_t>(candidate_index)];
      if (!candidate.has_timing) continue;
      const double delta = std::fabs(candidate.time_sec - frame.time_sec);
      if (delta < best_delta) {
        best_delta = delta;
        best = candidate_index;
      }
    }
    return best;
  }

  int rightIndexForFrame(int left_display) const {
    return indexForFrame(right, right_order, right_epoch, left_display);
  }

  void updateStatus(int right_frame) {
    if (!status) return;
    const size_t count = left_order.size();
    std::wstring text = tr(L"display frame ", L"显示帧 ") +
                        std::to_wstring(frame + 1) + L"/" +
                        std::to_wstring(count);
    if (frame >= 0 && frame < static_cast<int>(left_order.size())) {
      const auto& a = left.frames()[static_cast<size_t>(left_order[frame])];
      text += L"   POC " + std::to_wstring(a.poc);
      if (right_frame >= 0 &&
          right_frame < static_cast<int>(right.frames().size())) {
        const auto& b = right.frames()[static_cast<size_t>(right_frame)];
        text += L" / " + std::to_wstring(b.poc);
      } else {
        text += tr(L" / no match", L" / 无匹配");
      }
    }
    SetWindowTextW(status, text.c_str());
  }

  bool render(bs::Stream& stream, bs::MediaFile* media, int frame_index,
              ImageData& image, bool& keyframe_only, std::string& error) {
    if (!media || frame_index < 0) return false;
    bs::DecodedFrame decoded;
    if (!media->decodeFrameAt(stream, frame_index, decoded, keyframe_only,
                              error))
      return false;
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
    return true;
  }

  void showFrame() {
    const size_t count = left_order.size();
    if (count == 0) return;
    frame = std::max(0, std::min(frame, static_cast<int>(count) - 1));
    const int left_frame = left_order[static_cast<size_t>(frame)];
    const int right_frame = rightIndexForFrame(frame);
    ImageData left_image;
    ImageData right_image;
    bool keyframe_only = false;
    std::string error;
    if (render(left, left_media.get(), left_frame, left_image, keyframe_only,
               error))
      left_view.setImage(std::move(left_image),
                         tr(L"left stream", L"左侧码流"));
    else
      left_view.clear(wide(error));
    if (right_frame < 0) {
      right_view.clear(tr(L"no matching frame", L"无匹配帧"));
    } else if (render(right, right_media.get(), right_frame, right_image,
                      keyframe_only, error)) {
      if (view_mode != ViewMode::SideBySide) {
        bs::QualityMetrics metrics;
        std::string quality_error;
        const size_t bytes = right_image.bit_depth > 8 ? 2 : 1;
        bs::YuvFrameView a;
        bs::YuvFrameView b;
        a.bit_depth = left_image.bit_depth;
        a.chroma_format = left_image.chroma_format;
        b.bit_depth = right_image.bit_depth;
        b.chroma_format = right_image.chroma_format;
        const int chroma = left_image.chroma_format;
        for (int p = 0; p < 3; p++) {
          const int pw = p == 0 ? left_image.width
                                : (chroma == 3 ? left_image.width
                                               : (left_image.width + 1) / 2);
          const int ph = p == 0 ? left_image.height
                                : (chroma == 1 ? (left_image.height + 1) / 2
                                               : left_image.height);
          a.planes[p] = {left_image.planes[static_cast<size_t>(p)].data(),
                         pw * static_cast<int>(bytes), pw, ph};
          const size_t other_bytes = right_image.bit_depth > 8 ? 2 : 1;
          b.planes[p] = {right_image.planes[static_cast<size_t>(p)].data(),
                         pw * static_cast<int>(other_bytes), pw, ph};
        }
        if (left_image.width == right_image.width &&
            left_image.height == right_image.height &&
            left_image.bit_depth == right_image.bit_depth &&
            left_image.chroma_format == right_image.chroma_format &&
            bs::computeQuality(a, b, metrics, quality_error)) {
          wchar_t text[96];
          swprintf(text, 96, L"PSNR %.2f dB  SSIM %.4f", metrics.psnr_avg,
                   metrics.ssim);
          SetWindowTextW(psnr_box, text);
          std::vector<uint8_t> output;
          int output_w = 0;
          int output_h = 0;
          if (view_mode == ViewMode::Temperature) {
            bs::makeDiffImage(a, b, output);
            output_w = left_image.width;
            output_h = left_image.height;
          } else if (view_mode == ViewMode::Subtraction ||
                     view_mode == ViewMode::PsnrClip) {
            makeSubtractImage(a, b, view_mode == ViewMode::PsnrClip, output);
            output_w = left_image.width;
            output_h = left_image.height;
          } else {
            makeSplitImage(left_image, right_image,
                           view_mode == ViewMode::SplitV, output, output_w,
                           output_h);
          }
          ImageData result_image;
          result_image.bgra = std::move(output);
          result_image.width = output_w;
          result_image.height = output_h;
          right_view.setImage(std::move(result_image),
                              tr(L"difference", L"差异"));
        } else {
          SetWindowTextW(psnr_box, L"-");
          right_view.setImage(std::move(right_image),
                              tr(L"right stream", L"右侧码流"));
        }
      } else {
        SetWindowTextW(psnr_box, L"-");
        right_view.setImage(std::move(right_image),
                            tr(L"right stream", L"右侧码流"));
      }
    } else {
      right_view.clear(wide(error));
    }
    for (size_t i = 0; i < extra.size(); i++) {
      ExtraSide& side = *extra[i];
      const int index =
          indexForFrame(side.stream, side.order, side.epoch, frame);
      if (index < 0) {
        side.view.clear(tr(L"no matching frame", L"无匹配帧"));
        continue;
      }
      ImageData image;
      if (render(side.stream, side.media.get(), index, image, keyframe_only,
                 error)) {
        const std::wstring label =
            tr(L"stream ", L"码流 ") + std::to_wstring(i + 3);
        side.view.setImage(std::move(image), label);
      } else {
        side.view.clear(wide(error));
      }
    }
    updateStatus(right_frame);
    if (!extra.empty() && status) {
      std::wstring text(512, L'\0');
      const int len = GetWindowTextW(status, &text[0], 512);
      text.resize(static_cast<size_t>(std::max(0, len)));
      for (size_t i = 0; i < extra.size(); i++) {
        ExtraSide& side = *extra[i];
        const int index =
            indexForFrame(side.stream, side.order, side.epoch, frame);
        text += L" / ";
        if (index >= 0)
          text += L"POC " +
                  std::to_wstring(
                      side.stream.frames()[static_cast<size_t>(index)].poc);
        else
          text += L"-";
      }
      SetWindowTextW(status, text.c_str());
    }
  }

  void addStream() {
    if (extra.size() >= 2) {
      MessageBoxW(hwnd,
                  tr(L"At most four streams can be compared.",
                     L"最多只能对比四路码流。"),
                  tr(L"Compare Streams", L"码流对比"),
                  MB_OK | MB_ICONINFORMATION);
      return;
    }
    wchar_t file[32768] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = tr(L"Media files (*.*)\0*.*\0All files (*.*)\0*.*\0",
                         L"媒体文件 (*.*)\0*.*\0所有文件 (*.*)\0*.*\0");
    ofn.lpstrFile = file;
    ofn.nMaxFile = 32768;
    ofn.lpstrTitle = tr(L"Add a stream to compare", L"添加对比码流");
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return;
    auto side = std::make_unique<ExtraSide>();
    std::string error;
    if (!load(file, side->stream, side->media, error)) {
      MessageBoxW(hwnd, wide(error).c_str(), tr(L"Compare failed", L"对比失败"),
                  MB_OK | MB_ICONERROR);
      return;
    }
    side->order = displayOrder(side->stream);
    side->epoch = epochOrder(side->stream, side->order);
    side->path = file;
    side->view.create(hwnd, inst,
                      IDC_EXTRA_BASE + static_cast<int>(extra.size()));
    extra.push_back(std::move(side));
    SendMessageW(hwnd, WM_SIZE, 0, 0);
    showFrame();
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
    case WM_CREATE: {
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
      state->sync = CreateWindowExW(
          0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0,
          0, 200, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SYNC)),
          state->inst, nullptr);
      SendMessageW(state->sync, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(tr(L"Sync: Frame", L"同步：帧序")));
      SendMessageW(state->sync, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(tr(L"Sync: POC", L"同步：POC")));
      SendMessageW(state->sync, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(tr(L"Sync: Time", L"同步：时间")));
      SendMessageW(state->sync, CB_SETCURSEL, 0, 0);
      state->mode = CreateWindowExW(
          0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0,
          0, 220, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DIFF)),
          state->inst, nullptr);
      const wchar_t* modes[] = {tr(L"Side by side", L"并排"),
                                tr(L"Subtraction", L"相减"),
                                tr(L"Temperature", L"温度图"),
                                tr(L"PSNR clip", L"PSNR 裁剪"),
                                tr(L"Horizontal split", L"水平分割"),
                                tr(L"Vertical split", L"垂直分割")};
      for (const wchar_t* item : modes)
        SendMessageW(state->mode, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(item));
      SendMessageW(state->mode, CB_SETCURSEL, 0, 0);
      state->psnr_box = CreateWindowExW(
          0, L"STATIC", L"-", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PSNR)), state->inst,
          nullptr);
      state->add_button = CreateWindowExW(
          0, L"BUTTON", tr(L"Add Stream...", L"添加码流..."),
          WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_ADD)), state->inst,
          nullptr);
      SendMessageW(state->sync, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()),
                   TRUE);
      SendMessageW(state->mode, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()),
                   TRUE);
      SendMessageW(state->psnr_box, WM_SETFONT,
                   reinterpret_cast<WPARAM>(uiFont()), TRUE);
      SendMessageW(state->prev, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()),
                   TRUE);
      SendMessageW(state->next, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()),
                   TRUE);
      SendMessageW(state->status, WM_SETFONT,
                   reinterpret_cast<WPARAM>(uiFont()), TRUE);
      SendMessageW(state->add_button, WM_SETFONT,
                   reinterpret_cast<WPARAM>(uiFont()), TRUE);
      state->showFrame();
      return 0;
    }
    case WM_SIZE: {
      RECT rc;
      GetClientRect(hwnd, &rc);
      const int pad = px(6);
      const int button_h = px(28);
      MoveWindow(state->prev, pad, pad, px(80), button_h, TRUE);
      MoveWindow(state->next, pad + px(86), pad, px(80), button_h, TRUE);
      MoveWindow(state->sync, pad + px(172), pad, px(130), px(200), TRUE);
      MoveWindow(state->mode, pad + px(308), pad, px(150), px(220), TRUE);
      MoveWindow(state->psnr_box, pad + px(464), pad, px(140), button_h, TRUE);
      MoveWindow(state->add_button, pad + px(610), pad, px(110), button_h,
                 TRUE);
      MoveWindow(state->status, pad + px(726), pad,
                 std::max(0, static_cast<int>(rc.right) - pad - px(726)),
                 button_h, TRUE);
      const int top = pad + button_h + pad;
      const int height = std::max(0, static_cast<int>(rc.bottom) - top - pad);
      const int count = 2 + static_cast<int>(state->extra.size());
      const int columns = 2;
      const int rows = (count + columns - 1) / columns;
      const int cell_w = std::max(
          0, (static_cast<int>(rc.right) - (columns + 1) * pad) / columns);
      const int cell_h =
          std::max(0, (height - (rows - 1) * pad) / std::max(1, rows));
      auto place = [&](VideoView& view, int index) {
        const int column = index % columns;
        const int row = index / columns;
        MoveWindow(view.hwnd(), pad + column * (cell_w + pad),
                   top + row * (cell_h + pad), cell_w, cell_h, TRUE);
      };
      place(state->left_view, 0);
      place(state->right_view, 1);
      for (size_t i = 0; i < state->extra.size(); i++)
        place(state->extra[i]->view, 2 + static_cast<int>(i));
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
      if (LOWORD(wparam) == IDC_ADD) {
        state->addStream();
        return 0;
      }
      if (LOWORD(wparam) == IDC_SYNC && HIWORD(wparam) == CBN_SELCHANGE) {
        const int sel =
            static_cast<int>(SendMessageW(state->sync, CB_GETCURSEL, 0, 0));
        state->sync_mode = sel == 1   ? SyncMode::Poc
                           : sel == 2 ? SyncMode::Time
                                      : SyncMode::Frame;
        state->showFrame();
        return 0;
      }
      if (LOWORD(wparam) == IDC_DIFF && HIWORD(wparam) == CBN_SELCHANGE) {
        const int sel =
            static_cast<int>(SendMessageW(state->mode, CB_GETCURSEL, 0, 0));
        state->view_mode = sel == 1   ? ViewMode::Subtraction
                           : sel == 2 ? ViewMode::Temperature
                           : sel == 3 ? ViewMode::PsnrClip
                           : sel == 4 ? ViewMode::SplitH
                           : sel == 5 ? ViewMode::SplitV
                                      : ViewMode::SideBySide;
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

std::vector<int> epochOrder(const bs::Stream& stream,
                            const std::vector<int>& order) {
  std::vector<int> epochs(order.size());
  int epoch = 0;
  for (size_t i = 0; i < order.size(); i++) {
    const auto& frame = stream.frames()[static_cast<size_t>(order[i])];
    if (i > 0 && (frame.idr || frame.irap)) epoch++;
    epochs[i] = epoch;
  }
  return epochs;
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
  state->left_epoch = epochOrder(state->left, state->left_order);
  state->right_epoch = epochOrder(state->right, state->right_order);
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

#endif  // BS_HAVE_FFMPEG

#ifndef BS_HAVE_FFMPEG
namespace ui {
void CompareWindow::open(HWND owner, HINSTANCE, const std::wstring&,
                         const std::wstring&) {
  MessageBoxW(owner,
              tr(L"FFmpeg media layer not built", L"未构建 FFmpeg 媒体层"),
              tr(L"Compare", L"对比"), MB_OK | MB_ICONINFORMATION);
}
}  // namespace ui
#endif
