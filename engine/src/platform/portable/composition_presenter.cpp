#if !defined(_WIN32) || defined(VIBRANCE_FORCE_PORTABLE_COMPOSITION)
#include "../composition_presenter.h"

// No compositor backend is installed for this platform. The renderer selects
// its ordinary Vulkan swapchain; future native backends implement this facade.
struct CompositionPresenter::Impl {};
CompositionPresenter::CompositionPresenter() = default;
CompositionPresenter::~CompositionPresenter() = default;
bool CompositionPresenter::initialise(void*, vk::PhysicalDevice, vk::Device,
    std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, bool, bool) { return false; }
void CompositionPresenter::shutdown(vk::Device) {}
bool CompositionPresenter::available() const { return false; }
bool CompositionPresenter::gpu_interop() const { return false; }
std::uint32_t CompositionPresenter::buffer_count() const { return 0; }
vk::Image CompositionPresenter::image(std::uint32_t) const { return {}; }
vk::DeviceMemory CompositionPresenter::memory(std::uint32_t) const { return {}; }
bool CompositionPresenter::first_use(std::uint32_t) const { return false; }
void CompositionPresenter::mark_used(std::uint32_t) {}
bool CompositionPresenter::acquire(std::uint32_t) { return false; }
CompositionPresenter::PresentResult CompositionPresenter::present(
    std::uint32_t, bool, glm::uvec4, glm::uvec4) { return PresentResult::eFailed; }
bool CompositionPresenter::upload(std::uint32_t, const void*, std::uint32_t,
    std::uint32_t, std::uint32_t) { return false; }
bool CompositionPresenter::set_regions(const std::vector<SystemBackdropRegion>&) { return false; }
#endif
