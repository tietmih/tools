#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ui/video_view.h"

namespace ui {

bool writeImageFile(const std::wstring& path, const ImageData& image,
                    std::string& error);
bool writeRawYuv(const std::wstring& path, const ImageData& image,
                 std::string& error);
bool writeBinaryFile(const std::wstring& path, const std::vector<uint8_t>& data,
                     std::string& error);

}  // namespace ui
