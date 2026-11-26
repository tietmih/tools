#include "ui/syntax_tree.h"

#include <cstdio>

#include "ui/ui_util.h"

namespace ui {

using namespace bs;

namespace {

const size_t kMaxItems = 20000;

void rangeOf(const SyntaxNode& node, uint64_t& pos, uint64_t& len) {
  if (node.children.empty()) {
    pos = node.bit_pos;
    len = node.bit_len;
    return;
  }
  uint64_t min_pos = static_cast<uint64_t>(-1);
  uint64_t max_end = 0;
  for (const auto& child : node.children) {
    uint64_t p = 0, l = 0;
    rangeOf(child, p, l);
    if (l == 0) continue;
    if (p < min_pos) min_pos = p;
    if (p + l > max_end) max_end = p + l;
  }
  if (min_pos == static_cast<uint64_t>(-1)) {
    pos = 0;
    len = 0;
  } else {
    pos = min_pos;
    len = max_end - min_pos;
  }
}

std::wstring nodeText(const SyntaxNode& node) {
  std::wstring text = wide(node.name);
  if (!node.value.empty()) text += L" = " + wide(node.value);
  if (node.bit_len > 0) {
    text += L"   [" + std::to_wstring(node.bit_pos) + L".." +
            std::to_wstring(node.bit_pos + node.bit_len - 1) + L"]";
  }
  return text;
}

}  // namespace

void SyntaxTree::create(HWND parent, HINSTANCE inst, int id) {
  parent_ = parent;
  hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
      WS_CHILD | WS_VISIBLE | WS_BORDER | WS_HSCROLL | TVS_HASLINES |
          TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
      0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
      inst, nullptr);
  if (!hwnd_) return;
  TreeView_SetExtendedStyle(hwnd_, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
  SendMessageW(hwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(monoFont()), TRUE);
}

void SyntaxTree::clear() {
  TreeView_DeleteAllItems(hwnd_);
  items_.clear();
  selection_ = Selection();
}

HTREEITEM SyntaxTree::addNode(HTREEITEM parent, const SyntaxNode& node,
                              size_t nal, int depth) {
  if (items_.size() >= kMaxItems || depth > 40) return nullptr;
  TVINSERTSTRUCTW ins = {};
  ins.hParent = parent;
  ins.hInsertAfter = TVI_LAST;
  ins.item.mask = TVIF_TEXT | TVIF_PARAM;
  std::wstring text = nodeText(node);
  ins.item.pszText = const_cast<wchar_t*>(text.c_str());

  uint64_t pos = 0, len = 0;
  rangeOf(node, pos, len);
  Item item;
  item.nal = nal;
  item.bit_pos = pos;
  item.bit_len = len;
  item.is_error = node.is_error;
  item.header = node.nal_header;
  items_.push_back(item);
  ins.item.lParam = static_cast<LPARAM>(items_.size() - 1);
  HTREEITEM h = TreeView_InsertItem(hwnd_, &ins);
  if (!h) return nullptr;
  for (const auto& child : node.children) addNode(h, child, nal, depth + 1);
  if (depth <= 1 && !node.children.empty())
    TreeView_Expand(hwnd_, h, TVE_EXPAND);
  return h;
}

void SyntaxTree::setFrame(const Stream* stream, int frame_index) {
  clear();
  stream_ = stream;
  if (!stream || frame_index < 0) return;
  const auto& frames = stream->frames();
  if (frame_index >= static_cast<int>(frames.size())) return;
  const auto& nals = stream->nals();
  for (size_t i = 0; i < nals.size(); i++) {
    if (nals[i].frame_index != static_cast<size_t>(frame_index)) continue;
    std::wstring root_text = L"[" + std::to_wstring(i) + L"] " +
                             wide(nals[i].unit.type_name) + L"   (" +
                             std::to_wstring(nals[i].unit.size) + L" B)";
    TVINSERTSTRUCTW ins = {};
    ins.hParent = TVI_ROOT;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT | TVIF_PARAM;
    ins.item.pszText = const_cast<wchar_t*>(root_text.c_str());
    Item root_item;
    root_item.nal = i;
    root_item.header = true;
    items_.push_back(root_item);
    ins.item.lParam = static_cast<LPARAM>(items_.size() - 1);
    HTREEITEM root = TreeView_InsertItem(hwnd_, &ins);
    if (!root) continue;
    std::vector<SyntaxNode> tree = stream->syntaxTree(i);
    for (const auto& node : tree) addNode(root, node, i, 0);
    // Children must exist before expanding, otherwise there is nothing to show.
    TreeView_Expand(hwnd_, root, TVE_EXPAND);
  }
  // Show the first NAL of the frame right away.
  if (HTREEITEM first = TreeView_GetRoot(hwnd_))
    TreeView_SelectItem(hwnd_, first);
}

const SyntaxTree::Selection& SyntaxTree::selection() const {
  return selection_;
}

LRESULT SyntaxTree::onCustomDraw(NMTVCUSTOMDRAW* cd) {
  switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
      return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
      size_t idx = static_cast<size_t>(cd->nmcd.lItemlParam);
      if (idx < items_.size() && items_[idx].is_error)
        cd->clrText = RGB(200, 30, 30);
      return CDRF_NEWFONT;
    }
    default:
      break;
  }
  return CDRF_DODEFAULT;
}

LRESULT SyntaxTree::handleNotify(NMHDR* hdr) {
  if (hdr->code == TVN_SELCHANGEDW) {
    auto* tv = reinterpret_cast<NMTREEVIEWW*>(hdr);
    size_t idx = static_cast<size_t>(tv->itemNew.lParam);
    if (idx < items_.size()) {
      selection_.nal = items_[idx].nal;
      selection_.bit_pos = items_[idx].bit_pos;
      selection_.bit_len = items_[idx].bit_len;
      selection_.header = items_[idx].header;
    } else {
      selection_ = Selection();
    }
    SendMessageW(parent_, WM_APP_SYNTAX_SELECTED, 0, 0);
    return 0;
  }
  if (hdr->code == NM_CUSTOMDRAW)
    return onCustomDraw(reinterpret_cast<NMTVCUSTOMDRAW*>(hdr));
  return 0;
}

}  // namespace ui
