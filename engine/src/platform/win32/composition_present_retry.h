#pragma once

#include "composition_bridge_abi.h"

namespace vibrance::win32
{
// Once a frame has been copied, its keyed mutex belongs to Vulkan again.
// Retrying a busy DXGI present must only resubmit that retained back buffer.
class CompositionPresentRetry
{
public:
    template <typename Prepare, typename Present>
    std::uint32_t deliver(std::uint32_t index, Prepare&& prepare, Present&& present)
    {
        if (pending_ && index != index_)
        {
            return VIBRANCE_COMPOSITION_PRESENT_DEFERRED;
        }
        if (!pending_)
        {
            const std::uint32_t prepared = prepare();
            if (prepared != VIBRANCE_COMPOSITION_PRESENTED)
            {
                return prepared;
            }
            pending_ = true;
            index_ = index;
        }
        const std::uint32_t result = present();
        if (result != VIBRANCE_COMPOSITION_PRESENT_DEFERRED)
        {
            pending_ = false;
        }
        return result;
    }

private:
    bool pending_ = false;
    std::uint32_t index_ = 0u;
};
}
