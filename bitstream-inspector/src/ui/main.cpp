#include <windows.h>

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "core/conformance.h"
#include "core/stream.h"
#include "ui/chart_view.h"
#include "ui/compare_window.h"
#include "ui/frame_list.h"
#include "ui/hex_view.h"
#include "ui/image_export.h"
#include "ui/lang.h"
#include "ui/refs_view.h"
#include "ui/syntax_tree.h"
#include "ui/thumbnail_strip.h"
#include "ui/ui_util.h"
#include "ui/video_view.h"
#ifdef BS_HAVE_FFMPEG
#include "media/media_file.h"
#endif

using namespace bs;
using namespace ui;

namespace {

enum : int {
  IDC_BTN_OPEN = 1001,
  IDC_BTN_PREV = 1002,
  IDC_BTN_NEXT = 1003,
  IDC_INFO = 1004,
  IDC_BTN_PREV_KEY = 1005,
  IDC_BTN_NEXT_KEY = 1006,
  IDC_FRAME_LIST = 1010,
  IDC_SYNTAX_TREE = 1011,
  IDC_HEX_VIEW = 1012,
  IDC_CHART = 1013,
  IDC_TAB = 1014,
  IDC_VIDEO_VIEW = 1015,
  IDC_THUMBNAILS = 1016,
  IDC_REFS_VIEW = 1017,

  IDM_FILE_OPEN = 2001,
  IDM_FILE_EXIT = 2002,
  IDM_FILE_EXPORT_FRAMES = 2003,
  IDM_FILE_EXPORT_NALS = 2004,
  IDM_FILE_EXPORT_CPB = 2005,
  IDM_FILE_EXPORT_IMAGE = 2006,
  IDM_FILE_EXPORT_YUV = 2007,
  IDM_FILE_COMPARE = 2010,
  IDM_VIEW_PREV = 2101,
  IDM_VIEW_NEXT = 2102,
  IDM_VIEW_RELOAD = 2103,
  IDM_VIEW_PREV_KEY = 2104,
  IDM_VIEW_NEXT_KEY = 2105,
  IDM_VIEW_CHART_SIZE = 2106,
  IDM_VIEW_CHART_CPB = 2107,
  IDM_VIEW_CHART_BITS = 2117,
  IDM_VIEW_OVERLAY = 2118,
  IDM_VIEW_CONFORMANCE = 2119,
  IDM_VIEW_MATRIX_AUTO = 2108,
  IDM_VIEW_MATRIX_601 = 2109,
  IDM_VIEW_MATRIX_709 = 2110,
  IDM_VIEW_MATRIX_2020 = 2111,
  IDM_VIEW_RANGE_AUTO = 2112,
  IDM_VIEW_RANGE_LIMITED = 2113,
  IDM_VIEW_RANGE_FULL = 2114,
  IDM_VIEW_PREV_GOP = 2115,
  IDM_VIEW_NEXT_GOP = 2116,
  IDM_HELP_ABOUT = 2201,
  IDM_LANG_EN = 2301,
  IDM_LANG_ZH = 2302,
};

const wchar_t kWindowClass[] = L"BsInspectorMain";
const wchar_t kTitle[] = L"Bitstream Inspector";
const wchar_t kFilterEn[] =
    L"Media streams (*.mp4;*.mov;*.m4v;*.ts;*.m2ts;*.mts;*.mkv;*.webm;"
    L"*.h264;*.264;*.avc;*.h265;*.265;*.hevc)\0"
    L"*.mp4;*.mov;*.m4v;*.ts;*.m2ts;*.mts;*.mkv;*.webm;"
    L"*.h264;*.264;*.avc;*.h265;*.265;*.hevc\0"
    L"Annex B elementary streams\0*.h264;*.264;*.avc;*.h265;*.265;*.hevc\0"
    L"All files (*.*)\0*.*\0";
const wchar_t kFilterZh[] =
    L"媒体文件 (*.mp4;*.mov;*.m4v;*.ts;*.m2ts;*.mts;*.mkv;*.webm;"
    L"*.h264;*.264;*.avc;*.h265;*.265;*.hevc)\0"
    L"*.mp4;*.mov;*.m4v;*.ts;*.m2ts;*.mts;*.mkv;*.webm;"
    L"*.h264;*.264;*.avc;*.h265;*.265;*.hevc\0"
    L"裸流 (*.h264;*.264;*.avc;*.h265;*.265;*.hevc)\0"
    L"*.h264;*.264;*.avc;*.h265;*.265;*.hevc\0"
    L"所有文件 (*.*)\0*.*\0";

const wchar_t* filterForLanguage() {
  return language() == Lang::Zh ? kFilterZh : kFilterEn;
}

struct App {
  HINSTANCE inst = nullptr;
  HWND hwnd = nullptr;
  HWND btn_open = nullptr;
  HWND btn_prev = nullptr;
  HWND btn_next = nullptr;
  HWND btn_prev_key = nullptr;
  HWND btn_next_key = nullptr;
  HWND info = nullptr;
  HWND status = nullptr;
  FrameList frame_list;
  SyntaxTree syntax_tree;
  HexView hex_view;
  VideoView video_view;
  RefsView refs_view;
  ChartView chart;
  ThumbnailStrip thumbnails;
  HWND tabs = nullptr;
  int active_tab = 1;  // 0 = Hex, 1 = Video
  int video_frame = -1;
  // Background decode worker.
  HANDLE video_thread = nullptr;
  HANDLE video_event = nullptr;
  HANDLE video_stop_event = nullptr;
  CRITICAL_SECTION video_lock = {};
  bool video_lock_ready = false;
  int video_request = -1;
  int video_request_gen = 0;
  std::atomic<int> video_generation{0};
  // Background thumbnail worker.
  HANDLE thumb_thread = nullptr;
  HANDLE thumb_event = nullptr;
  HANDLE thumb_stop_event = nullptr;
  CRITICAL_SECTION thumb_lock = {};
  bool thumb_lock_ready = false;
  std::deque<std::pair<int, int>> thumb_queue;  // frame, generation
  int thumb_inflight = -1;
  std::atomic<int> thumb_generation{0};
#ifdef BS_HAVE_FFMPEG
  std::unique_ptr<MediaFile> media;
  std::unique_ptr<MediaFile> thumb_media;
  ContainerInfo container;
#endif
  Stream stream;
  std::wstring path;
  int current_frame = -1;
  bool loaded = false;
  // Layout ratios and splitter drag state.
  float left_ratio = 0.43f;
  float tree_ratio = 0.55f;
  int preview_matrix = -1;  // -1 = VUI/auto
  int preview_range = -1;   // -1 = VUI, 0 = limited, 1 = full
  bool show_overlay = false;
  int drag = 0;
  int drag_offset = 0;
  RECT split_left = {};
  RECT split_mid = {};
  int content_top = 0;
  int list_height = 0;
};

App g_app;

constexpr UINT WM_APP_VIDEO_READY = WM_APP + 20;
constexpr UINT WM_APP_THUMB_READY = WM_APP + 22;

struct VideoResult {
  ImageData image;
  int frame = -1;
  int generation = -1;
  bool keyframe_only = false;
  bool ok = false;
  std::string error;
};

struct ThumbResult {
  std::vector<uint8_t> bgra;
  int width = 0;
  int height = 0;
  int frame = -1;
  int generation = -1;
  bool ok = false;
  std::string error;
};

void requestThumbnail(int frame);
void startThumbWorker();
void stopThumbWorker();

void requestVideo(int frame);
void startVideoWorker();
void stopVideoWorker();

std::wstring baseName(const std::wstring& path) {
  size_t pos = path.find_last_of(L"\\/");
  return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

bool isKeyframe(const FrameInfo& frame) { return frame.idr || frame.irap; }

int findKeyframe(const std::vector<FrameInfo>& frames, int from,
                 int direction) {
  for (int i = from + direction; i >= 0 && i < static_cast<int>(frames.size());
       i += direction) {
    if (isKeyframe(frames[static_cast<size_t>(i)])) return i;
  }
  return -1;
}

std::wstring shortNalName(const Stream& stream, const NalEntry& e) {
  int type = e.unit.nal_type;
  if (stream.codec() == Codec::H264) {
    if (type >= 1 && type <= 5) return L"Slice";
    switch (type) {
      case 6:
        return L"SEI";
      case 7:
        return L"SPS";
      case 8:
        return L"PPS";
      case 9:
        return L"AUD";
      case 10:
        return L"EOS";
      case 11:
        return L"EOB";
      case 12:
        return L"Filler";
      default:
        break;
    }
  } else {
    if (type <= 31) return L"Slice";
    switch (type) {
      case 32:
        return L"VPS";
      case 33:
        return L"SPS";
      case 34:
        return L"PPS";
      case 35:
        return L"AUD";
      case 36:
        return L"EOS";
      case 37:
        return L"EOB";
      case 38:
        return L"Filler";
      case 39:
        return L"Prefix SEI";
      case 40:
        return L"Suffix SEI";
      default:
        break;
    }
  }
  return wide(e.unit.type_name);
}

#ifdef BS_HAVE_FFMPEG
std::wstring containerLabel(const ContainerInfo& info) {
  std::wstring format = wide(info.format);
  if (format.find(L"mp4") != std::wstring::npos ||
      format.find(L"mov") != std::wstring::npos)
    format = L"MP4";
  else if (format.find(L"matroska") != std::wstring::npos ||
           format.find(L"webm") != std::wstring::npos)
    format = L"MKV";
  else if (format.find(L"mpegts") != std::wstring::npos)
    format = L"MPEG-TS";
  else if (format == L"h264")
    format = L"H.264 ES";
  else if (format == L"hevc")
    format = L"HEVC ES";
  if (format.empty()) format = tr(L"file", L"文件");
  if (info.duration_ms > 0)
    format += L"  " + std::to_wstring((info.duration_ms + 500) / 1000) +
              tr(L"s", L"秒");
  return format;
}
#endif

std::wstring streamSummaryText() {
  const Stream& stream = g_app.stream;
  std::wstring text = wide(codecName(stream.codec()));
  if (stream.codec() == Codec::H264 && stream.haveH264Params()) {
    const H264Sps& sps = stream.h264Sps();
    if (sps.width() > 0 && sps.height() > 0) {
      text += L" | " + std::to_wstring(sps.width()) + L"x" +
              std::to_wstring(sps.height());
      text += L" | " + wide(h264ProfileName(sps.profile_idc)) + L"@" +
              wide(h264LevelName(sps.level_idc));
      double fps = sps.fps();
      if (fps > 0.0) {
        wchar_t buf[32];
        swprintf(buf, 32, L" | %.3f fps", fps);
        text += buf;
      }
    }
  } else if (stream.codec() == Codec::H265 && stream.haveH265Params()) {
    const H265Sps& sps = stream.h265Sps();
    if (sps.display_width() > 0 && sps.display_height() > 0) {
      text += L" | " + std::to_wstring(sps.display_width()) + L"x" +
              std::to_wstring(sps.display_height());
      text += L" | " + wide(h265ProfileName(sps.ptl.profile_idc)) + L"@" +
              wide(h265LevelName(sps.ptl.level_idc));
      double fps = sps.fps();
      if (fps > 0.0) {
        wchar_t buf[32];
        swprintf(buf, 32, L" | %.3f fps", fps);
        text += buf;
      }
    }
  }
  std::string hdr = stream.hdrSummary();
  if (!hdr.empty()) text += L" | " + wide(hdr);
  text += L" | " + std::to_wstring(stream.nals().size()) +
          tr(L" NAL, ", L" NAL，") + std::to_wstring(stream.frames().size()) +
          tr(L" frames", L" 帧");
  return text;
}

std::wstring streamInfoText() {
  std::wstring summary = streamSummaryText();
#ifdef BS_HAVE_FFMPEG
  if (g_app.media)
    summary = containerLabel(g_app.media->info()) + L"  |  " + summary;
#endif
  return summary;
}

void showConformance() {
  if (!g_app.loaded) return;
  std::vector<ConformanceIssue> issues;
  const double rate = g_app.stream.windowBitrate(
      0, static_cast<int>(g_app.stream.frames().size()));
  if (g_app.stream.codec() == Codec::H264)
    issues = checkH264Conformance(g_app.stream.h264Sps(), g_app.stream.fps(),
                                  static_cast<uint64_t>(rate));
  else if (g_app.stream.codec() == Codec::H265)
    issues = checkH265Conformance(g_app.stream.h265Sps(), g_app.stream.fps(),
                                  static_cast<uint64_t>(rate));
  std::wstring text;
  if (issues.empty()) {
    text = tr(L"No Annex A violations detected.", L"未发现 Annex A 限制违规。");
  } else {
    for (const auto& issue : issues)
      text += wide(issue.code) + L": " + wide(issue.message) + L"\n";
  }
  MessageBoxW(g_app.hwnd, text.c_str(), tr(L"Conformance Report", L"合规检查"),
              MB_OK | (issues.empty() ? MB_ICONINFORMATION : MB_ICONWARNING));
}

void applyPreviewColour() {
#ifdef BS_HAVE_FFMPEG
  if (!g_app.media) return;
  int matrix = g_app.preview_matrix;
  if (matrix < 0) {
    matrix = g_app.stream.colourDescription().present
                 ? g_app.stream.colourDescription().matrix
                 : 1;
  }
  bool full_range = g_app.preview_range < 0 ? g_app.stream.videoFullRange()
                                            : g_app.preview_range == 1;
  if (g_app.preview_matrix < 0 && g_app.preview_range < 0)
    g_app.media->clearPreviewColour();
  else
    g_app.media->setPreviewColour(matrix, full_range);
  g_app.video_frame = -1;
  if (g_app.current_frame >= 0) requestVideo(g_app.current_frame);
#endif
}

void createMenu(HWND hwnd) {
  HMENU menu = CreateMenu();
  HMENU file = CreatePopupMenu();
  AppendMenuW(file, MF_STRING, IDM_FILE_OPEN,
              tr(L"&Open...\tCtrl+O", L"打开(&O)...\tCtrl+O"));
  AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(file, MF_STRING, IDM_FILE_EXPORT_FRAMES,
              tr(L"Export &Frame List (CSV)...\tCtrl+E",
                 L"导出帧列表 CSV(&F)...\tCtrl+E"));
  AppendMenuW(file, MF_STRING, IDM_FILE_EXPORT_NALS,
              tr(L"Export &NAL List (CSV)...", L"导出 NAL 列表 CSV(&N)..."));
  AppendMenuW(file, MF_STRING, IDM_FILE_EXPORT_CPB,
              tr(L"Export &CPB Model (CSV)...", L"导出 CPB 模型 CSV(&C)..."));
  AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(
      file, MF_STRING, IDM_FILE_EXPORT_IMAGE,
      tr(L"Export Frame &Image (PNG/BMP)...", L"导出当前画面 PNG/BMP(&I)..."));
  AppendMenuW(file, MF_STRING, IDM_FILE_EXPORT_YUV,
              tr(L"Export Frame &YUV...", L"导出当前帧 YUV(&Y)..."));
  AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(file, MF_STRING, IDM_FILE_COMPARE,
              tr(L"&Compare Streams...", L"码流对比(&C)..."));
  AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(file, MF_STRING, IDM_FILE_EXIT, tr(L"E&xit", L"退出(&X)"));
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file),
              tr(L"&File", L"文件(&F)"));

