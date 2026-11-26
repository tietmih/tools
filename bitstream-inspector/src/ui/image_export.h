#pragma once

#include <string>

#include "ui/video_view.h"

namespace ui {

bool writeImageFile(const std::wstring& path, const ImageData& image,
                    std::string& error);
bool writeRawYuv(const std::wstring& path, const ImageData& image,
                 std::string& error);

}  // namespace ui
