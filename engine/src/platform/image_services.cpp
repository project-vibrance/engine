// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/platform/image_services.h>
#include <vibranceUI/platform/native_dialog.h>



namespace colour_palette
{
std::optional<ImageSource> choose_image_file(void* owner, std::string& error)
{
    NativeFileDialogOptions options;
    options.title = "New Image Palette";
    options.parentWindow = owner;
    options.filters = { { "Images", { "png", "jpg", "jpeg", "bmp", "gif", "tif", "tiff", "webp", "heic" } } };
    options.allowAllFiles = true;
    const auto path = show_native_file_dialog(options, error);
    if (!path) return {};
    ImageSource source;
    source.path = *path;
    const auto name = path->stem().u8string();
    source.name.assign(reinterpret_cast<const char*>(name.data()), name.size());
    return source;
}
}
