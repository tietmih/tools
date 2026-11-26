#include "ui/frame_list.h"

#include <cstdio>

#include "ui/lang.h"
#include "ui/ui_util.h"

namespace ui {

using namespace bs;

namespace {

const wchar_t* columnName(int index) {
  switch (index) {
    case 0:
      return tr(L"#", L"序号");
    case 1:
      return tr(L"Type", L"类型");
    case 2:
      return L"POC";
    case 3:
      return L"QP";
    case 4:
      return tr(L"Frame#", L"帧号");
    case 5:
      return tr(L"Size", L"大小");
    case 6:
      return tr(L"Slices", L"切片数");
    case 7:
      return tr(L"NALs", L"NAL 数");
    case 8:
      return tr(L"Offset", L"偏移");
    case 9:
      return tr(L"Time", L"时间");
    default:
      return L"";
  }
}

void addColumn(HWND hwnd, int index, const wchar_t* name, int width,
               int format = LVCFMT_LEFT) {
  LVCOLUMNW col = {};
  col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
  col.pszText = const_cast<wchar_t*>(name);
  col.cx = width;
  col.fmt = format;
  ListView_InsertColumn(hwnd, index, &col);
}

}  // namespace

void FrameList::create(HWND parent, HINSTANCE inst, int id) {
  parent_ = parent;
  hwnd_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                          WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL |
                              LVS_SHOWSELALWAYS | LVS_OWNERDATA,
                          0, 0, 0, 0, parent,
                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                          inst, nullptr);
  if (!hwnd_) return;
  ListView_SetExtendedListViewStyle(hwnd_, LVS_EX_FULLROWSELECT |
                                               LVS_EX_DOUBLEBUFFER |
                                               LVS_EX_HEADERDRAGDROP);
  SendMessageW(hwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()), TRUE);

  // Widths are in DPI-scaled units; CJK headers need a little more room than
  // the ASCII ones, so size for the wider of the two languages.
  int widths[] = {44, 48, 40, 32, 56, 64, 58, 52, 78, 52};
  for (int i = 0; i < 10; i++)
    addColumn(hwnd_, i, columnName(i), px(widths[i]),
              (i == 0 || i == 1) ? LVCFMT_LEFT : LVCFMT_RIGHT);
}

void FrameList::updateHeaders() {
  for (int i = 0; i < 10; i++) {
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT;
    col.pszText = const_cast<wchar_t*>(columnName(i));
    ListView_SetColumn(hwnd_, i, &col);
  }
}

void FrameList::setStream(const Stream* stream) {
  stream_ = stream;
  last_selected_ = -1;
  int count = stream ? static_cast<int>(stream->frames().size()) : 0;
  ListView_SetItemCountEx(hwnd_, count, LVSICF_NOINVALIDATEALL);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void FrameList::refresh() {
  int count = stream_ ? static_cast<int>(stream_->frames().size()) : 0;
  ListView_SetItemCountEx(hwnd_, count, LVSICF_NOINVALIDATEALL);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void FrameList::select(int index, bool ensure_visible) {
  if (!stream_) return;
  int count = static_cast<int>(stream_->frames().size());
  if (index < 0 || index >= count) return;
  if (selected() != index) {
    ListView_SetItemState(hwnd_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(hwnd_, index, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
  }
  if (ensure_visible) ListView_EnsureVisible(hwnd_, index, FALSE);
}

int FrameList::selected() const {
  return ListView_GetNextItem(hwnd_, -1, LVNI_SELECTED);
}

void FrameList::onGetDispInfo(NMLVDISPINFOW* di) {
  text_.clear();
  if (!stream_ || di->item.iItem < 0 ||
      di->item.iItem >= static_cast<int>(stream_->frames().size())) {
    di->item.pszText = const_cast<wchar_t*>(L"");
    return;
  }
  const FrameInfo& f = stream_->frames()[static_cast<size_t>(di->item.iItem)];
  wchar_t buf[128] = {};
  switch (di->item.iSubItem) {
    case 0:
      swprintf(buf, 128, L"%d", f.index);
      break;
    case 1:
      text_ = wide(stream_->frameTypeName(f));
      break;
    case 2:
      swprintf(buf, 128, L"%d", f.poc);
      break;
    case 3:
      swprintf(buf, 128, L"%d", f.qp);
      break;
    case 4:
      if (stream_->codec() == Codec::H264)
        swprintf(buf, 128, L"%d", f.frame_num);
      else
        swprintf(buf, 128, L"-");
      break;
    case 5:
      swprintf(buf, 128, L"%u", static_cast<unsigned>(f.byte_size));
      break;
    case 6:
      swprintf(buf, 128, L"%d", f.slice_count);
      break;
    case 7:
      swprintf(buf, 128, L"%d", f.nal_count);
      break;
    case 8:
      swprintf(buf, 128, L"0x%08X", static_cast<unsigned>(f.byte_offset));
      break;
    case 9: {
      double ms = stream_->frameTimeMs(f);
      if (ms >= 0.0)
        swprintf(buf, 128, L"%.0f", ms);
      else
        swprintf(buf, 128, L"-");
      break;
    }
    default:
      buf[0] = 0;
      break;
  }
  if (text_.empty() && buf[0] != L'\0' && di->item.iSubItem != 1) text_ = buf;
  di->item.pszText = const_cast<wchar_t*>(text_.c_str());
}

LRESULT FrameList::onCustomDraw(NMLVCUSTOMDRAW* cd) {
  switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
      return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
      if (!stream_) break;
      int idx = static_cast<int>(cd->nmcd.dwItemSpec);
      if (idx < 0 || idx >= static_cast<int>(stream_->frames().size())) break;
      const FrameInfo& f = stream_->frames()[static_cast<size_t>(idx)];
      cd->clrText = frameColor(f.picture_type, f.idr, f.irap,
                               stream_->codec() == Codec::H265, f.nal_type);
      return CDRF_NEWFONT;
    }
    default:
      break;
  }
  return CDRF_DODEFAULT;
}

LRESULT FrameList::handleNotify(NMHDR* hdr) {
  switch (hdr->code) {
    case LVN_GETDISPINFOW:
      onGetDispInfo(reinterpret_cast<NMLVDISPINFOW*>(hdr));
      return 0;
    case LVN_ITEMCHANGED: {
      auto* nv = reinterpret_cast<NMLISTVIEW*>(hdr);
      bool was = (nv->uOldState & LVIS_SELECTED) != 0;
      bool now = (nv->uNewState & LVIS_SELECTED) != 0;
      if (now && !was) {
        if (nv->iItem != last_selected_) {
          last_selected_ = nv->iItem;
          SendMessageW(parent_, WM_APP_FRAME_CHANGED,
                       static_cast<WPARAM>(nv->iItem), 0);
        }
      } else if (!now && was) {
        last_selected_ = -1;
      }
      break;
    }
    case NM_CUSTOMDRAW:
      return onCustomDraw(reinterpret_cast<NMLVCUSTOMDRAW*>(hdr));
    default:
      break;
  }
  return 0;
}

}  // namespace ui
