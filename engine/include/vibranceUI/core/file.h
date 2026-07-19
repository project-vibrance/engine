#pragma once
#include "vibranceUI/export.h"
#include <vector>

// Binary file helper used by shader and asset loading paths
VIBRANCE_ENGINE_API std::vector<char> read_file(const char* fileName);