  HMENU view = CreatePopupMenu();
  AppendMenuW(view, MF_STRING, IDM_VIEW_PREV,
              tr(L"&Previous Frame\tAlt+Left", L"上一帧(&P)\tAlt+Left"));
  AppendMenuW(view, MF_STRING, IDM_VIEW_NEXT,
              tr(L"&Next Frame\tAlt+Right", L"下一帧(&N)\tAlt+Right"));
  AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(
      view, MF_STRING, IDM_VIEW_PREV_KEY,
      tr(L"Previous &Keyframe\tShift+Left", L"上一个关键帧(&K)\tShift+Left"));
  AppendMenuW(
      view, MF_STRING, IDM_VIEW_NEXT_KEY,
      tr(L"Next K&eyframe\tShift+Right", L"下一个关键帧(&E)\tShift+Right"));
  AppendMenuW(view, MF_STRING, IDM_VIEW_PREV_GOP,
              tr(L"Previous &GOP", L"上一个 GOP(&G)"));
  AppendMenuW(view, MF_STRING, IDM_VIEW_NEXT_GOP,
              tr(L"Next G&OP", L"下一个 GOP(&O)"));
  AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(view, MF_STRING, IDM_VIEW_CHART_SIZE,
              tr(L"Chart: &Frame Size", L"图表：帧大小(&F)"));
  AppendMenuW(view, MF_STRING, IDM_VIEW_CHART_CPB,
              tr(L"Chart: &CPB Occupancy", L"图表：CPB 占用(&C)"));
  AppendMenuW(view, MF_STRING, IDM_VIEW_CHART_BITS,
              tr(L"Chart: &Bit Allocation", L"图表：比特分配(&B)"));
  AppendMenuW(view, MF_STRING | (g_app.show_overlay ? MF_CHECKED : 0),
              IDM_VIEW_OVERLAY,
              tr(L"Slice/&Tile Overlay", L"Slice/Tile 叠加(&T)"));
  AppendMenuW(view, MF_STRING, IDM_VIEW_CONFORMANCE,
              tr(L"&Conformance Report...", L"合规检查(&C)..."));
  CheckMenuRadioItem(view, IDM_VIEW_CHART_SIZE, IDM_VIEW_CHART_CPB,
                     IDM_VIEW_CHART_SIZE, MF_BYCOMMAND);

