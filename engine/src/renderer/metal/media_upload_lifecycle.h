#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>

namespace vibrance::metal
{
enum class MediaUploadCompletion { ePending, eSucceeded, eFailed };
enum class MediaUploadAdmission { ePending, eRejected, eAdmitted };

// Tickets expose a non-blocking status query. The same admission/retirement
// decisions can therefore be checked without requiring a native Metal SDK.
template<class Ticket> struct MediaUploadLifecycle
{
    Ticket ticket;
    bool cancelled = false;
    bool finalFrame = false;

    bool retired() const { return ticket.completion() != MediaUploadCompletion::ePending; }
    bool can_upload() const
    {
        return !cancelled && !finalFrame &&
            ticket.completion() == MediaUploadCompletion::eSucceeded;
    }
    bool can_publish() const
    {
        return !cancelled && finalFrame &&
            ticket.completion() == MediaUploadCompletion::eSucceeded;
    }
};

struct MediaUploadBudget
{
    static constexpr std::size_t limit = 192u * 1024u * 1024u;
    static constexpr std::size_t entryLimit = 8u;
    std::size_t reservedBytes = 0;

    static constexpr std::size_t reservation(std::size_t decodedBytes)
    {
        // Texture mip chains plus the current upload fit below twice the
        // decoded pixel size. Saturate before multiplication can overflow.
        return decodedBytes > limit / 2u ? std::numeric_limits<std::size_t>::max() : decodedBytes * 2u;
    }
    bool reserve(std::size_t bytes)
    {
        if (bytes > limit || reservedBytes > limit - bytes) return false;
        reservedBytes += bytes;
        return true;
    }
    MediaUploadAdmission admit(std::size_t bytes, std::size_t activeAndRetiredEntries)
    {
        if (bytes > limit) return MediaUploadAdmission::eRejected;
        if (activeAndRetiredEntries >= entryLimit || !reserve(bytes))
            return MediaUploadAdmission::ePending;
        return MediaUploadAdmission::eAdmitted;
    }
    void release(std::size_t bytes) { reservedBytes -= std::min(bytes, reservedBytes); }
};
}
