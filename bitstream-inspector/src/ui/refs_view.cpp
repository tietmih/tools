#include "ui/refs_view.h"

#include <commctrl.h>

#include <algorithm>

#include "ui/lang.h"
#include "ui/ui_util.h"

namespace ui {

void RefsView::create(HWND parent, HINSTANCE inst, int id) {
  parent_ = parent;
  hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
      WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 0,
      0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst,
      nullptr);
  ListView_SetExtendedListViewStyle(hwnd_,
                                    LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
  SendMessageW(hwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont()), TRUE);
  SetWindowSubclass(hwnd_, RefsView::subclassProc, id,
                    reinterpret_cast<DWORD_PTR>(this));
  updateHeaders();
}

void RefsView::updateHeaders() {
  if (!hwnd_) return;
  while (ListView_DeleteColumn(hwnd_, 0)) {
  }
  const struct {
    const wchar_t* text;
    int width;
  } columns[] = {
      {tr(L"List", L"列表"), 70},
      {tr(L"POC", L"POC"), 80},
      {tr(L"Frame", L"帧"), 80},
      {tr(L"State", L"状态"), 100},
  };
  for (int i = 0; i < static_cast<int>(std::size(columns)); i++) {
    LVCOLUMNW column = {};
    column.mask = LVCF_TEXT | LVCF_WIDTH;
    column.pszText = const_cast<wchar_t*>(columns[i].text);
    column.cx = px(columns[i].width);
    ListView_InsertColumn(hwnd_, i, &column);
  }
}

void RefsView::addRow(const wchar_t* list, int poc, int frame, bool missing) {
  if (!hwnd_) return;
  const int index = ListView_GetItemCount(hwnd_);
  LVITEMW item = {};
  item.mask = LVIF_TEXT | LVIF_PARAM;
  item.iItem = index;
  item.pszText = const_cast<wchar_t*>(list);
  item.lParam = static_cast<LPARAM>(frame);
  ListView_InsertItem(hwnd_, &item);
  std::wstring poc_text = std::to_wstring(poc);
  ListView_SetItemText(hwnd_, index, 1, const_cast<wchar_t*>(poc_text.c_str()));
  std::wstring frame_text =
      frame >= 0 ? std::to_wstring(frame) : tr(L"-", L"-");
  ListView_SetItemText(hwnd_, index, 2,
                       const_cast<wchar_t*>(frame_text.c_str()));
  const wchar_t* state =
      missing ? tr(L"missing", L"缺失") : tr(L"reference", L"参考");
  ListView_SetItemText(hwnd_, index, 3, const_cast<wchar_t*>(state));
}

void RefsView::setFrame(const bs::Stream* stream, int frame_index) {
  if (!hwnd_) return;
  ListView_DeleteAllItems(hwnd_);
  if (!stream || frame_index < 0 ||
      frame_index >= static_cast<int>(stream->frames().size()))
    return;
  const bs::FrameInfo& frame =
      stream->frames()[static_cast<size_t>(frame_index)];
  if (!frame.refs.computed) return;
  auto find_frame = [&](int poc) {
    for (const auto& candidate : stream->frames())
      if (candidate.poc == poc) return candidate.index;
    return -1;
  };
  for (int poc : frame.refs.l0_pocs) {
    const bool missing = std::find(frame.refs.missing_pocs.begin(),
                                   frame.refs.missing_pocs.end(),
                                   poc) != frame.refs.missing_pocs.end();
    addRow(L"L0", poc, missing ? -1 : find_frame(poc), missing);
  }
  for (int poc : frame.refs.l1_pocs) {
    const bool missing = std::find(frame.refs.missing_pocs.begin(),
                                   frame.refs.missing_pocs.end(),
                                   poc) != frame.refs.missing_pocs.end();
    addRow(L"L1", poc, missing ? -1 : find_frame(poc), missing);
  }
}

void RefsView::clear() {
  if (hwnd_) ListView_DeleteAllItems(hwnd_);
}

LRESULT CALLBACK RefsView::subclassProc(HWND hwnd, UINT msg, WPARAM wparam,
                                        LPARAM lparam, UINT_PTR id,
                                        DWORD_PTR ref_data) {
  auto* self = reinterpret_cast<RefsView*>(ref_data);
  if (self && msg == WM_NOTIFY) return self->handle(hwnd, msg, wparam, lparam);
  return DefSubclassProc(hwnd, msg, wparam, lparam);
}

LRESULT RefsView::handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == WM_NOTIFY) {
    auto* header = reinterpret_cast<NMHDR*>(lparam);
    if (header && header->hwndFrom == hwnd_) {
      if (header->code == NM_DBLCLK || header->code == LVN_ITEMACTIVATE) {
        const int selected = ListView_GetNextItem(hwnd_, -1, LVNI_SELECTED);
        if (selected >= 0) {
          LVITEMW item = {};
          item.mask = LVIF_PARAM;
          item.iItem = selected;
          if (ListView_GetItem(hwnd_, &item) && item.lParam >= 0) {
            PostMessageW(parent_, WM_APP_REF_SELECTED,
                         static_cast<WPARAM>(item.lParam), 0);
          }
        }
        return 0;
      }
    }
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace ui