  HMENU matrix = CreatePopupMenu();
  AppendMenuW(matrix, MF_STRING, IDM_VIEW_MATRIX_AUTO,
              tr(L"Auto (VUI)", L"自动（VUI）"));
  AppendMenuW(matrix, MF_STRING, IDM_VIEW_MATRIX_601, L"BT.601");
  AppendMenuW(matrix, MF_STRING, IDM_VIEW_MATRIX_709, L"BT.709");
  AppendMenuW(matrix, MF_STRING, IDM_VIEW_MATRIX_2020, L"BT.2020");
  AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(matrix),
              tr(L"Colour &Matrix", L"色彩矩阵(&M)"));

  HMENU range = CreatePopupMenu();
  AppendMenuW(range, MF_STRING, IDM_VIEW_RANGE_AUTO,
              tr(L"Auto (VUI)", L"自动（VUI）"));
  AppendMenuW(range, MF_STRING, IDM_VIEW_RANGE_LIMITED,
              tr(L"Limited", L"Limited"));
  AppendMenuW(range, MF_STRING, IDM_VIEW_RANGE_FULL, tr(L"Full", L"Full"));
  AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(range),
              tr(L"Colour &Range", L"色彩范围(&R)"));
  AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(view, MF_STRING, IDM_VIEW_RELOAD,
              tr(L"&Reload\tF5", L"重新加载(&R)\tF5"));
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view),
              tr(L"&View", L"视图(&V)"));

  HMENU lang = CreatePopupMenu();
  AppendMenuW(lang, MF_STRING | (language() == Lang::En ? MF_CHECKED : 0),
              IDM_LANG_EN, L"&English");
  AppendMenuW(lang, MF_STRING | (language() == Lang::Zh ? MF_CHECKED : 0),
              IDM_LANG_ZH, L"简体中文(&C)");
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(lang),
              tr(L"&Language", L"语言(&L)"));

  HMENU help = CreatePopupMenu();
  AppendMenuW(help, MF_STRING, IDM_HELP_ABOUT, tr(L"&About", L"关于(&A)"));
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help),
              tr(L"&Help", L"帮助(&H)"));

  SetMenu(hwnd, menu);
}

HWND makeButton(HWND parent, const wchar_t* text, int id) {
  HWND h = CreateWindowExW(
      0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
      parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_app.inst,
      nullptr);
  SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()), TRUE);
  return h;
}

bool readFileW(const std::wstring& path, std::vector<uint8_t>& out,
               std::string& error) {
  HANDLE file =
      CreateFileW(path.c_str(), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    error = "cannot open file";
    return false;
  }
  LARGE_INTEGER size = {};
  if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) {
    CloseHandle(file);
    error = "cannot determine file size";
    return false;
  }
  if (size.QuadPart > 512LL * 1024 * 1024) {
    CloseHandle(file);
    error = "file too large (limit 512 MiB for in-memory analysis)";
    return false;
  }
  out.resize(static_cast<size_t>(size.QuadPart));
  size_t total = 0;
  while (total < out.size()) {
    DWORD chunk =
        static_cast<DWORD>(std::min<size_t>(out.size() - total, 1u << 20));
    DWORD got = 0;
    if (!ReadFile(file, out.data() + total, chunk, &got, nullptr) || got == 0)
      break;
    total += got;
  }
  CloseHandle(file);
  if (total != out.size()) {
    error = "short read";
    return false;
  }
  return true;
}

bool writeFileW(const std::wstring& path, const std::string& data) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  size_t offset = 0;
  while (offset < data.size()) {
    DWORD chunk =
        static_cast<DWORD>(std::min<size_t>(data.size() - offset, 1u << 20));
    DWORD written = 0;
    if (!WriteFile(file, data.data() + offset, chunk, &written, nullptr) ||
        written == 0)
      break;
    offset += written;
  }
  CloseHandle(file);
  return offset == data.size();
}

std::wstring settingsPath() {
  wchar_t appdata[MAX_PATH] = {};
  DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return std::wstring();
  std::wstring dir = std::wstring(appdata) + L"\\BitstreamInspector";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir + L"\\settings.ini";
}

static int readLanguageSetting(const std::wstring& path);
void loadSettings() {
  std::wstring path = settingsPath();
  if (path.empty()) return;
  int left =
      GetPrivateProfileIntW(L"layout", L"left_percent", 43, path.c_str());
  int tree =
      GetPrivateProfileIntW(L"layout", L"tree_percent", 55, path.c_str());
  if (left < 25 || left > 70) left = 43;
  if (tree < 20 || tree > 85) tree = 55;
  g_app.left_ratio = static_cast<float>(left) / 100.0f;
  g_app.tree_ratio = static_cast<float>(tree) / 100.0f;

  int lang = readLanguageSetting(path);
  setLanguage(lang == 2 ? Lang::Zh : Lang::En);
}

static int readLanguageSetting(const std::wstring& path) {
  return GetPrivateProfileIntW(L"ui", L"language", 1, path.c_str());
}

void saveSettings() {
  std::wstring path = settingsPath();
  if (path.empty()) return;
  std::wstring left =
      std::to_wstring(static_cast<int>(g_app.left_ratio * 100.0f + 0.5f));
  std::wstring tree =
      std::to_wstring(static_cast<int>(g_app.tree_ratio * 100.0f + 0.5f));
  WritePrivateProfileStringW(L"layout", L"left_percent", left.c_str(),
                             path.c_str());
  WritePrivateProfileStringW(L"layout", L"tree_percent", tree.c_str(),
                             path.c_str());
  std::wstring lang = std::to_wstring(language() == Lang::Zh ? 2 : 1);
  WritePrivateProfileStringW(L"ui", L"language", lang.c_str(), path.c_str());
}

void updateStatus() {
  if (!g_app.status) return;
  std::wstring left;
  if (!g_app.loaded) {
    left = tr(L"No file loaded", L"未打开文件");
  } else {
    left = baseName(g_app.path) + L"  |  " + wide(g_app.stream.streamSummary());
  }
  SendMessageW(g_app.status, SB_SETTEXTW, 0,
               reinterpret_cast<LPARAM>(left.c_str()));

  std::wstring right;
  if (g_app.loaded && g_app.current_frame >= 0 &&
      g_app.current_frame < static_cast<int>(g_app.stream.frames().size())) {
    const FrameInfo& f =
        g_app.stream.frames()[static_cast<size_t>(g_app.current_frame)];
    right = std::wstring(tr(L"frame ", L"帧 ")) +
            std::to_wstring(g_app.current_frame + 1) + L"/" +
            std::to_wstring(g_app.stream.frames().size()) + L"   " +
            wide(g_app.stream.frameTypeName(f)) + L"   POC " +
            std::to_wstring(f.poc) + L"   QP " + std::to_wstring(f.qp) +
            L"   " + std::to_wstring(f.byte_size) + tr(L" B", L" 字节");
    if (f.refs.computed) {
      right += tr(L"   |   L0 ", L"   |   L0 ");
      for (size_t i = 0; i < f.refs.l0_pocs.size(); i++)
        right += (i ? L"," : L"") + std::to_wstring(f.refs.l0_pocs[i]);
      if (!f.refs.l1_pocs.empty()) {
        right += L"   L1 ";
        for (size_t i = 0; i < f.refs.l1_pocs.size(); i++)
          right += (i ? L"," : L"") + std::to_wstring(f.refs.l1_pocs[i]);
      }
      right += tr(L"   DPB ", L"   DPB ") + std::to_wstring(f.refs.dpb_count);
      if (!f.refs.missing_pocs.empty())
        right += tr(L"   missing ", L"   缺失 ") +
                 std::to_wstring(f.refs.missing_pocs.size());
    }
    SyntaxTree::Selection sel = g_app.syntax_tree.selection();
    if (sel.nal < g_app.stream.nals().size()) {
      const NalEntry& nal = g_app.stream.nals()[sel.nal];
      right += tr(L"   |   NAL ", L"   |   NAL ") + std::to_wstring(sel.nal) +
               L" " + shortNalName(g_app.stream, nal) +
               tr(L"  off 0x", L"  偏移 0x");
      wchar_t hex[16];
      swprintf(hex, 16, L"%X", static_cast<unsigned>(nal.unit.payload_off));
      right += hex;
      if (sel.bit_len)
        right += tr(L"  bits ", L"  位 ") + std::to_wstring(sel.bit_pos) +
                 L".." + std::to_wstring(sel.bit_pos + sel.bit_len - 1);
    }
  }
  SendMessageW(g_app.status, SB_SETTEXTW, 1,
               reinterpret_cast<LPARAM>(right.c_str()));
}

