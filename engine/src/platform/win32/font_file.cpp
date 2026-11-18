// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include "platform/font_file_native.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwrite.h>

namespace font_files
{

bool native_font_valid(const std::filesystem::path& source)
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute(source, error);
    if (error) return false;
    IDWriteFactory* factory = nullptr;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&factory)))) return false;
    IDWriteFontFile* file = nullptr;
    const auto created = factory->CreateFontFileReference(absolute.c_str(), nullptr, &file);
    BOOL supported = FALSE;
    DWRITE_FONT_FILE_TYPE fileType {};
    DWRITE_FONT_FACE_TYPE faceType {};
    UINT32 faces = 0u;
    const auto analysed = SUCCEEDED(created) ? file->Analyze(&supported, &fileType, &faceType, &faces) : E_FAIL;
    if (file) file->Release();
    factory->Release();
    return SUCCEEDED(analysed) && supported && faces > 0u;
}

}
