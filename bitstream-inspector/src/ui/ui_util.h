#pragma once

#include <windows.h>

#include <string>

namespace ui {

void initCommonControls();
void initDpi();
int px(int value);
HFONT uiFont();
HFONT uiFontBold();
HFONT monoFont();

std::wstring wide(const std::string& utf8);
std::string utf8(const std::wstring& wide);

COLORREF frameColor(int picture_type, bool idr, bool irap, bool hevc,
                    int nal_type);

}  // namespace ui