void layout(HWND hwnd) {
  if (!g_app.hwnd || !g_app.status) return;
  SendMessageW(g_app.status, WM_SIZE, 0, 0);
  RECT rc;
  GetClientRect(hwnd, &rc);
  RECT sr;
  GetWindowRect(g_app.status, &sr);
  int status_h = sr.bottom - sr.top;

  const int pad = px(6);
  const int btn_h = px(28);
  int x = pad;
  int y = pad;
  MoveWindow(g_app.btn_open, x, y, px(88), btn_h, TRUE);
  x += px(88) + pad;
  MoveWindow(g_app.btn_prev, x, y, px(68), btn_h, TRUE);
  x += px(68) + pad;
  MoveWindow(g_app.btn_next, x, y, px(68), btn_h, TRUE);
  x += px(68) + pad;
  MoveWindow(g_app.btn_prev_key, x, y, px(70), btn_h, TRUE);
  x += px(70) + pad;
  MoveWindow(g_app.btn_next_key, x, y, px(70), btn_h, TRUE);
  x += px(70) + pad;
  int info_w = std::max(0, static_cast<int>(rc.right) - x - pad);
  MoveWindow(g_app.info, x, y, info_w, btn_h, TRUE);

  int top = y + btn_h + pad;
  int chart_h = px(150);
  int thumb_h = px(68);
  int bottom_limit = static_cast<int>(rc.bottom) - status_h;
  int chart_top = bottom_limit - pad - chart_h;
  int thumb_top = chart_top - pad - thumb_h;
  int list_h = thumb_top - pad - top;
  if (list_h < px(140)) {
    list_h = px(140);
    thumb_top = top + list_h + pad;
    chart_top = thumb_top + thumb_h + pad;
  }

  int content_w = static_cast<int>(rc.right) - 3 * pad;
  if (content_w < 1) content_w = 1;
  int left_w = static_cast<int>(content_w * g_app.left_ratio);
  int min_left = std::min(px(240), content_w / 3);
  int max_left = std::max(min_left, content_w - px(240));
  left_w = std::min(std::max(left_w, min_left), max_left);
  int right_x = pad + left_w + pad;
  int right_w = std::max(px(200), static_cast<int>(rc.right) - right_x - pad);

  int avail = std::max(1, list_h - pad);
  int tree_h = static_cast<int>(avail * g_app.tree_ratio);
  int min_tree = std::min(px(100), avail / 3);
  tree_h =
      std::min(std::max(tree_h, min_tree), std::max(min_tree, avail - px(80)));
  int hex_h = std::max(0, list_h - tree_h - pad);

  MoveWindow(g_app.frame_list.hwnd(), pad, top, left_w, list_h, TRUE);
  MoveWindow(g_app.syntax_tree.hwnd(), right_x, top, right_w, tree_h, TRUE);

  int panel_y = top + tree_h + pad;
  if (g_app.tabs) {
    MoveWindow(g_app.tabs, right_x, panel_y, right_w, hex_h, TRUE);
    RECT display = {0, 0, right_w, hex_h};
    TabCtrl_AdjustRect(g_app.tabs, FALSE, &display);
    int vx = right_x + display.left;
    int vy = panel_y + display.top;
    int vw = std::max(0, static_cast<int>(display.right - display.left));
    int vh = std::max(0, static_cast<int>(display.bottom - display.top));
    MoveWindow(g_app.hex_view.hwnd(), vx, vy, vw, vh, TRUE);
    MoveWindow(g_app.video_view.hwnd(), vx, vy, vw, vh, TRUE);
    MoveWindow(g_app.refs_view.hwnd(), vx, vy, vw, vh, TRUE);
    ShowWindow(g_app.hex_view.hwnd(),
               g_app.active_tab == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app.video_view.hwnd(),
               g_app.active_tab == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app.refs_view.hwnd(),
               g_app.active_tab == 2 ? SW_SHOW : SW_HIDE);
  }
  MoveWindow(g_app.thumbnails.hwnd(), pad, thumb_top,
             std::max(0, static_cast<int>(rc.right) - 2 * pad), thumb_h, TRUE);
  MoveWindow(g_app.chart.hwnd(), pad, chart_top,
             std::max(0, static_cast<int>(rc.right) - 2 * pad), chart_h, TRUE);

  g_app.content_top = top;
  g_app.list_height = list_h;
  g_app.split_left = {pad + left_w, top, pad + left_w + pad, top + list_h};
  g_app.split_mid = {right_x, top + tree_h, right_x + right_w,
                     top + tree_h + pad};

  int parts[2] = {static_cast<int>(rc.right) * 60 / 100, -1};
  SendMessageW(g_app.status, SB_SETPARTS, 2, reinterpret_cast<LPARAM>(parts));
  InvalidateRect(hwnd, nullptr, FALSE);
}

int hitSplitter(int x, int y) {
  const int slop = px(2);
  auto inside = [&](const RECT& r) {
    return x >= r.left - slop && x < r.right + slop && y >= r.top - slop &&
           y < r.bottom + slop;
  };
  if (inside(g_app.split_left)) return 1;
  if (inside(g_app.split_mid)) return 2;
  return 0;
}

void resetUi() {
  g_app.loaded = false;
  g_app.current_frame = -1;
  g_app.path.clear();
  SetWindowTextW(g_app.hwnd, kTitle);
  SetWindowTextW(g_app.info, tr(L"Open a media file (Ctrl+O, or drag and drop)",
                                L"打开媒体文件（Ctrl+O，或拖放文件）"));
  EnableWindow(g_app.btn_prev, FALSE);
  EnableWindow(g_app.btn_next, FALSE);
  EnableWindow(g_app.btn_prev_key, FALSE);
  EnableWindow(g_app.btn_next_key, FALSE);
  g_app.frame_list.setStream(nullptr);
  g_app.chart.setStream(nullptr);
  g_app.thumbnails.setStream(nullptr);
  g_app.refs_view.clear();
  g_app.syntax_tree.clear();
  g_app.hex_view.clear();
#ifdef BS_HAVE_FFMPEG
  g_app.media.reset();
  g_app.container = ContainerInfo();
#endif
  g_app.video_frame = -1;
  g_app.video_view.clear(
      tr(L"Open a media file to decode", L"打开媒体文件后可解码"));
  updateStatus();
}

void onSyntaxSelected() {
  SyntaxTree::Selection sel = g_app.syntax_tree.selection();
  if (!g_app.loaded || sel.nal >= g_app.stream.nals().size()) {
    g_app.hex_view.clear();
    return;
  }
  g_app.hex_view.setNal(&g_app.stream, sel.nal);
  if (sel.header) {
    size_t header_bytes = g_app.stream.codec() == Codec::H265 ? 2 : 1;
    g_app.hex_view.highlightBytes(0, header_bytes);
  } else if (sel.bit_len > 0) {
    g_app.hex_view.highlight(sel.nal, sel.bit_pos, sel.bit_len);
  }
  updateStatus();
}

DWORD WINAPI videoWorkerProc(LPVOID) {
  for (;;) {
    HANDLE handles[2] = {g_app.video_stop_event, g_app.video_event};
    DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    if (wait == WAIT_OBJECT_0) break;
    if (wait != WAIT_OBJECT_0 + 1) continue;
    int frame = -1;
    int generation = -1;
    EnterCriticalSection(&g_app.video_lock);
    frame = g_app.video_request;
    generation = g_app.video_request_gen;
    g_app.video_request = -1;
    LeaveCriticalSection(&g_app.video_lock);
    if (frame < 0) continue;

    auto* result = new VideoResult();
    result->frame = frame;
    result->generation = generation;
#ifdef BS_HAVE_FFMPEG
    if (g_app.media) {
      DecodedFrame decoded;
      result->ok = g_app.media->decodeFrameAt(
          g_app.stream, frame, decoded, result->keyframe_only, result->error);
      if (result->ok) {
        result->image.bgra = std::move(decoded.bgra);
        result->image.width = decoded.width;
        result->image.height = decoded.height;
        result->image.bit_depth = decoded.bit_depth;
        result->image.chroma_format = decoded.chroma_format;
        result->image.matrix = decoded.matrix;
        result->image.full_range = decoded.full_range;
        result->image.layout = decoded.layout;
        result->image.planes = std::move(decoded.planes);
        result->image.strides = std::move(decoded.strides);
      }
    } else {
      result->error = "no media";
    }
#else
    result->error = "FFmpeg media layer not built";
#endif
    PostMessageW(g_app.hwnd, WM_APP_VIDEO_READY, 0,
                 reinterpret_cast<LPARAM>(result));
  }
  return 0;
}

void startVideoWorker() {
  if (g_app.video_thread || !g_app.video_event || !g_app.video_stop_event)
    return;
  ResetEvent(g_app.video_stop_event);
  g_app.video_thread =
      CreateThread(nullptr, 0, videoWorkerProc, nullptr, 0, nullptr);
}

void stopVideoWorker() {
  if (g_app.video_thread) {
    SetEvent(g_app.video_stop_event);
    WaitForSingleObject(g_app.video_thread, INFINITE);
    CloseHandle(g_app.video_thread);
    g_app.video_thread = nullptr;
  }
  g_app.video_generation.fetch_add(1);
  if (g_app.video_lock_ready) {
    EnterCriticalSection(&g_app.video_lock);
    g_app.video_request = -1;
    LeaveCriticalSection(&g_app.video_lock);
  }
  if (g_app.video_event) ResetEvent(g_app.video_event);
  if (g_app.video_stop_event) ResetEvent(g_app.video_stop_event);
}

void requestVideo(int frame) {
#ifdef BS_HAVE_FFMPEG
  if (!g_app.video_view.hwnd()) return;
  if (!g_app.media || frame < 0) {
    g_app.video_frame = -1;
    g_app.video_view.clear(
        tr(L"Open a media file to decode", L"打开媒体文件后可解码"));
    return;
  }
  if (g_app.video_frame == frame) return;
  EnterCriticalSection(&g_app.video_lock);
  g_app.video_request = frame;
  g_app.video_request_gen = g_app.video_generation.fetch_add(1) + 1;
  LeaveCriticalSection(&g_app.video_lock);
  SetEvent(g_app.video_event);
#else
  (void)frame;
  g_app.video_view.clear(
      tr(L"FFmpeg media layer not built", L"未构建 FFmpeg 媒体层"));
#endif
}

DWORD WINAPI thumbWorkerProc(LPVOID) {
  for (;;) {
    HANDLE handles[2] = {g_app.thumb_stop_event, g_app.thumb_event};
    DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    if (wait == WAIT_OBJECT_0) break;
    if (wait != WAIT_OBJECT_0 + 1) continue;
    int frame = -1;
    int generation = -1;
    EnterCriticalSection(&g_app.thumb_lock);
    if (!g_app.thumb_queue.empty()) {
      frame = g_app.thumb_queue.front().first;
      generation = g_app.thumb_queue.front().second;
      g_app.thumb_queue.pop_front();
      g_app.thumb_inflight = frame;
    }
    LeaveCriticalSection(&g_app.thumb_lock);
    if (frame < 0) continue;

    auto* result = new ThumbResult();
    result->frame = frame;
    result->generation = generation;
#ifdef BS_HAVE_FFMPEG
    if (g_app.thumb_media) {
      result->ok = g_app.thumb_media->decodeThumbnailAt(
          g_app.stream, frame, 96, 54, result->bgra, result->error);
      result->width = 96;
      result->height = 54;
    } else {
      result->error = "no thumbnail decoder";
    }
#else
    result->error = "FFmpeg media layer not built";
#endif
    PostMessageW(g_app.hwnd, WM_APP_THUMB_READY, 0,
                 reinterpret_cast<LPARAM>(result));
  }
  return 0;
}

