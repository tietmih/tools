#include "ui/ui_util.h"

#include <commctrl.h>

namespace ui {

namespace {
int g_dpi = 96;
HFONT g_ui = nullptr;
HFONT g_ui_bold = nullptr;
HFONT g_mono = nullptr;

HFONT makeFont(int pt, bool bold, const wchar_t *face)
{
    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(pt, g_dpi, 72);
    lf.lfWeight = bold ? FW_SEMIBOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    return CreateFontIndirectW(&lf);
}
} // namespace

void initDpi()
{
    HDC dc = GetDC(nullptr);
    if (dc) {
        g_dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(nullptr, dc);
    }
    if (g_dpi <= 0)
        g_dpi = 96;
}

int px(int value) { return MulDiv(value, g_dpi, 96); }

HFONT uiFont()
{
    if (!g_ui)
        g_ui = makeFont(9, false, L"Segoe UI");
    return g_ui;
}

HFONT uiFontBold()
{
    if (!g_ui_bold)
        g_ui_bold = makeFont(9, true, L"Segoe UI");
    return g_ui_bold;
}

HFONT monoFont()
{
    if (!g_mono)
        g_mono = makeFont(9, false, L"Consolas");
    return g_mono;
}

void initCommonControls()
{
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES |
                ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);
}

std::wstring wide(const std::string &s)
{
    if (s.empty())
        return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        &out[0], n);
    return out;
}

std::string utf8(const std::wstring &s)
{
    if (s.empty())
        return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        &out[0], n, nullptr, nullptr);
    return out;
}

COLORREF frameColor(int picture_type, bool idr, bool irap, bool hevc,
                    int nal_type)
{
    if (idr)
        return RGB(196, 48, 48);
    if (hevc && irap) {
        if (nal_type == 21)
            return RGB(170, 70, 180); // CRA
        return RGB(210, 90, 60);      // BLA
    }
    switch (picture_type) {
    case 'I': return RGB(216, 128, 32);
    case 'P': return RGB(52, 108, 200);
    case 'B': return RGB(46, 148, 88);
    default: return RGB(120, 120, 120);
    }
}

} // namespace ui
