// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>




enum class RenderBackend : std::uint32_t
{
    eVulkan = 0u,
    eDirectX11 = 1u,
    eDirectX12 = 2u,
    eMetal = 3u
};

enum class PresentationBackend : std::uint32_t
{
    eNative = 0u,
    eWindowsCompositionD3D11 = 1u,
    eWindowsCompositionD3D12 = 2u
};


constexpr RenderBackend default_render_backend() noexcept
{
#if defined(__APPLE__)
    return RenderBackend::eMetal;
#else
    return RenderBackend::eVulkan;
#endif
}