void startThumbWorker() {
  if (g_app.thumb_thread || !g_app.thumb_event || !g_app.thumb_stop_event)
    return;
  ResetEvent(g_app.thumb_stop_event);
  g_app.thumb_thread =
      CreateThread(nullptr, 0, thumbWorkerProc, nullptr, 0, nullptr);
}

void stopThumbWorker() {
  if (g_app.thumb_thread) {
    SetEvent(g_app.thumb_stop_event);
    WaitForSingleObject(g_app.thumb_thread, INFINITE);
    CloseHandle(g_app.thumb_thread);
    g_app.thumb_thread = nullptr;
  }
  g_app.thumb_generation.fetch_add(1);
  if (g_app.thumb_lock_ready) {
    EnterCriticalSection(&g_app.thumb_lock);
    g_app.thumb_queue.clear();
    g_app.thumb_inflight = -1;
    LeaveCriticalSection(&g_app.thumb_lock);
  }
  if (g_app.thumb_event) ResetEvent(g_app.thumb_event);
  if (g_app.thumb_stop_event) ResetEvent(g_app.thumb_stop_event);
}

void requestThumbnail(int frame) {
#ifdef BS_HAVE_FFMPEG
  if (!g_app.thumb_media || frame < 0) return;
  if (g_app.thumb_thread) {
    EnterCriticalSection(&g_app.thumb_lock);
    if (g_app.thumb_inflight == frame) {
      LeaveCriticalSection(&g_app.thumb_lock);
      return;
    }
    for (const auto& queued : g_app.thumb_queue) {
      if (queued.first == frame) {
        LeaveCriticalSection(&g_app.thumb_lock);
        return;
      }
    }
    g_app.thumb_queue.emplace_back(frame, g_app.thumb_generation.load());
    LeaveCriticalSection(&g_app.thumb_lock);
    SetEvent(g_app.thumb_event);
  }
#else
  (void)frame;
#endif
}

void applyLanguage(HWND hwnd) {
  HMENU old_menu = GetMenu(hwnd);
  createMenu(hwnd);
  if (old_menu) DestroyMenu(old_menu);
  DrawMenuBar(hwnd);

  SetWindowTextW(g_app.btn_open, tr(L"Open...", L"打开..."));
  SetWindowTextW(g_app.btn_prev, tr(L"< Prev", L"< 上一帧"));
  SetWindowTextW(g_app.btn_next, tr(L"Next >", L"下一帧 >"));
  SetWindowTextW(g_app.btn_prev_key, tr(L"Prev Key", L"上个关键帧"));
  SetWindowTextW(g_app.btn_next_key, tr(L"Next Key", L"下个关键帧"));

  if (g_app.tabs) {
    TCITEMW item = {};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t*>(tr(L"Hex", L"十六进制"));
    TabCtrl_SetItem(g_app.tabs, 0, &item);
    item.pszText = const_cast<wchar_t*>(tr(L"Video", L"画面"));
    TabCtrl_SetItem(g_app.tabs, 1, &item);
    item.pszText = const_cast<wchar_t*>(tr(L"Refs", L"参考帧"));
    TabCtrl_SetItem(g_app.tabs, 2, &item);
  }
  g_app.frame_list.updateHeaders();
  g_app.refs_view.updateHeaders();

  if (g_app.loaded)
    SetWindowTextW(g_app.info, streamInfoText().c_str());
  else
    SetWindowTextW(g_app.info,
                   tr(L"Open a media file (Ctrl+O, or drag and drop)",
                      L"打开媒体文件（Ctrl+O，或拖放文件）"));

  g_app.chart.invalidate();
  InvalidateRect(g_app.hex_view.hwnd(), nullptr, FALSE);
  g_app.video_frame = -1;
  g_app.video_view.clear(tr(L"Select a frame to decode", L"选择一帧进行解码"));
  if (g_app.loaded && g_app.current_frame >= 0)
    requestVideo(g_app.current_frame);
  updateStatus();
  saveSettings();
}

void updateNavButtons();

void onFrameSelected(int index) {
  if (!g_app.loaded || index < 0 ||
      index >= static_cast<int>(g_app.stream.frames().size()))
    return;
  g_app.current_frame = index;
  g_app.hex_view.clear();
  g_app.syntax_tree.setFrame(&g_app.stream, index);
  g_app.chart.setSelected(index);
  g_app.thumbnails.setSelected(index);
  g_app.refs_view.setFrame(&g_app.stream, index);
  EnableWindow(g_app.btn_prev, index > 0);
  EnableWindow(g_app.btn_next,
               index < static_cast<int>(g_app.stream.frames().size()) - 1);
  updateNavButtons();
  if (g_app.active_tab == 1) requestVideo(index);
  updateStatus();
}

void selectFrame(int index) {
  if (!g_app.loaded || g_app.stream.frames().empty()) return;
  int last = static_cast<int>(g_app.stream.frames().size()) - 1;
  index = std::min(std::max(index, 0), last);
  EnableWindow(g_app.btn_prev, index > 0);
  EnableWindow(g_app.btn_next, index < last);
  g_app.frame_list.select(index, true);
  if (g_app.current_frame != index) onFrameSelected(index);
  updateNavButtons();
}

void updateNavButtons() {
  bool prev_key = false;
  bool next_key = false;
  if (g_app.loaded && g_app.current_frame >= 0) {
    prev_key =
        findKeyframe(g_app.stream.frames(), g_app.current_frame, -1) >= 0;
    next_key = findKeyframe(g_app.stream.frames(), g_app.current_frame, 1) >= 0;
  }
  EnableWindow(g_app.btn_prev_key, prev_key);
  EnableWindow(g_app.btn_next_key, next_key);
}

void selectKeyframe(int direction) {
  if (!g_app.loaded || g_app.current_frame < 0) return;
  int target =
      findKeyframe(g_app.stream.frames(), g_app.current_frame, direction);
  if (target >= 0) selectFrame(target);
}

void selectGop(int direction) {
  if (!g_app.loaded || g_app.current_frame < 0) return;
  const auto& frames = g_app.stream.frames();
  const int current = g_app.current_frame;
  const int gop = frames[static_cast<size_t>(current)].gop_start;
  if (gop < 0) return;
  if (direction < 0) {
    if (gop == 0) return;
    selectFrame(frames[static_cast<size_t>(gop - 1)].gop_start);
    return;
  }
  size_t end = static_cast<size_t>(gop);
  while (end < frames.size() && frames[end].gop_start == gop) end++;
  if (end < frames.size()) selectFrame(frames[end].gop_start);
}

void openFile(const std::wstring& path) {
  SetCursor(LoadCursorW(nullptr, IDC_WAIT));
  Stream loaded;
  std::string error;
  std::string media_error;
#ifdef BS_HAVE_FFMPEG
  std::unique_ptr<MediaFile> media;
#endif
  bool ok = false;
#ifdef BS_HAVE_FFMPEG
  auto candidate = std::make_unique<MediaFile>();
  std::vector<uint8_t> es;
  if (candidate->open(utf8(path), media_error)) {
    if (candidate->extractAnnexB(es, media_error)) {
      if (loaded.loadBuffer(std::move(es), error)) {
        candidate->applyTiming(loaded);
        media = std::move(candidate);
        ok = true;
      }
    }
  }
#endif
  if (!ok) {
    std::vector<uint8_t> buffer;
    std::string read_error;
    loaded = Stream();
    if (readFileW(path, buffer, read_error) &&
        loaded.loadBuffer(std::move(buffer), error)) {
      ok = true;
    } else if (error.empty()) {
      error = media_error.empty() ? read_error : media_error;
    }
  }
  SetCursor(LoadCursorW(nullptr, IDC_ARROW));
  if (!ok) {
    if (error.empty()) error = "cannot open file";
    MessageBoxW(g_app.hwnd, wide(error).c_str(),
                tr(L"Open failed", L"打开失败"), MB_OK | MB_ICONERROR);
    return;  // keep the previously loaded stream
  }
  if (loaded.frames().empty()) {
    std::wstring message =
        error.empty()
            ? tr(L"No access units could be identified. The file may not be "
                 L"a supported H.264/H.265 stream.",
                 L"无法识别任何图像单元。该文件可能不是受支持的 H.264/H.265 "
                 L"码流。")
            : wide(error);
    MessageBoxW(g_app.hwnd, message.c_str(), tr(L"Open failed", L"打开失败"),
                MB_OK | MB_ICONWARNING);
    return;  // keep the previously loaded stream
  }
  stopVideoWorker();
  stopThumbWorker();
  g_app.stream = std::move(loaded);
  g_app.loaded = true;
  g_app.path = path;
  g_app.current_frame = -1;
  g_app.video_frame = -1;
#ifdef BS_HAVE_FFMPEG
  g_app.media = std::move(media);
  g_app.container = g_app.media ? g_app.media->info() : ContainerInfo();
  g_app.thumb_media.reset();
  {
    auto thumb = std::make_unique<MediaFile>();
    std::vector<uint8_t> thumb_es;
    std::string thumb_error;
    if (thumb->open(utf8(path), thumb_error) &&
        thumb->extractAnnexB(thumb_es, thumb_error)) {
      g_app.thumb_media = std::move(thumb);
    }
  }
  applyPreviewColour();
#endif
  SetWindowTextW(g_app.hwnd,
                 (std::wstring(kTitle) + L" - " + baseName(path)).c_str());
  SetWindowTextW(g_app.info, streamInfoText().c_str());
  EnableWindow(g_app.btn_prev, FALSE);
  EnableWindow(g_app.btn_next, g_app.stream.frames().size() > 1);
  EnableWindow(g_app.btn_prev_key, FALSE);
  EnableWindow(g_app.btn_next_key, FALSE);
  g_app.frame_list.setStream(&g_app.stream);
  g_app.frame_list.refresh();
  g_app.chart.setStream(&g_app.stream);
  g_app.thumbnails.setStream(&g_app.stream);
  g_app.refs_view.clear();
  g_app.syntax_tree.clear();
  g_app.hex_view.clear();
  g_app.video_view.clear(tr(L"Select a frame to decode", L"选择一帧进行解码"));

  startVideoWorker();
  startThumbWorker();
  selectFrame(0);
  updateStatus();
}

