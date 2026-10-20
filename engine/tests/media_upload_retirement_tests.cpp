#include "../src/renderer/metal/media_upload_lifecycle.h"
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>

namespace
{
using namespace vibrance::metal;
unsigned checksRun = 0;
struct FakeTicket
{
    const unsigned* elapsedMilliseconds = nullptr;
    unsigned completionMilliseconds = 0;
    bool failed = false;
    std::shared_ptr<unsigned> image;

    MediaUploadCompletion completion() const
    {
        if (elapsedMilliseconds && *elapsedMilliseconds < completionMilliseconds)
            return MediaUploadCompletion::ePending;
        return failed ? MediaUploadCompletion::eFailed : MediaUploadCompletion::eSucceeded;
    }
    // There is deliberately no blocking wait API. Instantiating the real
    // production policy with this ticket only permits polling its status.
};
bool expect(bool condition, std::string_view message)
{
    ++checksRun;
    if (!condition) std::cerr << message << '\n';
    return condition;
}
}

int main()
{
    using namespace vibrance::metal;
    bool passed = true;
    unsigned elapsed = 0;
    MediaUploadLifecycle<FakeTicket> state;
    passed &= expect(state.can_upload() && !state.can_publish(), "An empty ticket can admit the first frame");
    state.ticket = { &elapsed, 100u, false, std::make_shared<unsigned>(1u) };
    const std::weak_ptr<unsigned> retainedImage = state.ticket.image;
    for (elapsed = 0; elapsed < 100u; elapsed += 5u)
    {
        passed &= expect(!state.retired() && !state.can_upload() && !state.can_publish(),
            "A 100 ms delayed upload must permit neither another upload nor publication");
        passed &= expect(!retainedImage.expired(), "The delayed command must retain its texture");
    }
    passed &= expect(state.retired() && state.can_upload() && !state.can_publish(),
        "A completed intermediate frame permits the next upload");
    state.finalFrame = true;
    state.ticket.completionMilliseconds = 200u;
    passed &= expect(!state.can_publish(), "The last frame must retire before a handle can be published");
    elapsed = 200u;
    passed &= expect(state.can_publish() && !state.can_upload(), "Only a successful final completion permits publication");
    state.cancelled = true;
    passed &= expect(!state.can_publish() && !state.can_upload(), "Cancellation prevents every later publication");

    MediaUploadBudget budget;
    const auto bytes = MediaUploadBudget::reservation(48u * 1024u * 1024u);
    passed &= expect(budget.reserve(bytes), "A bounded animation must fit the pending upload budget");
    passed &= expect(budget.reserve(bytes) && !budget.reserve(1u), "Pending and retired uploads share the 192 MiB ceiling");
    passed &= expect(budget.admit(bytes, 2u) == MediaUploadAdmission::ePending,
        "Aggregate budget pressure must remain retryable rather than rejecting a valid image");
    MediaUploadLifecycle<FakeTicket> cancelled;
    elapsed = 0;
    cancelled.ticket = { &elapsed, 100u, false, std::make_shared<unsigned>(2u) };
    cancelled.cancelled = true;
    const std::weak_ptr<unsigned> cancelledImage = cancelled.ticket.image;
    unsigned admittedUiTicks = 0;
    for (; elapsed < 100u; elapsed += 5u)
    {
        ++admittedUiTicks;
        if (cancelled.cancelled && cancelled.retired())
        {
            cancelled.ticket = {};
            budget.release(bytes);
        }
        passed &= expect(!cancelledImage.expired() && !budget.reserve(1u),
            "Cancelled in-flight textures and their budget must remain owned until terminal status");
    }
    passed &= expect(admittedUiTicks == 20u, "UI admission must continue throughout a 100 ms GPU delay");
    if (cancelled.cancelled && cancelled.retired())
    {
        cancelled.ticket = {};
        budget.release(bytes);
    }
    passed &= expect(cancelledImage.expired() && budget.reserve(1u),
        "Retirement must release both the cancelled texture and its budget");
    budget.release(1u);
    passed &= expect(budget.admit(bytes, 1u) == MediaUploadAdmission::eAdmitted,
        "An image held by aggregate pressure must be admitted after retirement frees capacity");

    state.cancelled = false;
    state.ticket.failed = true;
    elapsed = 200u;
    passed &= expect(state.retired() && !state.can_upload() && !state.can_publish(),
        "An error is terminal for ownership but must never publish an image");
    passed &= expect(!budget.reserve(MediaUploadBudget::reservation(std::numeric_limits<std::size_t>::max())),
        "Overflow-sized input must fail admission before arithmetic wraps");
    passed &= expect(budget.admit(MediaUploadBudget::reservation(std::numeric_limits<std::size_t>::max()), 0u) ==
        MediaUploadAdmission::eRejected, "An individually oversized image must fail permanently");
    budget.release(std::numeric_limits<std::size_t>::max());
    passed &= expect(budget.reservedBytes == 0u, "Budget release must not underflow");
    for (std::size_t entries = 0; entries < MediaUploadBudget::entryLimit; ++entries)
        passed &= expect(budget.admit(0u, entries) == MediaUploadAdmission::eAdmitted,
            "Small uploads must remain eligible below the active/retired entry limit");
    passed &= expect(budget.admit(0u, MediaUploadBudget::entryLimit) == MediaUploadAdmission::ePending &&
        budget.reservedBytes == 0u, "Eight stalled tiny uploads must block a ninth without consuming budget");
    if (passed) std::cout << "Metal upload lifecycle checks passed: " << checksRun << '\n';
    return passed ? 0 : 1;
}
