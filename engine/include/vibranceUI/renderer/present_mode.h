#pragma once

#include <cstdint>

enum class RendererPresentMode : uint32_t
{
    eAuto = 0,
    eImmediate,
    eMailbox,
    eFifo,
    eFifoRelaxed
};

inline const char* renderer_present_mode_name(RendererPresentMode mode)
{
    switch (mode)
    {
    case RendererPresentMode::eImmediate:
        return "Immediate";
    case RendererPresentMode::eMailbox:
        return "Mailbox";
    case RendererPresentMode::eFifo:
        return "FIFO";
    case RendererPresentMode::eFifoRelaxed:
        return "FIFO relaxed";
    case RendererPresentMode::eAuto:
    default:
        return "Auto";
    }
}