void openCompare(HWND hwnd) {
  auto pick = [&](const wchar_t* title, std::wstring& path) {
    wchar_t file[32768] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = filterForLanguage();
    ofn.lpstrFile = file;
    ofn.nMaxFile = 32768;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return false;
    path = file;
    return true;
  };
  std::wstring first;
  std::wstring second;
  if (!pick(tr(L"Select the first stream", L"选择第一条码流"), first)) return;
  if (!pick(tr(L"Select the second stream", L"选择第二条码流"), second)) return;
  CompareWindow::open(hwnd, g_app.inst, first, second);
}

void openDialog(HWND hwnd) {
  wchar_t file[32768] = {};
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = hwnd;
  ofn.lpstrFilter = filterForLanguage();
  ofn.lpstrFile = file;
  ofn.nMaxFile = 32768;
  ofn.lpstrTitle = tr(L"Open media file", L"打开媒体文件");
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (GetOpenFileNameW(&ofn)) openFile(file);
}

std::wstring defaultExportName(const wchar_t* suffix) {
  std::wstring name = baseName(g_app.path);
  size_t dot = name.find_last_of(L'.');
  if (dot != std::wstring::npos && dot > 0) name.resize(dot);
  return name + suffix;
}

std::wstring frameExportName(const wchar_t* extension) {
  std::wstring name = defaultExportName(L"");
  return name + L"_frame" + std::to_wstring(g_app.current_frame) + extension;
}

void exportImage() {
  const ImageData& image = g_app.video_view.image();
  if (image.bgra.empty()) {
    MessageBoxW(g_app.hwnd,
                tr(L"Select a decoded frame before exporting.",
                   L"请先选择一帧并等待解码完成。"),
                kTitle, MB_OK | MB_ICONINFORMATION);
    return;
  }
  std::wstring default_name = frameExportName(L".png");
  wchar_t file[32768] = {};
  lstrcpynW(file, default_name.c_str(), 32768);
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = g_app.hwnd;
  ofn.lpstrFilter =
      tr(L"PNG image (*.png)\0*.png\0BMP image (*.bmp)\0*.bmp\0All files "
         L"(*.*)\0*.*\0",
         L"PNG 图像 (*.png)\0*.png\0BMP 图像 (*.bmp)\0*.bmp\0所有文件 "
         L"(*.*)\0*.*\0");
  ofn.lpstrFile = file;
  ofn.nMaxFile = 32768;
  ofn.lpstrDefExt = L"png";
  ofn.lpstrTitle = tr(L"Export frame image", L"导出当前画面");
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (!GetSaveFileNameW(&ofn)) return;
  std::string error;
  if (!writeImageFile(file, image, error)) {
    MessageBoxW(g_app.hwnd, wide(error).c_str(),
                tr(L"Export failed", L"导出失败"), MB_OK | MB_ICONERROR);
    return;
  }
  MessageBoxW(g_app.hwnd, tr(L"Image exported.", L"画面已导出。"),
              tr(L"Export complete", L"导出完成"), MB_OK | MB_ICONINFORMATION);
}

void exportYuv() {
  const ImageData& image = g_app.video_view.image();
  if (image.planes.empty()) {
    MessageBoxW(g_app.hwnd,
                tr(L"Select a decoded frame before exporting.",
                   L"请先选择一帧并等待解码完成。"),
                kTitle, MB_OK | MB_ICONINFORMATION);
    return;
  }
  std::wstring default_name = frameExportName(L".yuv");
  wchar_t file[32768] = {};
  lstrcpynW(file, default_name.c_str(), 32768);
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = g_app.hwnd;
  ofn.lpstrFilter = tr(L"Raw YUV (*.yuv)\0*.yuv\0All files (*.*)\0*.*\0",
                       L"原始 YUV (*.yuv)\0*.yuv\0所有文件 (*.*)\0*.*\0");
  ofn.lpstrFile = file;
  ofn.nMaxFile = 32768;
  ofn.lpstrDefExt = L"yuv";
  ofn.lpstrTitle = tr(L"Export raw YUV frame", L"导出当前帧 YUV");
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (!GetSaveFileNameW(&ofn)) return;
  std::string error;
  if (!writeRawYuv(file, image, error)) {
    MessageBoxW(g_app.hwnd, wide(error).c_str(),
                tr(L"Export failed", L"导出失败"), MB_OK | MB_ICONERROR);
    return;
  }
  MessageBoxW(g_app.hwnd, tr(L"YUV frame exported.", L"YUV 已导出。"),
              tr(L"Export complete", L"导出完成"), MB_OK | MB_ICONINFORMATION);
}

enum class ExportKind { Frames, Nals, Cpb };

