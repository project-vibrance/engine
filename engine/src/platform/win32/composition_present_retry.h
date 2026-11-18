// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "composition_bridge_abi.h"

namespace vibrance::win32
{


class CompositionLatencyHint
{
public:
    bool may_prepare(bool signalled, std::uint64_t now) noexcept
    {
        if (unreliable_ || signalled)
        {
            blocked_ = false;
            return true;
        }
        if (!blocked_)
        {
            blocked_ = true;
            blockedSince_ = now;
        }
        if (now - blockedSince_ < 32u) return false;
        unreliable_ = true;
        return true;
    }

private:
    bool blocked_ = false;
    bool unreliable_ = false;
    std::uint64_t blockedSince_ = 0u;
};

class CompositionPresentStall
{
public:
    bool needs_recovery(std::uint32_t result, bool visible, std::uint64_t now) noexcept
    {
        if (result != VIBRANCE_COMPOSITION_PRESENT_DEFERRED || !visible)
        {
            deferred_ = false;
            return false;
        }
        if (!deferred_)
        {
            deferred_ = true;
            deferredSince_ = now;
        }
        return now - deferredSince_ >= 1000u;
    }

private:
    bool deferred_ = false;
    std::uint64_t deferredSince_ = 0u;
};



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
