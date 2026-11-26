#include "ui/image_export.h"

#include <windows.h>

#include <wincodec.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

namespace ui {

namespace {

std::wstring lowerExtension(const std::wstring& path) {
  size_t dot = path.find_last_of(L'.');
  if (dot == std::wstring::npos) return L"";
  std::wstring ext = path.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
  return ext;
}

void release(IUnknown* object) {
  if (object) object->Release();
}

}  // namespace

bool writeImageFile(const std::wstring& path, const ImageData& image,
                    std::string& error) {
  if (image.bgra.empty() || image.width <= 0 || image.height <= 0) {
    error = "no decoded image";
    return false;
  }
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool uninit = SUCCEEDED(hr);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    error = "COM initialization failed";
    return false;
  }

  IWICImagingFactory* factory = nullptr;
  hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                        IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    if (uninit) CoUninitialize();
    error = "WIC factory creation failed";
    return false;
  }

  IWICStream* stream = nullptr;
  IWICBitmapEncoder* encoder = nullptr;
  IWICBitmapFrameEncode* frame = nullptr;
  IPropertyBag2* properties = nullptr;
  bool ok = true;
  const std::wstring ext = lowerExtension(path);
  const GUID& format =
      ext == L".bmp" ? GUID_ContainerFormatBmp : GUID_ContainerFormatPng;

  if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
  if (SUCCEEDED(hr))
    hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
  if (SUCCEEDED(hr)) hr = factory->CreateEncoder(format, nullptr, &encoder);
  if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
  if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &properties);
  if (SUCCEEDED(hr)) hr = frame->Initialize(properties);
  if (SUCCEEDED(hr)) hr = frame->SetSize(image.width, image.height);
  WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppBGRA;
  if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&pixel_format);
  if (SUCCEEDED(hr)) {
    hr = frame->WritePixels(static_cast<UINT>(image.height),
                            static_cast<UINT>(image.width) * 4,
                            static_cast<UINT>(image.bgra.size()),
                            const_cast<BYTE*>(image.bgra.data()));
  }
  if (SUCCEEDED(hr)) hr = frame->Commit();
  if (SUCCEEDED(hr)) hr = encoder->Commit();
  if (FAILED(hr)) {
    error = "image encoding failed";
    ok = false;
  }

  release(properties);
  release(frame);
  release(encoder);
  release(stream);
  release(factory);
  if (uninit) CoUninitialize();
  return ok;
}

bool writeRawYuv(const std::wstring& path, const ImageData& image,
                 std::string& error) {
  if (image.planes.empty() || image.width <= 0 || image.height <= 0) {
    error = "no decoded YUV planes";
    return false;
  }
  // Raw >8-bit YUV is written little-endian with the sample in the low bits,
  // matching ffmpeg rawvideo and the reference YUV reader.
  const bool wide = image.bit_depth > 8;
  FILE* out = _wfopen(path.c_str(), L"wb");
  if (!out) {
    error = "cannot open the output file";
    return false;
  }
  bool ok = true;
  for (const auto& plane : image.planes) {
    if (!wide) {
      if (std::fwrite(plane.data(), 1, plane.size(), out) != plane.size())
        ok = false;
    } else {
      if (std::fwrite(plane.data(), 1, plane.size(), out) != plane.size())
        ok = false;
    }
    if (!ok) break;
  }
  std::fclose(out);
  if (!ok) {
    error = "cannot write the output file";
    return false;
  }
  return true;
}

bool writeBinaryFile(const std::wstring& path, const std::vector<uint8_t>& data,
                     std::string& error) {
  FILE* out = _wfopen(path.c_str(), L"wb");
  if (!out) {
    error = "cannot open the output file";
    return false;
  }
  const bool ok = std::fwrite(data.data(), 1, data.size(), out) == data.size();
  std::fclose(out);
  if (!ok) error = "cannot write the output file";
  return ok;
}

}  // namespace ui