void exportCsv(ExportKind kind) {
  if (!g_app.loaded) {
    MessageBoxW(g_app.hwnd,
                tr(L"Open a stream before exporting.", L"请先打开码流再导出。"),
                kTitle, MB_OK | MB_ICONINFORMATION);
    return;
  }
  const bool frames = kind == ExportKind::Frames;
  const bool cpb = kind == ExportKind::Cpb;
  const wchar_t* suffix = frames ? L"_frames.csv"
                          : cpb  ? L"_cpb.csv"
                                 : L"_nals.csv";
  std::wstring default_name = defaultExportName(suffix);
  wchar_t file[32768] = {};
  lstrcpynW(file, default_name.c_str(), 32768);

  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = g_app.hwnd;
  ofn.lpstrFilter = tr(L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0",
                       L"CSV 文件 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0");
  ofn.lpstrFile = file;
  ofn.nMaxFile = 32768;
  ofn.lpstrDefExt = L"csv";
  ofn.lpstrTitle = frames ? tr(L"Export frame list", L"导出帧列表")
                   : cpb  ? tr(L"Export CPB model", L"导出 CPB 模型")
                          : tr(L"Export NAL list", L"导出 NAL 列表");
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (!GetSaveFileNameW(&ofn)) return;

  std::string data("\xEF\xBB\xBF", 3);
  data += frames ? g_app.stream.framesCsv()
          : cpb  ? g_app.stream.cpbCsv()
                 : g_app.stream.nalsCsv();
  if (!writeFileW(file, data)) {
    MessageBoxW(g_app.hwnd,
                tr(L"Cannot write the CSV file.", L"无法写入 CSV 文件。"),
                tr(L"Export failed", L"导出失败"), MB_OK | MB_ICONERROR);
    return;
  }
  size_t count = frames ? g_app.stream.frames().size()
                 : cpb  ? g_app.stream.cpbModel().samples.size()
                        : g_app.stream.nals().size();
  std::wstring message = tr(L"Exported ", L"已导出 ") + std::to_wstring(count) +
                         (frames ? tr(L" frame rows.", L" 行帧记录。")
                          : cpb  ? tr(L" CPB rows.", L" 行 CPB 记录。")
                                 : tr(L" NAL rows.", L" 行 NAL 记录。"));
  MessageBoxW(g_app.hwnd, message.c_str(), tr(L"Export complete", L"导出完成"),
              MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_CREATE: {
      g_app.hwnd = hwnd;
      InitializeCriticalSection(&g_app.video_lock);
      g_app.video_lock_ready = true;
      g_app.video_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      g_app.video_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      InitializeCriticalSection(&g_app.thumb_lock);
      g_app.thumb_lock_ready = true;
      g_app.thumb_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      g_app.thumb_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      loadSettings();
      DragAcceptFiles(hwnd, TRUE);
      createMenu(hwnd);
      g_app.btn_open =
          makeButton(hwnd, tr(L"Open...", L"打开..."), IDC_BTN_OPEN);
      g_app.btn_prev =
          makeButton(hwnd, tr(L"< Prev", L"< 上一帧"), IDC_BTN_PREV);
      g_app.btn_next =
          makeButton(hwnd, tr(L"Next >", L"下一帧 >"), IDC_BTN_NEXT);
      g_app.btn_prev_key =
          makeButton(hwnd, tr(L"Prev Key", L"上个关键帧"), IDC_BTN_PREV_KEY);
      g_app.btn_next_key =
          makeButton(hwnd, tr(L"Next Key", L"下个关键帧"), IDC_BTN_NEXT_KEY);
      g_app.info = CreateWindowExW(
          0, L"STATIC",
          tr(L"Open a media file (Ctrl+O, or drag and drop)",
             L"打开媒体文件（Ctrl+O，或拖放文件）"),
          WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS, 0,
          0, 0, 0, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_INFO)), g_app.inst,
          nullptr);
      SendMessageW(g_app.info, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()),
                   TRUE);
      g_app.status = CreateWindowExW(
          0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0,
          0, 0, 0, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_INFO + 1)),
          g_app.inst, nullptr);
      SendMessageW(g_app.status, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()),
                   TRUE);
      g_app.frame_list.create(hwnd, g_app.inst, IDC_FRAME_LIST);
      g_app.syntax_tree.create(hwnd, g_app.inst, IDC_SYNTAX_TREE);
      g_app.hex_view.create(hwnd, g_app.inst, IDC_HEX_VIEW);
      g_app.tabs = CreateWindowExW(
          0, WC_TABCONTROLW, L"",
          WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0, 0, 0,
          hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TAB)),
          g_app.inst, nullptr);
      SendMessageW(g_app.tabs, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()),
                   TRUE);
      TCITEMW tab_item = {};
      tab_item.mask = TCIF_TEXT;
      tab_item.pszText = const_cast<wchar_t*>(tr(L"Hex", L"十六进制"));
      TabCtrl_InsertItem(g_app.tabs, 0, &tab_item);
      tab_item.pszText = const_cast<wchar_t*>(tr(L"Video", L"画面"));
      TabCtrl_InsertItem(g_app.tabs, 1, &tab_item);
      tab_item.pszText = const_cast<wchar_t*>(tr(L"Refs", L"参考帧"));
      TabCtrl_InsertItem(g_app.tabs, 2, &tab_item);
      g_app.active_tab = 1;
      TabCtrl_SetCurSel(g_app.tabs, g_app.active_tab);
      g_app.video_view.create(hwnd, g_app.inst, IDC_VIDEO_VIEW);
      g_app.refs_view.create(hwnd, g_app.inst, IDC_REFS_VIEW);
      g_app.chart.create(hwnd, g_app.inst, IDC_CHART);
      g_app.thumbnails.create(hwnd, g_app.inst, IDC_THUMBNAILS);
      g_app.thumbnails.setRequestHandler(requestThumbnail);
      layout(hwnd);
      resetUi();
      startVideoWorker();
      return 0;
    }

    case WM_SIZE:
      layout(hwnd);
      return 0;

    case WM_GETMINMAXINFO: {
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lparam);
      RECT work = {};
      SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
      int w = static_cast<int>(work.right - work.left);
      int h = static_cast<int>(work.bottom - work.top);
      mmi->ptMinTrackSize.x = std::min(px(860), w * 3 / 4);
      mmi->ptMinTrackSize.y = std::min(px(560), h * 3 / 4);
      return 0;
    }

    case WM_CTLCOLORSTATIC: {
      HDC dc = reinterpret_cast<HDC>(wparam);
      SetTextColor(dc, RGB(70, 70, 70));
      SetBkMode(dc, TRANSPARENT);
      return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    }

    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case IDC_BTN_OPEN:
        case IDM_FILE_OPEN:
          openDialog(hwnd);
          return 0;
        case IDC_BTN_PREV:
        case IDM_VIEW_PREV:
          selectFrame(g_app.current_frame - 1);
          return 0;
        case IDC_BTN_NEXT:
        case IDM_VIEW_NEXT:
          selectFrame(g_app.current_frame + 1);
          return 0;
        case IDC_BTN_PREV_KEY:
        case IDM_VIEW_PREV_KEY:
          selectKeyframe(-1);
          return 0;
        case IDC_BTN_NEXT_KEY:
        case IDM_VIEW_NEXT_KEY:
          selectKeyframe(1);
          return 0;
        case IDM_VIEW_PREV_GOP:
          selectGop(-1);
          return 0;
        case IDM_VIEW_NEXT_GOP:
          selectGop(1);
          return 0;
        case IDM_FILE_EXPORT_FRAMES:
          exportCsv(ExportKind::Frames);
          return 0;
        case IDM_FILE_EXPORT_NALS:
          exportCsv(ExportKind::Nals);
          return 0;
        case IDM_FILE_EXPORT_CPB:
          exportCsv(ExportKind::Cpb);
          return 0;
        case IDM_FILE_EXPORT_IMAGE:
          exportImage();
          return 0;
        case IDM_FILE_EXPORT_YUV:
          exportYuv();
          return 0;
        case IDM_FILE_COMPARE:
          openCompare(hwnd);
          return 0;
        case IDM_VIEW_MATRIX_AUTO:
          g_app.preview_matrix = -1;
          applyPreviewColour();
          return 0;
        case IDM_VIEW_MATRIX_601:
          g_app.preview_matrix = 5;
          applyPreviewColour();
          return 0;
        case IDM_VIEW_MATRIX_709:
          g_app.preview_matrix = 1;
          applyPreviewColour();
          return 0;
        case IDM_VIEW_MATRIX_2020:
          g_app.preview_matrix = 9;
          applyPreviewColour();
          return 0;
        case IDM_VIEW_RANGE_AUTO:
          g_app.preview_range = -1;
          applyPreviewColour();
          return 0;
        case IDM_VIEW_RANGE_LIMITED:
          g_app.preview_range = 0;
          applyPreviewColour();
          return 0;
        case IDM_VIEW_RANGE_FULL:
          g_app.preview_range = 1;
          applyPreviewColour();
          return 0;
        case IDM_VIEW_CHART_SIZE:
          g_app.chart.setMode(ChartView::ChartMode::FrameSize);
          CheckMenuRadioItem(GetMenu(hwnd), IDM_VIEW_CHART_SIZE,
                             IDM_VIEW_CHART_CPB, IDM_VIEW_CHART_SIZE,
                             MF_BYCOMMAND);
          return 0;
        case IDM_VIEW_CHART_CPB:
          g_app.chart.setMode(ChartView::ChartMode::Cpb);
          CheckMenuRadioItem(GetMenu(hwnd), IDM_VIEW_CHART_SIZE,
                             IDM_VIEW_CHART_CPB, IDM_VIEW_CHART_CPB,
                             MF_BYCOMMAND);
          return 0;
        case IDM_VIEW_CHART_BITS:
          g_app.chart.setMode(ChartView::ChartMode::Bits);
          return 0;
        case IDM_VIEW_CONFORMANCE:
          showConformance();
          return 0;
        case IDM_VIEW_OVERLAY:
          g_app.show_overlay = !g_app.show_overlay;
          CheckMenuItem(
              GetMenu(hwnd), IDM_VIEW_OVERLAY,
              MF_BYCOMMAND | (g_app.show_overlay ? MF_CHECKED : MF_UNCHECKED));
          if (g_app.loaded && g_app.current_frame >= 0)
            g_app.video_view.setOverlay(
                g_app.show_overlay
                    ? g_app.stream.sliceOverlay(g_app.current_frame)
                    : std::vector<OverlayLine>());
          return 0;
        case IDM_VIEW_RELOAD:
          if (!g_app.path.empty()) openFile(g_app.path);
          return 0;
        case IDM_FILE_EXIT:
          SendMessageW(hwnd, WM_CLOSE, 0, 0);
          return 0;
        case IDM_HELP_ABOUT:
          MessageBoxW(
              hwnd,
              tr(L"Bitstream Inspector 0.4.0\n\n"
                 L"Annex B and container H.264/H.265 analyzer.\n"
                 L"Independent implementation based on the published "
                 L"ITU-T H.264 and H.265 syntax specifications.\n\n"
                 L"Not affiliated with Elecard or any commercial product.",
                 L"码流分析器 0.4.0\n\n"
                 L"支持 Annex B 裸流与容器封装的 H.264/H.265 分析。\n"
                 L"基于公开的 ITU-T H.264/H.265 语法规范独立实现。\n\n"
                 L"与 Elecard 或任何商业产品无关联。"),
              kTitle, MB_OK | MB_ICONINFORMATION);
          return 0;
        case IDM_LANG_EN:
        case IDM_LANG_ZH: {
          Lang target = LOWORD(wparam) == IDM_LANG_ZH ? Lang::Zh : Lang::En;
          if (target != language()) {
            setLanguage(target);
            applyLanguage(hwnd);
          }
          return 0;
        }
        default:
          break;
      }
      break;

    case WM_NOTIFY: {
      auto* hdr = reinterpret_cast<NMHDR*>(lparam);
      if (hdr->hwndFrom == g_app.tabs && hdr->code == TCN_SELCHANGE) {
        g_app.active_tab = TabCtrl_GetCurSel(g_app.tabs);
        layout(hwnd);
        if (g_app.active_tab == 1) requestVideo(g_app.current_frame);
        return 0;
      }
      if (hdr->hwndFrom == g_app.frame_list.hwnd())
        return g_app.frame_list.handleNotify(hdr);
      if (hdr->hwndFrom == g_app.syntax_tree.hwnd())
        return g_app.syntax_tree.handleNotify(hdr);
      break;
    }

    case WM_APP_FRAME_CHANGED:
      onFrameSelected(static_cast<int>(wparam));
      return 0;

    case WM_APP_FRAME_FROM_CHART:
      selectFrame(static_cast<int>(wparam));
      return 0;

    case WM_APP_SYNTAX_SELECTED:
      onSyntaxSelected();
      return 0;

    case WM_APP_VIDEO_READY: {
      auto* result = reinterpret_cast<VideoResult*>(lparam);
      if (result) {
        if (result->generation == g_app.video_generation.load() &&
            result->frame == g_app.current_frame) {
          if (!result->ok) {
            g_app.video_frame = -1;
            g_app.video_view.clear(tr(L"Decode failed: ", L"解码失败：") +
                                   wide(result->error));
          } else {
            g_app.video_frame = result->frame;
            std::wstring note;
            if (result->keyframe_only)
              note = tr(L"Keyframe preview (raw stream has no timestamps)",
                        L"关键帧预览（裸流不含时间戳）");
            g_app.video_view.setImage(std::move(result->image), note);
          }
        }
        delete result;
      }
      return 0;
    }

    case WM_APP_THUMB_READY: {
      auto* result = reinterpret_cast<ThumbResult*>(lparam);
      if (result) {
        if (g_app.thumb_lock_ready) {
          EnterCriticalSection(&g_app.thumb_lock);
          if (g_app.thumb_inflight == result->frame) g_app.thumb_inflight = -1;
          LeaveCriticalSection(&g_app.thumb_lock);
        }
        if (result->ok && result->generation == g_app.thumb_generation.load() &&
            result->frame >= 0 &&
            result->frame < static_cast<int>(g_app.stream.frames().size())) {
          ThumbnailStrip::Thumb thumb;
          thumb.bgra = std::move(result->bgra);
          thumb.width = result->width;
          thumb.height = result->height;
          g_app.thumbnails.setThumbnail(result->frame, std::move(thumb));
        }
        delete result;
      }
      return 0;
    }

    case WM_APP_THUMBNAIL_SELECTED:
    case WM_APP_REF_SELECTED:
      selectFrame(static_cast<int>(wparam));
      return 0;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      FillRect(dc, &ps.rcPaint,
               reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
      HPEN pen = CreatePen(PS_SOLID, 1, RGB(205, 205, 205));
      HGDIOBJ old = SelectObject(dc, pen);
      int x = g_app.split_left.left +
              (g_app.split_left.right - g_app.split_left.left) / 2;
      MoveToEx(dc, x, g_app.split_left.top, nullptr);
      LineTo(dc, x, g_app.split_left.bottom);
      int y = g_app.split_mid.top +
              (g_app.split_mid.bottom - g_app.split_mid.top) / 2;
      MoveToEx(dc, g_app.split_mid.left, y, nullptr);
      LineTo(dc, g_app.split_mid.right, y);
      SelectObject(dc, old);
      DeleteObject(pen);
      EndPaint(hwnd, &ps);
      return 0;
    }

    case WM_SETCURSOR:
      if (LOWORD(lparam) == HTCLIENT) {
        POINT pt = {};
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        int hit = hitSplitter(pt.x, pt.y);
        if (hit) {
          SetCursor(LoadCursorW(nullptr, hit == 1 ? IDC_SIZEWE : IDC_SIZENS));
          return TRUE;
        }
      }
      break;

    case WM_LBUTTONDOWN: {
      int x = static_cast<short>(LOWORD(lparam));
      int y = static_cast<short>(HIWORD(lparam));
      int hit = hitSplitter(x, y);
      if (hit) {
        g_app.drag = hit;
        if (hit == 1) {
          int divider = g_app.split_left.left +
                        (g_app.split_left.right - g_app.split_left.left) / 2;
          g_app.drag_offset = x - divider;
        } else {
          int divider = g_app.split_mid.top +
                        (g_app.split_mid.bottom - g_app.split_mid.top) / 2;
          g_app.drag_offset = y - divider;
        }
        SetCapture(hwnd);
        SetCursor(LoadCursorW(nullptr, hit == 1 ? IDC_SIZEWE : IDC_SIZENS));
        return 0;
      }
      break;
    }

    case WM_MOUSEMOVE: {
      int x = static_cast<short>(LOWORD(lparam));
      int y = static_cast<short>(HIWORD(lparam));
      if (g_app.drag == 1) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        int pad = px(6);
        int content_w = std::max(1, static_cast<int>(rc.right) - 3 * pad);
        int divider = x - g_app.drag_offset;
        int left_w = divider - pad - pad / 2;
        int min_left = std::min(px(240), content_w / 3);
        int max_left = std::max(min_left, content_w - px(240));
        left_w = std::min(std::max(left_w, min_left), max_left);
        g_app.left_ratio = static_cast<float>(left_w) / content_w;
        layout(hwnd);
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        return 0;
      }
      if (g_app.drag == 2) {
        int pad = px(6);
        int avail = std::max(1, g_app.list_height - pad);
        int divider = y - g_app.drag_offset;
        int tree_h = divider - g_app.content_top - pad / 2;
        int min_tree = std::min(px(100), avail / 3);
        int max_tree = std::max(min_tree, avail - px(80));
        tree_h = std::min(std::max(tree_h, min_tree), max_tree);
        g_app.tree_ratio = static_cast<float>(tree_h) / avail;
        layout(hwnd);
        SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
        return 0;
      }
      int hit = hitSplitter(x, y);
      SetCursor(
          LoadCursorW(nullptr, hit == 1 ? IDC_SIZEWE
                                        : (hit == 2 ? IDC_SIZENS : IDC_ARROW)));
      break;
    }

    case WM_LBUTTONUP:
      if (g_app.drag) {
        g_app.drag = 0;
        ReleaseCapture();
        return 0;
      }
      break;

    case WM_CAPTURECHANGED:
      g_app.drag = 0;
      return 0;

    case WM_DROPFILES: {
      HDROP drop = reinterpret_cast<HDROP>(wparam);
      wchar_t path[32768] = {};
      UINT len = DragQueryFileW(drop, 0, path, 32767);
      DragFinish(drop);
      if (len > 0) openFile(path);
      return 0;
    }

    case WM_CLOSE:
      saveSettings();
      DestroyWindow(hwnd);
      return 0;

    case WM_DESTROY:
      stopVideoWorker();
      stopThumbWorker();
      if (g_app.thumb_event) {
        CloseHandle(g_app.thumb_event);
        g_app.thumb_event = nullptr;
      }
      if (g_app.thumb_stop_event) {
        CloseHandle(g_app.thumb_stop_event);
        g_app.thumb_stop_event = nullptr;
      }
      if (g_app.thumb_lock_ready) {
        DeleteCriticalSection(&g_app.thumb_lock);
        g_app.thumb_lock_ready = false;
      }
      if (g_app.video_event) {
        CloseHandle(g_app.video_event);
        g_app.video_event = nullptr;
      }
      if (g_app.video_stop_event) {
        CloseHandle(g_app.video_stop_event);
        g_app.video_stop_event = nullptr;
      }
      if (g_app.video_lock_ready) {
        DeleteCriticalSection(&g_app.video_lock);
        g_app.video_lock_ready = false;
      }
      PostQuitMessage(0);
      return 0;

    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
  SetProcessDPIAware();
  initDpi();
  initCommonControls();

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = wndProc;
  wc.hInstance = inst;
  HICON app_icon = LoadIconW(inst, MAKEINTRESOURCEW(101));
  if (!app_icon) app_icon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hIcon = app_icon;
  wc.hIconSm = app_icon;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kWindowClass;
  if (!RegisterClassExW(&wc)) return 1;

  g_app.inst = inst;
  RECT work = {};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  int win_w = px(1280);
  int win_h = px(820);
  int avail_w = static_cast<int>(work.right - work.left) - px(40);
  int avail_h = static_cast<int>(work.bottom - work.top) - px(40);
  if (avail_w > 0 && win_w > avail_w) win_w = avail_w;
  if (avail_h > 0 && win_h > avail_h) win_h = avail_h;
  g_app.hwnd =
      CreateWindowExW(WS_EX_ACCEPTFILES, kWindowClass, kTitle,
                      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, win_w,
                      win_h, nullptr, nullptr, inst, nullptr);
  if (!g_app.hwnd) return 1;

  ShowWindow(g_app.hwnd, show);
  UpdateWindow(g_app.hwnd);

  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv) {
    if (argc >= 2) openFile(argv[1]);
    LocalFree(argv);
  }

  ACCEL accel[] = {
      {FVIRTKEY | FCONTROL, 'O', IDM_FILE_OPEN},
      {FVIRTKEY | FCONTROL, 'E', IDM_FILE_EXPORT_FRAMES},
      {FVIRTKEY, VK_F5, IDM_VIEW_RELOAD},
      {FVIRTKEY | FALT, VK_LEFT, IDM_VIEW_PREV},
      {FVIRTKEY | FALT, VK_RIGHT, IDM_VIEW_NEXT},
      {FVIRTKEY | FSHIFT, VK_LEFT, IDM_VIEW_PREV_KEY},
      {FVIRTKEY | FSHIFT, VK_RIGHT, IDM_VIEW_NEXT_KEY},
  };
  HACCEL table =
      CreateAcceleratorTableW(accel, static_cast<int>(std::size(accel)));

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!TranslateAcceleratorW(g_app.hwnd, table, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  if (table) DestroyAcceleratorTable(table);
  return static_cast<int>(msg.wParam);
}
