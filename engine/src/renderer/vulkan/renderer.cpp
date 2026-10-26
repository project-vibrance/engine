#include <vibranceUI/renderer/renderer.h>
#include <vibranceUI/renderer/device.h>
#include <vibranceUI/renderer/shader.h>
#include <vibranceUI/renderer/command.h>
#include <vibranceUI/renderer/descriptors.h>
#include <vibranceUI/core/logger.h>
#include <vibranceUI/renderer/instance.h>
#include <vibranceUI/renderer/frame.h>
#include <vibranceUI/renderer/swapchain.h>
#include <vibranceUI/factories/mesh_factory.h>
#include <vibranceUI/ui/text.h>
#include <vibranceUI/ui/layout_direction.h>
#include "../../platform/composition_presenter.h"
#include "pipeline_cache.h"
#include "media_pipeline_policy.h"
#include "composition_readback.h"
#include "composition_frame_delivery.h"
#include "../common/media_prepared_upload.h"
#if defined(_WIN32)
#include "../../platform/win32/composition_bridge_abi.h"
#endif
#include <sstream>
#include <string_view>
#include <deque>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vma/vk_mem_alloc.h>
#include <filesystem>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

#if defined(__APPLE__)
#include <mach/mach_time.h>
#elif !defined(_WIN32) && (defined(__unix__) || defined(__linux__))
#include <cerrno>
#include <ctime>
#endif

namespace
{
	constexpr uint32_t kCompositionBufferCount = 2u;
	constexpr uint64_t kGpuTimestampSampleInterval = 16u;

	enum class RendererCpuPhase : std::size_t
	{
		eRetainedCheck,
		eCadenceCheck,
		eRedrawCheck,
		eGlyphScan,
		eCommandRecording,
		eBackdropGeometry,
		eInteropAcquire,
		eSurfaceAcquire,
		eQueueSubmit,
		eFenceWait,
        eHostUpload,
		eBackdropApply,
		ePresent,
		eCount
	};

	struct RendererCpuWallTiming
	{
		uint64_t samples = 0u;
		double totalMilliseconds = 0.0;
		double maxMilliseconds = 0.0;
	};

	class RendererCpuTimingScope
	{
	public:
		explicit RendererCpuTimingScope(RendererCpuWallTiming* timing) : timing_(timing)
		{
			// Disabled diagnostics perform no clock reads or aggregation.
			if (timing_)
			{
				start_ = std::chrono::steady_clock::now();
			}
		}

		~RendererCpuTimingScope()
		{
			if (timing_)
			{
				const double milliseconds = std::chrono::duration<double, std::milli>(
					std::chrono::steady_clock::now() - start_).count();
				++timing_->samples;
				timing_->totalMilliseconds += milliseconds;
				timing_->maxMilliseconds = std::max(timing_->maxMilliseconds, milliseconds);
			}
		}

		RendererCpuTimingScope(const RendererCpuTimingScope&) = delete;
		RendererCpuTimingScope& operator=(const RendererCpuTimingScope&) = delete;

	private:
		RendererCpuWallTiming* timing_ = nullptr;
		std::chrono::steady_clock::time_point start_ {};
	};

	// A shared UI thread ticks every window before asking for its next event
	// wait. Only let that thread sleep when every renderer retained its image;
	// one animating or pending window keeps the normal presentation cadence.
	thread_local bool rendererDrawSeen = false;
	thread_local bool allRendererDrawsSettled = true;
	thread_local bool rendererHostUsesIdleEventWait = false;
	thread_local bool externallyPacedFrameActive = false;
	thread_local uint32_t externallyPacedFrameRate = 60u;

	struct RendererDrawActivity
	{
		bool settled = false;

		RendererDrawActivity()
		{
			rendererDrawSeen = true;
		}

		~RendererDrawActivity()
		{
			allRendererDrawsSettled &= settled;
		}
	};

	vk::Extent2D hosted_3d_extent(
		vk::Extent2D renderExtent,
		Renderer2DScene& scene)
	{
		return scene.registry().view<Model3DComponent>().empty() ?
			vk::Extent2D { 1u, 1u } : renderExtent;
	}


#if defined(_WIN32)
	void restore_native_window_redirection(
		void* nativeWindowHandle,
		bool transparentFramebuffer)
	{
		if (!nativeWindowHandle || !transparentFramebuffer)
		{
			return;
		}
		HWND window = static_cast<HWND>(nativeWindowHandle);
		SetLastError(ERROR_SUCCESS);
		const LONG_PTR extendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
		if (extendedStyle == 0 && GetLastError() != ERROR_SUCCESS)
		{
			return;
		}
		if ((extendedStyle & WS_EX_NOREDIRECTIONBITMAP) == 0)
		{
			return;
		}
		(void)SetWindowLongPtrW(
			window,
			GWL_EXSTYLE,
			extendedStyle &
				~static_cast<LONG_PTR>(WS_EX_NOREDIRECTIONBITMAP));
		(void)SetWindowPos(
			window,
			nullptr,
			0,
			0,
			0,
			0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
				SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}
#endif

	constexpr uint32_t kMaxModel3DMaterialSets = 2048;
	constexpr uint32_t kMaxMedia2DTextures = 2048;

	struct RetainedDamageTracker
	{
		std::vector<glm::uvec4> pendingRects;

		void clear()
		{
			pendingRects.clear();
		}

		void reset(uint32_t bufferCount, glm::uvec4 initialDamage = glm::uvec4(0u))
		{
			pendingRects.assign(bufferCount, initialDamage);
		}

		void ensure_buffer_count(uint32_t bufferCount, glm::uvec4 fullDamage)
		{
			if (pendingRects.size() != bufferCount)
			{
				// A changed imported-image set has no trustworthy retained contents.
				// Force each new buffer through one complete resynchronisation.
				reset(bufferCount, fullDamage);
			}
		}

		glm::uvec4 pending_for(uint32_t bufferIndex) const
		{
			return bufferIndex < pendingRects.size() ?
				pendingRects[bufferIndex] : glm::uvec4(0u);
		}

		bool has_pending() const
		{
			return std::any_of(
				pendingRects.begin(),
				pendingRects.end(),
				[](const glm::uvec4& rect) {
					return rect.z > 0u && rect.w > 0u;
				});
		}

		void commit(uint32_t renderedBufferIndex, glm::uvec4 sceneDamage)
		{
			for (glm::uvec4& pendingDamage : pendingRects)
			{
				pendingDamage = union_rect(pendingDamage, sceneDamage);
			}
			if (renderedBufferIndex < pendingRects.size())
			{
				pendingRects[renderedBufferIndex] = glm::uvec4(0u);
			}
		}

	private:
		static glm::uvec4 union_rect(glm::uvec4 left, glm::uvec4 right)
		{
			if (left.z == 0u || left.w == 0u)
			{
				return right;
			}
			if (right.z == 0u || right.w == 0u)
			{
				return left;
			}
			const uint32_t x = std::min(left.x, right.x);
			const uint32_t y = std::min(left.y, right.y);
			const uint32_t rightEdge = std::max(left.x + left.z, right.x + right.z);
			const uint32_t bottomEdge = std::max(left.y + left.w, right.y + right.w);
			return { x, y, rightEdge - x, bottomEdge - y };
		}
	};

	std::string resource_file_cache_version(const std::filesystem::path& path)
	{
		std::error_code error;
		const std::uintmax_t size = std::filesystem::file_size(path, error);
		const std::uintmax_t safeSize = error ? 0u : size;
		error.clear();
		const auto writeTime = std::filesystem::last_write_time(path, error);
		// libc++ may expose the filesystem clock count as a 128-bit integer,
		// which has no std::to_string overload. The cache stamp only needs a
		// stable, process-independent scalar representation.
		const std::int64_t writeTicks = error
			? 0
			: static_cast<std::int64_t>(writeTime.time_since_epoch().count());
		return std::to_string(safeSize) + "@" + std::to_string(writeTicks);
	}

	vk::Extent2D choose_render_extent(uint32_t width, uint32_t height, uint32_t maxRenderPixels)
	{
		// Optional pixel cap reduces expensive offscreen work on very large windows
		if (width == 0 || height == 0)
		{
			return { width, height };
		}

		const uint64_t pixelCount = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
		if (maxRenderPixels == 0 || pixelCount <= static_cast<uint64_t>(maxRenderPixels))
		{
			return { width, height };
		}

		const double scale = std::sqrt(static_cast<double>(maxRenderPixels) / static_cast<double>(pixelCount));
		return {
			std::max(1u, static_cast<uint32_t>(std::round(static_cast<double>(width) * scale))),
			std::max(1u, static_cast<uint32_t>(std::round(static_cast<double>(height) * scale)))
		};
	}

	bool same_backdrop_region(
		const SystemBackdropRegion& left,
		const SystemBackdropRegion& right)
	{
		const auto close = [](float a, float b) {
			return a == b ||
				(std::isfinite(a) && std::isfinite(b) &&
					std::abs(a - b) <= 0.05f);
		};
        return left.identity == right.identity &&
            left.material == right.material &&
			left.liquidBackend == right.liquidBackend &&
			left.provider == right.provider &&
			left.shape == right.shape &&
			close(left.x, right.x) && close(left.y, right.y) &&
			close(left.width, right.width) && close(left.height, right.height) &&
			close(left.cornerRadius, right.cornerRadius) &&
			close(left.topLeftRadius, right.topLeftRadius) &&
			close(left.topRightRadius, right.topRightRadius) &&
			close(left.bottomRightRadius, right.bottomRightRadius) &&
			close(left.bottomLeftRadius, right.bottomLeftRadius) &&
            left.squircleAmount == right.squircleAmount &&
            left.squirclePower == right.squirclePower &&
            left.notchAmount == right.notchAmount &&
			close(left.notchDepth, right.notchDepth) &&
			// Optical settings change in 1% steps; geometry's tolerance would
			// suppress those updates and leave the native shader's values stale.
			left.liquidRefraction == right.liquidRefraction &&
			left.liquidBlur == right.liquidBlur &&
			left.liquidRim == right.liquidRim &&
			left.liquidRimAnimated == right.liquidRimAnimated &&
            left.liquidDispersion == right.liquidDispersion &&
            left.liquidTint.red == right.liquidTint.red &&
            left.liquidTint.green == right.liquidTint.green &&
            left.liquidTint.blue == right.liquidTint.blue &&
            left.liquidTint.alpha == right.liquidTint.alpha &&
            left.contourCount == right.contourCount &&
            left.hasExclusion == right.hasExclusion &&
            left.clipRawBackdropToShape == right.clipRawBackdropToShape &&
            left.paintCapGradient == right.paintCapGradient &&
            left.capGradient == right.capGradient && left.capColour == right.capColour &&
            left.viewportRect == right.viewportRect && left.viewportFade == right.viewportFade &&
            std::memcmp(left.contours.data(), right.contours.data(),
                sizeof(left.contours)) == 0 &&
            (!left.hasExclusion || std::memcmp(&left.exclusion,
                &right.exclusion, sizeof(left.exclusion)) == 0) &&
			close(left.verticalStart, right.verticalStart) &&
			std::abs(left.opacity - right.opacity) <= 0.002f &&
			close(left.blurRadius, right.blurRadius) &&
			close(left.saturation, right.saturation) &&
			close(left.tint.red, right.tint.red) &&
			close(left.tint.green, right.tint.green) &&
			close(left.tint.blue, right.tint.blue) &&
			close(left.tint.alpha, right.tint.alpha);
	}

#ifdef _WIN32
	uint32_t monitor_refresh_rate(void* nativeWindowHandle) noexcept
	{
		const HWND window = static_cast<HWND>(nativeWindowHandle);
		const HMONITOR monitor = window ?
			MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) : nullptr;
		MONITORINFOEXW monitorInfo {};
		monitorInfo.cbSize = sizeof(monitorInfo);
		DEVMODEW displayMode {};
		displayMode.dmSize = sizeof(displayMode);
		if (monitor &&
			GetMonitorInfoW(monitor, &monitorInfo) &&
			EnumDisplaySettingsW(
				monitorInfo.szDevice,
				ENUM_CURRENT_SETTINGS,
				&displayMode) &&
			displayMode.dmDisplayFrequency >= 30u)
		{
			return std::clamp<uint32_t>(
				displayMode.dmDisplayFrequency,
				30u,
				360u);
		}
		return 60u;
	}

	struct WindowsFrameTimerResolution
	{
		using TimePeriodFn = UINT(WINAPI*)(UINT);

		HMODULE winmm = nullptr;
		TimePeriodFn timeEndPeriod = nullptr;
		bool active = false;

		WindowsFrameTimerResolution()
			: winmm(LoadLibraryA("winmm.dll"))
		{
			if (!winmm)
			{
				return;
			}

			const auto timeBeginPeriod = reinterpret_cast<TimePeriodFn>(GetProcAddress(winmm, "timeBeginPeriod"));
			timeEndPeriod = reinterpret_cast<TimePeriodFn>(GetProcAddress(winmm, "timeEndPeriod"));
			active = timeBeginPeriod && timeEndPeriod && timeBeginPeriod(1u) == 0u;
		}

		~WindowsFrameTimerResolution()
		{
			if (active && timeEndPeriod)
			{
				timeEndPeriod(1u);
			}
			if (winmm)
			{
				FreeLibrary(winmm);
			}
		}
	};

	struct WindowsFrameWaitTimer
	{
		HANDLE timer = nullptr;

		WindowsFrameWaitTimer()
			: timer(CreateWaitableTimerExW(
				nullptr,
				nullptr,
				CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
				TIMER_ALL_ACCESS))
		{
			if (!timer)
			{
				timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
			}
		}

		~WindowsFrameWaitTimer()
		{
			if (timer)
			{
				CloseHandle(timer);
			}
		}

		bool wait_until(std::chrono::steady_clock::time_point deadline)
		{
			if (!timer)
			{
				return false;
			}

			const auto now = std::chrono::steady_clock::now();
			if (now >= deadline)
			{
				return true;
			}

			const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count();
			LARGE_INTEGER dueTime = {};
			dueTime.QuadPart = -std::max<long long>(1ll, remaining / 100ll);
			if (!SetWaitableTimer(timer, &dueTime, 0, nullptr, nullptr, FALSE))
			{
				return false;
			}
			WaitForSingleObject(timer, INFINITE);
			return true;
		}
	};

	void ensure_frame_timer_resolution()
	{
		static WindowsFrameTimerResolution timerResolution;
		(void)timerResolution;
	}

	bool wait_with_high_resolution_timer(std::chrono::steady_clock::time_point deadline)
	{
		static WindowsFrameWaitTimer timer;
		return timer.wait_until(deadline);
	}
#elif defined(__APPLE__)
	void ensure_frame_timer_resolution()
	{
	}

	bool wait_with_high_resolution_timer(std::chrono::steady_clock::time_point deadline)
	{
		const auto now = std::chrono::steady_clock::now();
		if (now >= deadline)
		{
			return true;
		}

		static mach_timebase_info_data_t timebase = [] {
			mach_timebase_info_data_t info {};
			mach_timebase_info(&info);
			return info;
		}();
		const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count();
		const uint64_t ticks = static_cast<uint64_t>(
			std::max<long long>(1ll, remaining) * static_cast<long double>(timebase.denom) /
			static_cast<long double>(timebase.numer));
		mach_wait_until(mach_absolute_time() + ticks);
		return true;
	}
#elif defined(__unix__) || defined(__linux__)
	void ensure_frame_timer_resolution()
	{
	}

	bool wait_with_high_resolution_timer(std::chrono::steady_clock::time_point deadline)
	{
		const auto now = std::chrono::steady_clock::now();
		if (now >= deadline)
		{
			return true;
		}

		const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count();
		timespec request {};
		request.tv_sec = static_cast<time_t>(remaining / 1000000000ll);
		request.tv_nsec = static_cast<long>(remaining % 1000000000ll);
		while (nanosleep(&request, &request) == -1 && errno == EINTR)
		{
		}
		return true;
	}
#else
	void ensure_frame_timer_resolution()
	{
	}

	bool wait_with_high_resolution_timer(std::chrono::steady_clock::time_point)
	{
		return false;
	}
#endif

	void wait_for_frame_deadline(std::chrono::steady_clock::time_point deadline)
	{
		ensure_frame_timer_resolution();
		// Sleep through the deadline. A 250 us spin on every frame consumes
		// CPU even when the retained scene has nothing to submit.
		if (!wait_with_high_resolution_timer(deadline))
		{
			std::this_thread::sleep_until(deadline);
		}
	}

	const char* sample_count_name(vk::SampleCountFlagBits samples)
	{
		switch (samples)
		{
		case vk::SampleCountFlagBits::e64:
			return "64x";
		case vk::SampleCountFlagBits::e32:
			return "32x";
		case vk::SampleCountFlagBits::e16:
			return "16x";
		case vk::SampleCountFlagBits::e8:
			return "8x";
		case vk::SampleCountFlagBits::e4:
			return "4x";
		case vk::SampleCountFlagBits::e2:
			return "2x";
		case vk::SampleCountFlagBits::e1:
		default:
			return "1x";
		}
	}

	vk::SampleCountFlagBits choose_hosted3d_sample_count(
		vk::PhysicalDevice physicalDevice,
		uint32_t requestedSamples)
	{
		// Hosted 3D uses the highest requested MSAA count supported by colour and depth
		if (!physicalDevice || requestedSamples <= 1)
		{
			return vk::SampleCountFlagBits::e1;
		}

		const vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
		const vk::SampleCountFlags supportedSamples =
			properties.limits.framebufferColorSampleCounts &
			properties.limits.framebufferDepthSampleCounts;

		auto supports = [&](vk::SampleCountFlagBits sampleCount) {
			return static_cast<bool>(supportedSamples & sampleCount);
		};

		if (requestedSamples >= 64 && supports(vk::SampleCountFlagBits::e64))
		{
			return vk::SampleCountFlagBits::e64;
		}
		if (requestedSamples >= 32 && supports(vk::SampleCountFlagBits::e32))
		{
			return vk::SampleCountFlagBits::e32;
		}
		if (requestedSamples >= 16 && supports(vk::SampleCountFlagBits::e16))
		{
			return vk::SampleCountFlagBits::e16;
		}
		if (requestedSamples >= 8 && supports(vk::SampleCountFlagBits::e8))
		{
			return vk::SampleCountFlagBits::e8;
		}
		if (requestedSamples >= 4 && supports(vk::SampleCountFlagBits::e4))
		{
			return vk::SampleCountFlagBits::e4;
		}
		if (requestedSamples >= 2 && supports(vk::SampleCountFlagBits::e2))
		{
			return vk::SampleCountFlagBits::e2;
		}
		return vk::SampleCountFlagBits::e1;
	}

	bool upload_rgba_to_images(
		VmaAllocator allocator,
		vk::CommandBuffer commandBuffer,
		vk::Queue queue,
		const std::vector<StorageImage*>& images,
		const unsigned char* rgba,
		std::size_t byteCount,
		std::string_view label)
	{
		// External backdrops are uploaded to every image that may be sampled this frame
		Logger* logger = Logger::fetch_logger();
		if (images.empty() || images.front() == nullptr || rgba == nullptr)
		{
			return false;
		}

		const vk::Extent2D extent = images.front()->extent;
		const std::size_t expectedBytes =
			static_cast<std::size_t>(extent.width) * static_cast<std::size_t>(extent.height) * 4u;
		if (expectedBytes == 0u || byteCount != expectedBytes)
		{
			logger->vulkan("Skipped RGBA upload for " + std::string(label) + ": pixel buffer size does not match image extent.");
			return false;
		}

		for (const StorageImage* image : images)
		{
			if (!image ||
				image->extent.width != extent.width ||
				image->extent.height != extent.height)
			{
				return false;
			}
		}

		vk::BufferCreateInfo bufferInfo = {};
		bufferInfo.size = static_cast<vk::DeviceSize>(expectedBytes);
		bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;

		VmaAllocationCreateInfo allocationInfo = {};
		allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
			VMA_ALLOCATION_CREATE_MAPPED_BIT;
		allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;

		VkBuffer stagingBuffer = VK_NULL_HANDLE;
		VmaAllocation stagingAllocation = nullptr;
		VmaAllocationInfo stagingInfo = {};
		VkBufferCreateInfo rawBufferInfo = bufferInfo;
		if (vmaCreateBuffer(allocator, &rawBufferInfo, &allocationInfo,
			&stagingBuffer, &stagingAllocation, &stagingInfo) != VK_SUCCESS)
		{
			logger->vulkan("Failed to create RGBA staging buffer for " + std::string(label) + ".");
			return false;
		}

		std::memcpy(stagingInfo.pMappedData, rgba, expectedBytes);

		vk::Result result = commandBuffer.reset();
		if (result != vk::Result::eSuccess)
		{
			logger->vulkan("Failed to reset RGBA upload command buffer.");
			vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
			return false;
		}

		vk::CommandBufferBeginInfo beginInfo = {};
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		result = commandBuffer.begin(beginInfo);
		if (result != vk::Result::eSuccess)
		{
			logger->vulkan("Failed to begin RGBA upload command buffer.");
			vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
			return false;
		}

		for (StorageImage* image : images)
		{
			transition_image_layout(commandBuffer, image->image,
				vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferDstOptimal,
				vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
				vk::AccessFlagBits::eTransferWrite,
				vk::PipelineStageFlagBits::eComputeShader,
				vk::PipelineStageFlagBits::eTransfer,
				vk::ImageAspectFlagBits::eColor, 0, image->mipLevels);

			vk::BufferImageCopy region = {};
			region.bufferOffset = 0;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
			region.imageSubresource.mipLevel = 0;
			region.imageSubresource.baseArrayLayer = 0;
			region.imageSubresource.layerCount = 1;
			region.imageOffset = vk::Offset3D { 0, 0, 0 };
			region.imageExtent = vk::Extent3D { image->extent.width, image->extent.height, 1 };
			commandBuffer.copyBufferToImage(stagingBuffer, image->image,
				vk::ImageLayout::eTransferDstOptimal, 1, &region);

			transition_image_layout(commandBuffer, image->image,
				vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral,
				vk::AccessFlagBits::eTransferWrite,
				vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
				vk::PipelineStageFlagBits::eTransfer,
				vk::PipelineStageFlagBits::eComputeShader,
				vk::ImageAspectFlagBits::eColor, 0, image->mipLevels);
		}

		result = commandBuffer.end();
		if (result != vk::Result::eSuccess)
		{
			logger->vulkan("Failed to end RGBA upload command buffer.");
			vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
			return false;
		}

		vk::SubmitInfo submitInfo = {};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;
		result = queue.submit(1, &submitInfo, nullptr);
		if (result != vk::Result::eSuccess)
		{
			logger->vulkan("Failed to submit RGBA upload.");
			vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
			return false;
		}

		result = queue.waitIdle();
		vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
		if (result != vk::Result::eSuccess)
		{
			logger->vulkan("Failed to wait for RGBA upload.");
			return false;
		}

		return true;
	}

	vk::RenderPass make_hosted3d_render_pass(
		vk::Device logicalDevice,
		vk::SampleCountFlagBits sampleCount,
		std::deque<std::function<void(vk::Device)>>& deletionQueue)
	{
		const bool usesResolveAttachment = sampleCount != vk::SampleCountFlagBits::e1;

		vk::AttachmentDescription colorAttachment = {};
		colorAttachment.format = vk::Format::eR8G8B8A8Unorm;
		colorAttachment.samples = sampleCount;
		colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
		colorAttachment.storeOp = usesResolveAttachment
			? vk::AttachmentStoreOp::eDontCare
			: vk::AttachmentStoreOp::eStore;
		colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		colorAttachment.initialLayout = usesResolveAttachment
			? vk::ImageLayout::eUndefined
			: vk::ImageLayout::eColorAttachmentOptimal;
		colorAttachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::AttachmentDescription depthAttachment = {};
		depthAttachment.format = vk::Format::eD32Sfloat;
		depthAttachment.samples = sampleCount;
		depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
		depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
		depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
		depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		vk::AttachmentDescription resolveAttachment = {};
		resolveAttachment.format = vk::Format::eR8G8B8A8Unorm;
		resolveAttachment.samples = vk::SampleCountFlagBits::e1;
		resolveAttachment.loadOp = vk::AttachmentLoadOp::eDontCare;
		resolveAttachment.storeOp = vk::AttachmentStoreOp::eStore;
		resolveAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		resolveAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		resolveAttachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
		resolveAttachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

		std::array<vk::AttachmentDescription, 3> attachments = {
			colorAttachment,
			depthAttachment,
			resolveAttachment
		};

		vk::AttachmentReference colorReference = {};
		colorReference.attachment = 0;
		colorReference.layout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::AttachmentReference depthReference = {};
		depthReference.attachment = 1;
		depthReference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		vk::AttachmentReference resolveReference = {};
		resolveReference.attachment = 2;
		resolveReference.layout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::SubpassDescription subpass = {};
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;
		subpass.pDepthStencilAttachment = &depthReference;
		subpass.pResolveAttachments = usesResolveAttachment ? &resolveReference : nullptr;

		std::array<vk::SubpassDependency, 2> dependencies = {};
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask =
			vk::PipelineStageFlagBits::eColorAttachmentOutput |
			vk::PipelineStageFlagBits::eEarlyFragmentTests;
		dependencies[0].dstStageMask =
			vk::PipelineStageFlagBits::eColorAttachmentOutput |
			vk::PipelineStageFlagBits::eEarlyFragmentTests;
		dependencies[0].srcAccessMask = vk::AccessFlagBits::eNone;
		dependencies[0].dstAccessMask =
			vk::AccessFlagBits::eColorAttachmentWrite |
			vk::AccessFlagBits::eDepthStencilAttachmentWrite;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask =
			vk::PipelineStageFlagBits::eColorAttachmentOutput |
			vk::PipelineStageFlagBits::eLateFragmentTests;
		dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eComputeShader;
		dependencies[1].srcAccessMask =
			vk::AccessFlagBits::eColorAttachmentWrite |
			vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		dependencies[1].dstAccessMask =
			vk::AccessFlagBits::eShaderRead |
			vk::AccessFlagBits::eShaderWrite;

		vk::RenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.attachmentCount = usesResolveAttachment ? 3u : 2u;
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		auto result = logicalDevice.createRenderPass(renderPassInfo);
		if (result.result != vk::Result::eSuccess)
		{
			Logger::fetch_logger()->vulkan("Failed to create hosted 3D render pass.");
			return nullptr;
		}

		VkRenderPass renderPassHandle = result.value;
		deletionQueue.push_back([renderPassHandle](vk::Device device) {
			device.destroyRenderPass(renderPassHandle);
		});
		return result.value;
	}

	std::mutex& live_engine_mutex()
	{
		static std::mutex mutex;
		return mutex;
	}

	std::vector<Engine*>& live_engines()
	{
		static std::vector<Engine*> engines;
		return engines;
	}

	std::string& shared_locale_value()
	{
		static std::string locale = "en_us";
		return locale;
	}

	void register_live_engine(Engine* engine)
	{
		if (!engine)
		{
			return;
		}

		std::lock_guard<std::mutex> lock(live_engine_mutex());
		std::vector<Engine*>& engines = live_engines();
		if (std::find(engines.begin(), engines.end(), engine) == engines.end())
		{
			engines.push_back(engine);
		}
	}

	void unregister_live_engine(Engine* engine)
	{
		std::lock_guard<std::mutex> lock(live_engine_mutex());
		std::vector<Engine*>& engines = live_engines();
		engines.erase(std::remove(engines.begin(), engines.end(), engine), engines.end());
	}

	std::vector<Engine*> live_engine_snapshot()
	{
		std::lock_guard<std::mutex> lock(live_engine_mutex());
		return live_engines();
	}
}

struct Engine::Impl
{
	Impl(const EngineCreateInfo& createInfo);
	~Impl();

	void draw();
	int update_timing(double currentTimeSeconds);
	bool set_present_mode(RendererPresentMode mode);
	RendererPresentMode present_mode_preference() const;
	RendererPresentMode active_present_mode() const;
	std::vector<RendererPresentMode> available_present_modes() const;
	void set_target_frame_rate(uint32_t frameRate);
	uint32_t target_frame_rate() const;
	uint32_t recommended_ui_update_rate() const;
	void resize(uint32_t width, uint32_t height);
	Model3DHandle load_model_3d(const std::filesystem::path& path);
	Media2DHandle load_media_2d(const std::filesystem::path& path, const Media2DLoadOptions& options);
	Media2DUploadResult upload_media_2d(const PreparedMedia2D& prepared, uint32_t maxFrameUploads);
	void cancel_media_2d_upload(const PreparedMedia2D& prepared);
	void clear_pending_media_uploads();
	void poll_retired_media_uploads();
	AudioClipHandle load_audio_clip(const std::filesystem::path& path);
	AudioEngine& audio();
	const AudioEngine& audio() const;
	bool load_renderer2d_font(const std::filesystem::path& path);
	bool load_renderer2d_font(const std::filesystem::path& path, const Renderer2DFontAtlasLoadOptions& options);
	bool load_renderer2d_font_internal(
		const std::filesystem::path& path,
		const Renderer2DFontAtlasLoadOptions& options,
		bool explicitRequest);
	void ensure_renderer2d_scene_glyphs();
	bool load_localisation_directory(const std::filesystem::path& directory);
	bool load_localisation_directories(const std::vector<std::filesystem::path>& directories);
	bool set_locale(const std::string& locale);
	std::string locale() const;
	std::string resolve_text(const Text& text) const;
	RenderBackend render_backend() const;
	PresentationBackend presentation_backend() const;
	bool system_backdrop_available() const;
	bool ready() const;
	std::string vulkan_api_version() const;
	void refresh_localised_texts();
	Localisation& localisation();
	const Localisation& localisation() const;
	bool set_external_backdrop_rgba(uint32_t width, uint32_t height, const unsigned char* rgba, std::size_t byteCount);
	void clear_external_backdrop();
	bool recreate_surface();
	uint32_t render_width() const;
	uint32_t render_height() const;
	Renderer2DScene& renderer2d_scene();
	const Renderer2DScene& renderer2d_scene() const;
	Renderer2DFontAtlas& renderer2d_font_atlas();
	const Renderer2DFontAtlas& renderer2d_font_atlas() const;

private:
	Logger* logger { Logger::fetch_logger() };

	void make_descriptor_sets();
	void make_pipeline_layouts();
	void make_pipelines();
	bool initialise_composition_presenter();
	bool rebuild_composition_presenter();
	bool ensure_composition_readback(vk::Extent2D extent);
	void destroy_composition_readback();
    bool upload_software_composition(std::uint32_t bufferIndex, glm::uvec4 damage);
	std::vector<SystemBackdropRegion> collect_system_backdrop_regions(
		double renderTimeSeconds);
	void apply_system_backdrop_regions(
		const std::vector<SystemBackdropRegion>& regions);
	void publish_completed_composition_frames();
	void collect_gpu_render_timings(Frame& frame);
	RendererCpuTimingScope measure_cpu_phase(RendererCpuPhase phase)
	{
		return RendererCpuTimingScope(renderDiagnosticsEnabled ?
			&diagnosticCpuTimings[static_cast<std::size_t>(phase)] : nullptr);
	}

	uint32_t framebufferWidth = 0;
	uint32_t framebufferHeight = 0;
	uint32_t maxRenderPixels = 0;
	uint32_t requestedMsaaSamples = 4;
	RenderBackend renderBackend = RenderBackend::eVulkan;
	PresentationBackend requestedPresentationBackend = PresentationBackend::eNative;
	PresentationBackend activePresentationBackend = PresentationBackend::eNative;
	void* nativeWindowHandle = nullptr;
	vk::Extent2D renderExtent = {};
	vk::Extent2D modelRenderExtent = {};

	std::deque<std::function<void(vk::Instance)>> instanceDeletionQueue;
	std::deque<std::function<void(vk::Device)>> deviceDeletionQueue;
	std::deque<std::function<void(VmaAllocator)>> vmaDeletionQueue;

	vk::Instance instance;
	vk::detail::DispatchLoaderDynamic dldi;
	vk::DebugUtilsMessengerEXT debugMessenger = nullptr;
	vk::PhysicalDevice physicalDevice;
	vk::Device logicalDevice;
	VmaAllocator allocator = nullptr;
	vk::Queue graphicsQueue;
	uint32_t graphicsQueueFamilyIndex = UINT32_MAX;
	vk::SurfaceKHR surface;
	void* surfaceUserData = nullptr;
	int (*createSurface)(void* instance, void* userData, void* surfaceOut) = nullptr;
	Swapchain swapchain;
	bool rendererReady = false;
	bool transparentFramebuffer = false;
	CompositionPresenter compositionPresenter;
	bool compositionGpuInterop = false;
	vk::Buffer compositionReadbackBuffer {};
	VmaAllocation compositionReadbackAllocation = nullptr;
	void* compositionReadbackMapped = nullptr;
	vk::Extent2D compositionReadbackExtent {};
	std::vector<SystemBackdropRegion> appliedBackdropRegions;

	std::unordered_map<DescriptorScope, vk::DescriptorSetLayout> descriptorSetLayouts;
	std::unordered_map<PipelineType, vk::PipelineLayout> pipelineLayouts;
	std::unordered_map<DescriptorScope, vk::DescriptorPool> descriptorPools;
	std::vector<std::unordered_map<DescriptorScope, vk::DescriptorSet>> descriptorSets;
	std::vector<Frame> frames;
	std::unordered_map<PipelineType, vk::Pipeline> pipelines;
	std::unique_ptr<vibrance::vulkan::PipelineCache> pipelineCache;
	vk::RenderPass hosted3DRenderPass = nullptr;
	vk::SampleCountFlagBits hosted3DSamples = vk::SampleCountFlagBits::e1;
	Renderer2DScene renderer2DScene;
	Renderer2DFontAtlas renderer2DFontAtlas;
	std::filesystem::path renderer2DFontPath {};
	Renderer2DFontAtlasLoadOptions renderer2DFontOptions {};
	std::unordered_set<uint32_t> attemptedRenderer2DCodepoints;
	uint64_t lastRenderer2DGlyphScanGeneration = UINT64_MAX;
	uint64_t lastRenderer2DTextFingerprint = 0u;
	bool hasRenderer2DTextFingerprint = false;

	vk::CommandPool commandPool;
	vk::CommandBuffer mainCommandBuffer;

	double lastTime = 0.0;
	double currentTime = 0.0;
	int numFrames = 0;
	float frameTime = 0.0f;
	uint32_t frameIndex = 0;
	uint32_t targetFrameRate = 0;
	std::chrono::steady_clock::time_point nextFrameDeadline {};
	std::vector<bool> compositionFramePending;
	std::vector<bool> compositionFrameUploaded;
	bool compositionRecoveryPending = false;
	uint32_t consecutiveCompositionRecoveries = 0u;
	std::chrono::steady_clock::time_point nextCompositionRecoveryDeadline {};
	std::chrono::steady_clock::time_point compositionAcquireWaitSince {};
	std::vector<uint32_t> compositionFrameBufferIndices;
	RetainedDamageTracker compositionBufferDamage;
	uint32_t nextCompositionBufferIndex = 0u;
	uint32_t compositionFrameRate = 60u;
	std::chrono::steady_clock::time_point nextCompositionSubmitDeadline {};
	std::chrono::steady_clock::time_point nextCompositionPollDeadline {};
	std::chrono::steady_clock::time_point nextNativeSubmitDeadline {};
	bool nativeTransparencyPrimed = false;
	bool hasSubmittedFrame = false;
	uint64_t lastSubmittedFrameGeneration = 0u;
	double lastSubmittedRenderTimeSeconds = -1.0;
	bool renderDiagnosticsEnabled = false;
	std::chrono::steady_clock::time_point renderDiagnosticsStart {};
	uint64_t diagnosticDrawCalls = 0u;
	uint64_t diagnosticSubmissions = 0u;
    uint64_t diagnosticReadbackBytes = 0u;
	uint64_t diagnosticGenerationChanges = 0u;
	uint64_t diagnosticContinuousRequests = 0u;
	uint64_t diagnosticRetainedSyncRequests = 0u;
	uint64_t diagnosticLastSceneGeneration = 0u;
	bool gpuTimestampDiagnosticsEnabled = false;
	bool gpuTimestampDiagnosticsAvailable = false;
	uint64_t gpuTimestampSubmissionIndex = 0u;
	uint64_t diagnosticGpuSamples = 0u;
	double diagnosticGpuTotalMilliseconds = 0.0;
	double diagnosticGpuRender2DMilliseconds = 0.0;
	double diagnosticGpuCompositeMilliseconds = 0.0;
	double diagnosticGpuOutputMilliseconds = 0.0;
	double diagnosticGpuMaxMilliseconds = 0.0;
	uint64_t diagnosticCacheRebuilds = 0u;
	uint64_t diagnosticCachedBatchReplays = 0u;
	uint64_t diagnosticDynamicBatchReplays = 0u;
	uint64_t diagnosticTextGlyphs = 0u;
	uint64_t diagnosticTextDispatches = 0u;
	uint64_t diagnosticTextBatchDispatches = 0u;
	uint64_t diagnosticLayeredBarriers = 0u;
	std::array<RendererCpuWallTiming,
		static_cast<std::size_t>(RendererCpuPhase::eCount)> diagnosticCpuTimings {};

	std::unordered_map<uint32_t, Model3DAsset> modelAssets;
	std::unordered_map<std::string, uint32_t> modelIdsByPath;
	uint32_t nextModelId = 1;
	std::unordered_map<uint32_t, Media2DAsset> mediaAssets;
	std::unordered_map<std::string, uint32_t> mediaIdsByPath;
    std::unordered_map<std::string, PreparedMedia2DAsyncUpload> pendingMediaUploads;
    PreparedMediaUploadBudget preparedMediaBudget;
    bool hasRetiredPreparedMediaUploads = false;
	uint32_t nextMediaId = 1;
	AudioEngine audioEngine;
	Localisation localisation_;
	bool externalBackdropAvailable = false;
};

Engine::Engine(const EngineCreateInfo& createInfo) : impl(std::make_unique<Impl>(createInfo))
{
    Logger::fetch_logger()->log(ready() ? LogLevel::eInfo : LogLevel::eError,
        ready() ? "Engine renderer ready: vulkan." : "Engine renderer initialisation failed: vulkan.");
	register_live_engine(this);
}

Engine::~Engine()
{
    Logger::fetch_logger()->info("Engine instance shutting down: vulkan.");
	unregister_live_engine(this);
}

void Engine::begin_externally_paced_frame(uint32_t frameRate)
{
	externallyPacedFrameRate = std::max(frameRate, 1u);
	externallyPacedFrameActive = true;
}

void Engine::end_externally_paced_frame()
{
	externallyPacedFrameActive = false;
}

void Engine::draw()
{
	impl->draw();
}

int Engine::update_timing(double currentTimeSeconds)
{
	return impl->update_timing(currentTimeSeconds);
}

bool Engine::set_present_mode(RendererPresentMode mode)
{
    const LogOperation diagnosticOperation("vulkan: Engine::set_present_mode");
	return impl->set_present_mode(mode);
}

RendererPresentMode Engine::present_mode_preference() const
{
	return impl->present_mode_preference();
}

RendererPresentMode Engine::active_present_mode() const
{
	return impl->active_present_mode();
}

std::vector<RendererPresentMode> Engine::available_present_modes() const
{
	return impl->available_present_modes();
}

void Engine::set_target_frame_rate(uint32_t frameRate)
{
	impl->set_target_frame_rate(frameRate);
}

uint32_t Engine::target_frame_rate() const
{
	return impl->target_frame_rate();
}

uint32_t Engine::recommended_ui_update_rate() const
{
	return impl->recommended_ui_update_rate();
}

double Engine::idle_event_wait_seconds()
{
	rendererHostUsesIdleEventWait = true;
	const bool settled = rendererDrawSeen && allRendererDrawsSettled;
	rendererDrawSeen = false;
	allRendererDrawsSettled = true;
	// Passthrough overlays still sample the desktop pointer promptly. Native
	// input interrupts the GLFW wait immediately, and moving content continues
	// through the renderer's existing display and user frame-rate pacing.
	return settled ? 1.0 / 60.0 : 0.0;
}

void Engine::resize(uint32_t framebufferWidth, uint32_t framebufferHeight)
{
	impl->resize(framebufferWidth, framebufferHeight);
}

Model3DHandle Engine::load_model_3d(const std::filesystem::path& path)
{
    const LogOperation diagnosticOperation("vulkan: Engine::load_model_3d");
	return impl->load_model_3d(path);
}

Media2DHandle Engine::load_media_2d(const std::filesystem::path& path, const Media2DLoadOptions& options)
{
    const LogOperation diagnosticOperation("vulkan: Engine::load_media_2d");
	return impl->load_media_2d(path, options);
}

Media2DUploadResult Engine::upload_media_2d(const PreparedMedia2D& prepared, uint32_t maxFrameUploads)
{
    try { return impl->upload_media_2d(prepared, maxFrameUploads); }
    catch (...)
    {
        impl->cancel_media_2d_upload(prepared);
        return { {}, true };
    }
}

void Engine::cancel_media_2d_upload(const PreparedMedia2D& prepared)
{
    impl->cancel_media_2d_upload(prepared);
}

AudioClipHandle Engine::load_audio_clip(const std::filesystem::path& path)
{
	return impl->load_audio_clip(path);
}

AudioEngine& Engine::audio()
{
	return impl->audio();
}

const AudioEngine& Engine::audio() const
{
	return impl->audio();
}

bool Engine::load_renderer2d_font(const std::filesystem::path& path)
{
    const LogOperation diagnosticOperation("vulkan: Engine::load_renderer2d_font");
	return impl->load_renderer2d_font(path);
}

bool Engine::load_renderer2d_font(const std::filesystem::path& path, const Renderer2DFontAtlasLoadOptions& options)
{
    const LogOperation diagnosticOperation("vulkan: Engine::load_renderer2d_font");
	return impl->load_renderer2d_font(path, options);
}

bool Engine::load_localisation_directory(const std::filesystem::path& directory)
{
    const LogOperation diagnosticOperation("vulkan: Engine::load_localisation_directory");
	const bool loaded = impl->load_localisation_directory(directory);
	if (loaded)
	{
		impl->set_locale(shared_locale());
	}
	return loaded;
}

bool Engine::load_localisation_directories(
	const std::vector<std::filesystem::path>& directories)
{
    const LogOperation diagnosticOperation("vulkan: Engine::load_localisation_directories");
	const bool loaded = impl->load_localisation_directories(directories);
	if (loaded)
	{
		impl->set_locale(shared_locale());
	}

	return loaded;
}

bool Engine::set_locale(const std::string& locale)
{
    const LogOperation diagnosticOperation("vulkan: Engine::set_locale");
	return impl->set_locale(locale);
}

bool Engine::set_shared_locale(const std::string& locale)
{
    const LogOperation diagnosticOperation("vulkan: Engine::set_shared_locale");
	{
		std::lock_guard<std::mutex> lock(live_engine_mutex());
		shared_locale_value() = locale;
	}

	bool updatedAny = false;
	for (Engine* engine : live_engine_snapshot())
	{
		if (engine)
		{
			updatedAny = engine->set_locale(locale) || updatedAny;
		}
	}
	return updatedAny;
}

std::string Engine::shared_locale()
{
	std::lock_guard<std::mutex> lock(live_engine_mutex());
	return shared_locale_value();
}

std::string Engine::locale() const
{
	return impl->locale();
}

std::string Engine::resolve_text(const Text& text) const
{
	return impl->resolve_text(text);
}

RenderBackend Engine::render_backend() const
{
	return impl->render_backend();
}

PresentationBackend Engine::presentation_backend() const
{
	return impl->presentation_backend();
}

bool Engine::system_backdrop_available() const
{
	return impl->system_backdrop_available();
}

bool Engine::ready() const
{
	return impl->ready();
}

std::string Engine::vulkan_api_version() const
{
	return impl->vulkan_api_version();
}

void Engine::refresh_localised_texts()
{
	impl->refresh_localised_texts();
}

Localisation& Engine::localisation()
{
	return impl->localisation();
}

const Localisation& Engine::localisation() const
{
	return impl->localisation();
}

bool Engine::set_external_backdrop_rgba(
	uint32_t width,
	uint32_t height,
	const unsigned char* rgba,
	std::size_t byteCount)
{
	return impl->set_external_backdrop_rgba(width, height, rgba, byteCount);
}

void Engine::clear_external_backdrop()
{
	impl->clear_external_backdrop();
}

uint32_t Engine::render_width() const
{
	return impl->render_width();
}

uint32_t Engine::render_height() const
{
	return impl->render_height();
}

Renderer2DScene& Engine::renderer2d_scene()
{
	return impl->renderer2d_scene();
}

const Renderer2DScene& Engine::renderer2d_scene() const
{
	return impl->renderer2d_scene();
}

Renderer2DFontAtlas& Engine::renderer2d_font_atlas()
{
	return impl->renderer2d_font_atlas();
}

const Renderer2DFontAtlas& Engine::renderer2d_font_atlas() const
{
	return impl->renderer2d_font_atlas();
}

Engine::Impl::Impl(const EngineCreateInfo& createInfo)
    : framebufferWidth(createInfo.framebufferWidth),
	framebufferHeight(createInfo.framebufferHeight),
	maxRenderPixels(createInfo.maxRenderPixels),
	requestedMsaaSamples(createInfo.msaaSamples),
	renderBackend(createInfo.renderBackend),
	requestedPresentationBackend(createInfo.presentationBackend),
	nativeWindowHandle(createInfo.nativeWindowHandle),
	renderExtent(choose_render_extent(createInfo.framebufferWidth, createInfo.framebufferHeight, createInfo.maxRenderPixels)),
	// Hosted 3D attachments grow to the render extent on demand.  Most UI-only
	// windows never submit a model, so full-size colour/depth targets are waste.
	modelRenderExtent(1u, 1u),
	transparentFramebuffer(createInfo.transparentFramebuffer),
	targetFrameRate(std::min(createInfo.targetFrameRate, 1000u)),
	audioEngine(createInfo.enableAudio)
{
    logger = Logger::fetch_logger();
    const auto startupBegan = std::chrono::steady_clock::now();
    const char* startupPhase = "renderer configuration";
    const auto startupStage = [&](const char* phase) {
        startupPhase = phase;
        logger->info(std::string("Renderer initialisation: ") + phase + ".");
    };
    try
    {
	const char* renderDiagnostics = std::getenv(
		"VIBRANCE_RENDER_DIAGNOSTICS");
	renderDiagnosticsEnabled = renderDiagnostics &&
		std::strcmp(renderDiagnostics, "0") != 0;
#if defined(_WIN32)
	gpuTimestampDiagnosticsEnabled = renderDiagnostics &&
		std::strcmp(renderDiagnostics, "1") == 0;
	compositionFrameRate = monitor_refresh_rate(nativeWindowHandle);
#endif
    if (renderBackend != RenderBackend::eVulkan)
    {
        logger->error("The requested render backend is not implemented. vibranceUI currently renders with Vulkan.");
        return;
    }
    logger->vulkan("Initialising renderer.");

    startupStage("Vulkan instance");
    instance = make_instance(
        createInfo.applicationName,
        createInfo.instanceExtensionCount,
        createInfo.instanceExtensions,
        instanceDeletionQueue
    );
    if (!instance)
    {
        logger->error("Renderer initialisation could not create a Vulkan instance.");
        return;
    }
	dldi = vk::detail::DispatchLoaderDynamic(instance, vkGetInstanceProcAddr);
	if (logger->is_vulkan_validation_enabled()) 
    {
		debugMessenger = logger->make_debug_messenger(instance, dldi, instanceDeletionQueue);
	}

	if (!createInfo.createSurface)
	{
		logger->vulkan(LogLevel::eError, "No surface creation callback was supplied.");
		return;
	}
	createSurface = createInfo.createSurface;
	surfaceUserData = createInfo.surfaceUserData;

	VkSurfaceKHR raw_surface = VK_NULL_HANDLE;
	int surfaceResult = createSurface(instance, surfaceUserData, &raw_surface);
	if (surfaceResult != 0)
	{
		logger->vulkan(LogLevel::eError, "Failed to create Vulkan surface.");
		return;
	}
	surface = raw_surface;
	instanceDeletionQueue.push_back([this](vk::Instance instance) {
		if (surface)
		{
			instance.destroySurfaceKHR(surface);
		}
	});

	bool compositionRequested =
		requestedPresentationBackend == PresentationBackend::eWindowsCompositionD3D11;
#if !defined(_WIN32)
	compositionRequested = false;
#endif
	if (compositionRequested && !nativeWindowHandle)
	{
		logger->warning(
			"Windows Composition presentation requires a native HWND; using native Vulkan presentation.");
		compositionRequested = false;
	}
	const char* forceCompositionHostUpload = std::getenv(
		"VIBRANCE_FORCE_COMPOSITION_HOST_UPLOAD");
	const bool forceHostUpload = compositionRequested &&
		forceCompositionHostUpload &&
		std::strcmp(forceCompositionHostUpload, "0") != 0;
	if (forceHostUpload)
	{
		logger->info(
			"Windows Composition host-upload fallback was forced for diagnostics.");
	}
	// Prefer zero-copy Vulkan/D3D interop when the native ICD exposes the full
	// external-memory contract. Native ARM64 drivers may omit that optional
	// contract; those devices keep DirectComposition via the host-upload path.
	const bool attemptCompositionGpuInterop =
		compositionRequested && !forceHostUpload;
	startupStage("graphics device selection");
	physicalDevice = choose_physical_device(
		instance,
		attemptCompositionGpuInterop,
		compositionRequested ? nativeWindowHandle : nullptr);
	compositionGpuInterop = attemptCompositionGpuInterop &&
		static_cast<bool>(physicalDevice);
	if (!physicalDevice && attemptCompositionGpuInterop)
	{
		logger->warning(
			"No Vulkan GPU supports D3D11 external-memory and keyed-mutex interop; using the host-upload Windows Composition fallback.");
		physicalDevice = choose_physical_device(
			instance, false, nativeWindowHandle);
	}
	if (!physicalDevice)
	{
		logger->vulkan(LogLevel::eError, "No suitable physical device was found.");
		return;
	}
	hosted3DSamples = choose_hosted3d_sample_count(physicalDevice, requestedMsaaSamples);
	logger->vulkan(std::string("Hosted 3D MSAA sample count: ") + sample_count_name(hosted3DSamples) + ".");

	startupStage("graphics device creation");
	logicalDevice = create_logical_device(
		physicalDevice,
		surface,
		deviceDeletionQueue,
		compositionGpuInterop);
	if (!logicalDevice)
	{
		logger->vulkan("Failed to create a logical device.");
		return;
	}
	graphicsQueueFamilyIndex = find_queue_family_index(physicalDevice, surface, vk::QueueFlagBits::eGraphics);
	graphicsQueue = logicalDevice.getQueue(graphicsQueueFamilyIndex, 0);

	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.device = logicalDevice;
	allocatorInfo.instance = instance;
	allocatorInfo.physicalDevice = physicalDevice;
	allocatorInfo.vulkanApiVersion = vk::ApiVersion13;
	// Each UI window owns an allocator. Small blocks avoid reserving a large
	// mostly empty heap for every window; larger resources still fit normally.
	allocatorInfo.preferredLargeHeapBlockSize = 4ull * 1024ull * 1024ull;
	const VkResult allocatorResult = vmaCreateAllocator(&allocatorInfo, &allocator);
	if (allocatorResult != VK_SUCCESS || !allocator)
	{
		logger->error("Renderer initialisation could not create its graphics allocator: " +
			vk::to_string(static_cast<vk::Result>(allocatorResult)));
		return;
	}

	startupStage("presentation surface");
	swapchain.presentModePreference = createInfo.presentMode;
	swapchain.build(logicalDevice, physicalDevice, surface, framebufferWidth, framebufferHeight, transparentFramebuffer);
	if (!swapchain.chain || swapchain.images.empty())
	{
		logger->vulkan("Swapchain creation produced no drawable images.");
		return;
	}
	if (compositionRequested && initialise_composition_presenter())
	{
		activePresentationBackend = PresentationBackend::eWindowsCompositionD3D11;
		// Transparent Composition windows opt out of the HWND redirection
		// surface. DirectComposition is the only visible content, whether its
		// source arrives through shared GPU memory or the host-upload fallback.
		nativeTransparencyPrimed = transparentFramebuffer;
	}
	else
	{
		activePresentationBackend = PresentationBackend::eNative;
#if defined(_WIN32)
		restore_native_window_redirection(
			nativeWindowHandle,
			transparentFramebuffer);
#endif
	}
	renderExtent = choose_render_extent(swapchain.extent.width, swapchain.extent.height, maxRenderPixels);
	modelRenderExtent = hosted_3d_extent(renderExtent, renderer2DScene);
	if (compositionPresenter.available() &&
		!compositionPresenter.gpu_interop() &&
		!ensure_composition_readback(renderExtent))
	{
		logger->warning(
			"Windows Composition host-upload staging could not be allocated; using native Vulkan presentation.");
		compositionPresenter.shutdown(logicalDevice);
		activePresentationBackend = PresentationBackend::eNative;
#if defined(_WIN32)
		restore_native_window_redirection(
			nativeWindowHandle,
			transparentFramebuffer);
#endif
	}

	make_descriptor_sets();

	make_pipeline_layouts();
	hosted3DRenderPass = make_hosted3d_render_pass(logicalDevice, hosted3DSamples, deviceDeletionQueue);
	if (!hosted3DRenderPass)
	{
		logger->vulkan("Failed to create hosted 3D render pass.");
		return;
	}
	
	startupStage("graphics pipelines");
	make_pipelines();
	const std::array requiredPipelines = {
		PipelineType::eClear,
		PipelineType::eRasteriseSmall,
		PipelineType::eShape2D,
		PipelineType::eShadow2D,
		PipelineType::eBlur2D,
		PipelineType::eTextMSDF,
		PipelineType::eMedia2D,
		PipelineType::eCompositeHosted3D,
		PipelineType::eComposite2D,
		PipelineType::eModel3D
	};
	const bool hasRequiredPipelines = std::all_of(requiredPipelines.begin(), requiredPipelines.end(),
		[this](PipelineType pipelineType) {
			const auto it = pipelines.find(pipelineType);
			return it != pipelines.end() && static_cast<bool>(it->second);
		});
	if (!hasRequiredPipelines)
	{
		logger->vulkan("Failed to create all required pipelines.");
		return;
	}

	commandPool = make_command_pool(logicalDevice, graphicsQueueFamilyIndex, deviceDeletionQueue);

	mainCommandBuffer = allocate_command_buffer(logicalDevice, commandPool);

	// DirectComposition deliberately keeps one Vulkan submission in flight (see
	// draw()).  Mirroring every native swapchain image here only duplicates the
	// sizeable offscreen render graph without adding any parallelism.  Native
	// presentation remains fully pipelined against all of its swapchain images.
	const uint32_t frameCount = compositionPresenter.available() ?
		1u : static_cast<uint32_t>(swapchain.images.size());
	std::vector<vk::DescriptorType> descriptorTypes = { vk::DescriptorType::eStorageImage, vk::DescriptorType::eStorageImage };
	descriptorPools[DescriptorScope::eFrame] = make_descriptor_pool(logicalDevice, frameCount, descriptorTypes.size(), descriptorTypes.data(), deviceDeletionQueue);
	descriptorPools[DescriptorScope::eModelFrame] = make_descriptor_pool(logicalDevice, frameCount, descriptorTypes.size(), descriptorTypes.data(), deviceDeletionQueue);
	descriptorPools[DescriptorScope::eUICache] = make_descriptor_pool(logicalDevice, frameCount, descriptorTypes.size(), descriptorTypes.data(), deviceDeletionQueue);
	descriptorPools[DescriptorScope::eForegroundBlur] = make_descriptor_pool(logicalDevice, frameCount, descriptorTypes.size(), descriptorTypes.data(), deviceDeletionQueue);
	descriptorTypes[0] = vk::DescriptorType::eStorageBuffer;
	descriptorPools[DescriptorScope::eDrawCall] = make_descriptor_pool(logicalDevice, frameCount, 1, descriptorTypes.data(), deviceDeletionQueue);
	descriptorPools[DescriptorScope::eTextBatch] = make_descriptor_pool(logicalDevice, frameCount, 1, descriptorTypes.data(), deviceDeletionQueue);
	descriptorTypes = {
		vk::DescriptorType::eCombinedImageSampler,
		vk::DescriptorType::eCombinedImageSampler,
		vk::DescriptorType::eCombinedImageSampler,
		vk::DescriptorType::eCombinedImageSampler,
		vk::DescriptorType::eCombinedImageSampler
	};
	descriptorPools[DescriptorScope::eModel3DTexture] = make_descriptor_pool(logicalDevice, kMaxModel3DMaterialSets, descriptorTypes.size(), descriptorTypes.data(), deviceDeletionQueue);
	descriptorTypes = { vk::DescriptorType::eCombinedImageSampler };
	descriptorPools[DescriptorScope::eMediaTexture] = make_descriptor_pool(logicalDevice, kMaxMedia2DTextures, descriptorTypes.size(), descriptorTypes.data(), deviceDeletionQueue);
	descriptorTypes = {
		vk::DescriptorType::eStorageImage,
		vk::DescriptorType::eStorageImage,
		vk::DescriptorType::eStorageImage,
		vk::DescriptorType::eStorageImage,
		vk::DescriptorType::eStorageImage,
		vk::DescriptorType::eStorageImage,
		vk::DescriptorType::eStorageImage,
		vk::DescriptorType::eStorageImage,
		vk::DescriptorType::eStorageImage
	};
	descriptorPools[DescriptorScope::ePost] = make_descriptor_pool(logicalDevice, frameCount, descriptorTypes.size(), descriptorTypes.data(), deviceDeletionQueue);
	descriptorPools[DescriptorScope::eUICachePost] = make_descriptor_pool(logicalDevice, frameCount, descriptorTypes.size(), descriptorTypes.data(), deviceDeletionQueue);

	startupStage("font atlas and frame resources");
	if (!createInfo.defaultRenderer2DFontPath.empty())
	{
		if (renderer2DFontAtlas.load_from_file(
				createInfo.defaultRenderer2DFontPath,
				createInfo.defaultRenderer2DFontOptions,
				allocator,
				mainCommandBuffer,
				graphicsQueue,
				logicalDevice,
				vmaDeletionQueue,
				deviceDeletionQueue))
		{
			renderer2DFontPath = createInfo.defaultRenderer2DFontPath;
			renderer2DFontOptions = createInfo.defaultRenderer2DFontOptions;
		}
		else
		{
			logger->vulkan("Renderer2D font atlas is using its fallback texture.");
		}
	}

	descriptorSets.resize(frameCount);
	frames.reserve(frameCount);
	uint32_t diagnosticTimestampValidBits = 0u;
	double diagnosticTimestampPeriodNanoseconds = 0.0;
	if (gpuTimestampDiagnosticsEnabled)
	{
		const auto queueFamilies = physicalDevice.getQueueFamilyProperties();
		if (graphicsQueueFamilyIndex < queueFamilies.size())
		{
			diagnosticTimestampValidBits =
				queueFamilies[graphicsQueueFamilyIndex].timestampValidBits;
			diagnosticTimestampPeriodNanoseconds =
				physicalDevice.getProperties().limits.timestampPeriod;
		}
	}
	for (uint32_t i = 0; i < frameCount; ++i) 
	{
		vk::CommandBuffer commandBuffer = allocate_command_buffer(logicalDevice, commandPool);
		constexpr int scopeCount = 8;
		DescriptorScope scopes[scopeCount] = {
			DescriptorScope::eFrame,
			DescriptorScope::eModelFrame,
			DescriptorScope::eDrawCall,
			DescriptorScope::ePost,
			DescriptorScope::eUICache,
			DescriptorScope::eUICachePost,
			DescriptorScope::eForegroundBlur,
			DescriptorScope::eTextBatch
		};
		for (int j = 0; j < scopeCount; ++j) 
		{
			DescriptorScope scope = scopes[j];
			descriptorSets[i][scope] = allocate_descriptor_set(logicalDevice,
				descriptorPools[scope], descriptorSetLayouts[scope]);
		}

		frames.emplace_back(swapchain, renderExtent, modelRenderExtent, logicalDevice, pipelines, hosted3DRenderPass,
			hosted3DSamples,
			commandBuffer, graphicsQueue, deviceDeletionQueue,
			descriptorSets[i], pipelineLayouts,
			allocator, &modelAssets, &mediaAssets, renderer2DScene, renderer2DFontAtlas.image());
		if (gpuTimestampDiagnosticsEnabled)
		{
			gpuTimestampDiagnosticsAvailable |= frames.back().enable_gpu_timing(
				diagnosticTimestampValidBits,
				diagnosticTimestampPeriodNanoseconds,
				deviceDeletionQueue);
		}
	}
	compositionFramePending.assign(frameCount, false);
	compositionFrameUploaded.assign(frameCount, false);
	compositionFrameBufferIndices.assign(frameCount, 0u);

	currentTime = 0.0;
	lastTime = 0.0;
	numFrames = 0;
	rendererReady = true;
    logger->info("Renderer initialisation completed in " + std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startupBegan).count()) + " ms.");
    }
    catch (const std::exception& error)
    {
        rendererReady = false;
        logger->error(std::string("Renderer initialisation failed during ") + startupPhase +
            ": " + error.what());
    }
    catch (...)
    {
        rendererReady = false;
        logger->error(std::string("Renderer initialisation failed during ") + startupPhase + ".");
    }
}

void Engine::Impl::make_descriptor_sets() 
{
	DescriptorSetLayoutBuilder builder(logicalDevice);

	builder.add_entry(vk::ShaderStageFlagBits::eCompute, vk::DescriptorType::eStorageImage);
	builder.add_entry(vk::ShaderStageFlagBits::eCompute, vk::DescriptorType::eStorageImage);
	descriptorSetLayouts[DescriptorScope::eFrame] = builder.build(deviceDeletionQueue);
	builder.add_entry(vk::ShaderStageFlagBits::eCompute, vk::DescriptorType::eStorageImage);
	builder.add_entry(vk::ShaderStageFlagBits::eCompute, vk::DescriptorType::eStorageImage);
	descriptorSetLayouts[DescriptorScope::eModelFrame] = builder.build(deviceDeletionQueue);
	builder.add_entry(vk::ShaderStageFlagBits::eCompute, vk::DescriptorType::eStorageBuffer);
	descriptorSetLayouts[DescriptorScope::eDrawCall] = builder.build(deviceDeletionQueue);
	builder.add_entry(vk::ShaderStageFlagBits::eCompute, vk::DescriptorType::eStorageBuffer);
	descriptorSetLayouts[DescriptorScope::eTextBatch] = builder.build(deviceDeletionQueue);
	builder.add_entry(vk::ShaderStageFlagBits::eFragment, vk::DescriptorType::eCombinedImageSampler);
	builder.add_entry(vk::ShaderStageFlagBits::eFragment, vk::DescriptorType::eCombinedImageSampler);
	builder.add_entry(vk::ShaderStageFlagBits::eFragment, vk::DescriptorType::eCombinedImageSampler);
	builder.add_entry(vk::ShaderStageFlagBits::eFragment, vk::DescriptorType::eCombinedImageSampler);
	builder.add_entry(vk::ShaderStageFlagBits::eFragment, vk::DescriptorType::eCombinedImageSampler);
	descriptorSetLayouts[DescriptorScope::eModel3DTexture] = builder.build(deviceDeletionQueue);
	builder.add_entry(vk::ShaderStageFlagBits::eCompute, vk::DescriptorType::eCombinedImageSampler);
	descriptorSetLayouts[DescriptorScope::eMediaTexture] = builder.build(deviceDeletionQueue);
	builder.add_entry(vk::ShaderStageFlagBits::eCompute, vk::DescriptorType::eStorageImage);
	builder.add_entry(vk::ShaderStageFlagBits::eCompute, vk::DescriptorType::eStorageImage);
	builder.add_entry(vk::ShaderStageFlagBits::eCompute, vk::DescriptorType::eStorageImage);
	builder.add_entry(vk::ShaderStageFlagBits::eCompute, vk::DescriptorType::eStorageImage);
	builder.add_entry(vk::ShaderStageFlagBits::eCompute, vk::DescriptorType::eStorageImage);
	builder.add_entry(vk::ShaderStageFlagBits::eCompute, vk::DescriptorType::eStorageImage);
	builder.add_entry(vk::ShaderStageFlagBits::eCompute, vk::DescriptorType::eStorageImage);
	builder.add_entry(vk::ShaderStageFlagBits::eCompute, vk::DescriptorType::eStorageImage);
	builder.add_entry(vk::ShaderStageFlagBits::eCompute, vk::DescriptorType::eStorageImage);
	descriptorSetLayouts[DescriptorScope::ePost] = builder.build(deviceDeletionQueue);
	descriptorSetLayouts[DescriptorScope::eUICache] = descriptorSetLayouts[DescriptorScope::eFrame];
	descriptorSetLayouts[DescriptorScope::eForegroundBlur] = descriptorSetLayouts[DescriptorScope::eFrame];
	descriptorSetLayouts[DescriptorScope::eUICachePost] = descriptorSetLayouts[DescriptorScope::ePost];
}

void Engine::Impl::make_pipeline_layouts() 
{
	PipelineLayoutBuilder builder(logicalDevice);

	builder.add(descriptorSetLayouts[DescriptorScope::eFrame]);
	builder.add_push_constants(vk::ShaderStageFlagBits::eCompute, sizeof(glm::uvec4));
	pipelineLayouts[PipelineType::eClear] = builder.build(deviceDeletionQueue);

	builder.add(descriptorSetLayouts[DescriptorScope::eFrame]);
	builder.add(descriptorSetLayouts[DescriptorScope::eDrawCall]);
	builder.add_push_constants(vk::ShaderStageFlagBits::eCompute, sizeof(RasterPushConstants));
	vk::PipelineLayout renderLayout = builder.build(deviceDeletionQueue);
	pipelineLayouts[PipelineType::eRasteriseSmall] = renderLayout;

	builder.add(descriptorSetLayouts[DescriptorScope::eFrame]);
	builder.add_push_constants(vk::ShaderStageFlagBits::eCompute, sizeof(Renderer2DPushConstants));
	vk::PipelineLayout renderer2DPrimitiveLayout = builder.build(deviceDeletionQueue);
	pipelineLayouts[PipelineType::eShape2D] = renderer2DPrimitiveLayout;
	pipelineLayouts[PipelineType::eShadow2D] = renderer2DPrimitiveLayout;

	builder.add(descriptorSetLayouts[DescriptorScope::eFrame]);
	builder.add(descriptorSetLayouts[DescriptorScope::ePost]);
	builder.add_push_constants(vk::ShaderStageFlagBits::eCompute, sizeof(Renderer2DPushConstants));
	pipelineLayouts[PipelineType::eTextMSDF] = builder.build(deviceDeletionQueue);
	builder.add(descriptorSetLayouts[DescriptorScope::eFrame]);
	builder.add(descriptorSetLayouts[DescriptorScope::ePost]);
	builder.add(descriptorSetLayouts[DescriptorScope::eTextBatch]);
	builder.add_push_constants(vk::ShaderStageFlagBits::eCompute, sizeof(glm::uvec4));
	pipelineLayouts[PipelineType::eTextMSDFBatch] = builder.build(deviceDeletionQueue);

	builder.add(descriptorSetLayouts[DescriptorScope::eFrame]);
	builder.add(descriptorSetLayouts[DescriptorScope::eMediaTexture]);
	builder.add(descriptorSetLayouts[DescriptorScope::ePost]);
	builder.add_push_constants(vk::ShaderStageFlagBits::eCompute, sizeof(Renderer2DPushConstants));
	pipelineLayouts[PipelineType::eMedia2D] = builder.build(deviceDeletionQueue);

	builder.add(descriptorSetLayouts[DescriptorScope::eFrame]);
	builder.add(descriptorSetLayouts[DescriptorScope::ePost]);
	builder.add_push_constants(vk::ShaderStageFlagBits::eCompute, sizeof(Renderer2DPushConstants));
	pipelineLayouts[PipelineType::eBlur2D] = builder.build(deviceDeletionQueue);

	builder.add(descriptorSetLayouts[DescriptorScope::eFrame]);
	builder.add(descriptorSetLayouts[DescriptorScope::ePost]);
	builder.add_push_constants(vk::ShaderStageFlagBits::eCompute, sizeof(glm::uvec4));
	vk::PipelineLayout compositeLayout = builder.build(deviceDeletionQueue);
	pipelineLayouts[PipelineType::eCompositeHosted3D] = compositeLayout;
	builder.add(descriptorSetLayouts[DescriptorScope::eFrame]);
	builder.add(descriptorSetLayouts[DescriptorScope::ePost]);
	builder.add_push_constants(vk::ShaderStageFlagBits::eCompute, sizeof(Renderer2DCompositeConstants));
	pipelineLayouts[PipelineType::eComposite2D] = builder.build(deviceDeletionQueue);

	builder.add(descriptorSetLayouts[DescriptorScope::eModel3DTexture]);
	builder.add_push_constants(
		vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
		sizeof(Model3DPushConstants));
	pipelineLayouts[PipelineType::eModel3D] = builder.build(deviceDeletionQueue);
}

void Engine::Impl::make_pipelines() 
{
	vk::PipelineCache cache {};
	try
	{
		pipelineCache = std::make_unique<vibrance::vulkan::PipelineCache>(
			logicalDevice, physicalDevice.getProperties(), embedded_shader_fingerprint());
		cache = pipelineCache->handle();
	}
	catch (...)
	{
		logger->vulkan("Vulkan pipeline cache unavailable; compiling normally.");
	}
	pipelines[PipelineType::eClear] = make_compute_pipeline(
		logicalDevice, "clear_screen", pipelineLayouts[PipelineType::eClear], deviceDeletionQueue, cache
	);
	pipelines[PipelineType::eRasteriseSmall] = make_compute_pipeline(
		logicalDevice, "rasterise_small", pipelineLayouts[PipelineType::eRasteriseSmall], deviceDeletionQueue, cache
	);
	pipelines[PipelineType::eShape2D] = make_compute_pipeline(
		logicalDevice, "shape_2d", pipelineLayouts[PipelineType::eShape2D], deviceDeletionQueue, cache
	);
	pipelines[PipelineType::eShadow2D] = make_compute_pipeline(
		logicalDevice, "shadow_2d", pipelineLayouts[PipelineType::eShadow2D], deviceDeletionQueue, cache
	);
	pipelines[PipelineType::eBlur2D] = make_compute_pipeline(
		logicalDevice, "blur_2d", pipelineLayouts[PipelineType::eBlur2D], deviceDeletionQueue, cache
	);
	pipelines[PipelineType::eTextMSDF] = make_compute_pipeline(
		logicalDevice, "text_msdf", pipelineLayouts[PipelineType::eTextMSDF], deviceDeletionQueue, cache
	);
	// Batching is optional. The ordinary text pipeline remains available if a
	// driver cannot create this variant or its bounded upload cannot be allocated.
	pipelines[PipelineType::eTextMSDFBatch] = make_compute_pipeline(
		logicalDevice, "text_msdf_batch", pipelineLayouts[PipelineType::eTextMSDFBatch], deviceDeletionQueue, cache
	);
	const uint32_t mediaVendorId = physicalDevice.getProperties().vendorID;
	const std::string_view mediaShaderName =
		vibrance::vulkan::media_pipeline_shader_name(mediaVendorId);
	pipelines[PipelineType::eMedia2D] = make_compute_pipeline(
		logicalDevice, mediaShaderName.data(), pipelineLayouts[PipelineType::eMedia2D], deviceDeletionQueue, cache
	);
	if (!pipelines[PipelineType::eMedia2D] && mediaShaderName != "media_2d")
	{
		pipelines[PipelineType::eMedia2D] = make_compute_pipeline(
			logicalDevice, vibrance::vulkan::media_pipeline_shader_name(mediaVendorId, false).data(),
			pipelineLayouts[PipelineType::eMedia2D], deviceDeletionQueue, cache);
	}
	pipelines[PipelineType::eCompositeHosted3D] = make_compute_pipeline(
		logicalDevice, "hosted_3d_composite", pipelineLayouts[PipelineType::eCompositeHosted3D], deviceDeletionQueue, cache
	);
	pipelines[PipelineType::eComposite2D] = make_compute_pipeline(
		logicalDevice, "composite_2d", pipelineLayouts[PipelineType::eComposite2D], deviceDeletionQueue, cache
	);
	pipelines[PipelineType::eModel3D] = make_graphics_pipeline(
		logicalDevice, "model_3d", pipelineLayouts[PipelineType::eModel3D], hosted3DRenderPass,
		hosted3DSamples, deviceDeletionQueue, cache
	);
	if (pipelineCache) pipelineCache->persist();
}

bool Engine::Impl::initialise_composition_presenter()
{
	const auto initialise = [this] {
		return compositionPresenter.initialise(nativeWindowHandle, physicalDevice,
			logicalDevice, swapchain.extent.width, swapchain.extent.height,
			kCompositionBufferCount, graphicsQueueFamilyIndex,
			transparentFramebuffer, compositionGpuInterop);
	};
	if (initialise()) return true;
	if (!compositionGpuInterop) return false;
	// Extension support alone does not establish a usable shared allocation on
	// older or hybrid drivers. Keep Composition and its visual quality through
	// the asynchronous host-upload transport if the import contract fails.
	Logger::fetch_logger()->warning(
		"Windows Composition GPU sharing failed; trying the host-upload transport on the selected adapter.");
	compositionGpuInterop = false;
	return initialise();
}

bool Engine::Impl::rebuild_composition_presenter()
{
	compositionPresenter.shutdown(logicalDevice);
	nativeTransparencyPrimed = false;
	nextCompositionSubmitDeadline = {};
	nextCompositionPollDeadline = {};
	nextNativeSubmitDeadline = {};
	nextCompositionBufferIndex = 0u;
	compositionAcquireWaitSince = {};
	hasSubmittedFrame = false;
	lastSubmittedFrameGeneration = 0u;
	compositionBufferDamage.clear();
	std::fill(compositionFramePending.begin(), compositionFramePending.end(), false);
	std::fill(compositionFrameUploaded.begin(), compositionFrameUploaded.end(), false);
	std::fill(
		compositionFrameBufferIndices.begin(),
		compositionFrameBufferIndices.end(),
		0u);
	appliedBackdropRegions.clear();
	activePresentationBackend = PresentationBackend::eNative;
#if defined(_WIN32)
	if (requestedPresentationBackend != PresentationBackend::eWindowsCompositionD3D11 ||
		!nativeWindowHandle || !logicalDevice || !physicalDevice ||
		!swapchain.chain || swapchain.images.empty())
	{
		return false;
	}
	if (initialise_composition_presenter())
	{
		activePresentationBackend = PresentationBackend::eWindowsCompositionD3D11;
		nativeTransparencyPrimed = transparentFramebuffer;
		if (!compositionPresenter.gpu_interop() &&
			!ensure_composition_readback(renderExtent))
		{
			logger->warning(
				"Windows Composition host-upload staging could not be rebuilt.");
			compositionPresenter.shutdown(logicalDevice);
			activePresentationBackend = PresentationBackend::eNative;
			return false;
		}
		if (compositionPresenter.gpu_interop())
		{
			destroy_composition_readback();
		}
		compositionBufferDamage.reset(
			compositionPresenter.buffer_count(),
			glm::uvec4(0u));
		return true;
	}
#endif
#if defined(_WIN32)
	restore_native_window_redirection(
		nativeWindowHandle,
		transparentFramebuffer);
#endif
	return false;
}

bool Engine::Impl::ensure_composition_readback(vk::Extent2D extent)
{
	if (extent.width == 0u || extent.height == 0u || !allocator)
	{
		return false;
	}
	if (compositionReadbackBuffer && compositionReadbackMapped &&
		compositionReadbackExtent.width == extent.width &&
		compositionReadbackExtent.height == extent.height)
	{
		return true;
	}

	const vk::DeviceSize byteCount =
		static_cast<vk::DeviceSize>(extent.width) *
		static_cast<vk::DeviceSize>(extent.height) * 4u;
	vk::BufferCreateInfo bufferInfo = {};
	bufferInfo.size = byteCount;
	bufferInfo.usage = vk::BufferUsageFlagBits::eTransferDst;
	bufferInfo.sharingMode = vk::SharingMode::eExclusive;
	const VkBufferCreateInfo rawBufferInfo = bufferInfo;

	VmaAllocationCreateInfo allocationInfo = {};
	allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
		VMA_ALLOCATION_CREATE_MAPPED_BIT;
	allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
	VkBuffer newBuffer = VK_NULL_HANDLE;
	VmaAllocation newAllocation = nullptr;
	VmaAllocationInfo mappedInfo = {};
	const VkResult createResult = vmaCreateBuffer(
		allocator,
		&rawBufferInfo,
		&allocationInfo,
		&newBuffer,
		&newAllocation,
		&mappedInfo);
	if (createResult != VK_SUCCESS || !newBuffer || !newAllocation ||
		!mappedInfo.pMappedData)
	{
		if (newBuffer && newAllocation)
		{
			vmaDestroyBuffer(allocator, newBuffer, newAllocation);
		}
		return false;
	}

	if (logicalDevice)
	{
		(void)logicalDevice.waitIdle();
	}
	destroy_composition_readback();
	compositionReadbackBuffer = newBuffer;
	compositionReadbackAllocation = newAllocation;
	compositionReadbackMapped = mappedInfo.pMappedData;
	compositionReadbackExtent = extent;
	vmaSetAllocationName(
		allocator,
		compositionReadbackAllocation,
		"Windows Composition host readback");
	return true;
}

void Engine::Impl::destroy_composition_readback()
{
	if (allocator && compositionReadbackBuffer &&
		compositionReadbackAllocation)
	{
		vmaDestroyBuffer(
			allocator,
			static_cast<VkBuffer>(compositionReadbackBuffer),
			compositionReadbackAllocation);
	}
	compositionReadbackBuffer = nullptr;
	compositionReadbackAllocation = nullptr;
	compositionReadbackMapped = nullptr;
	compositionReadbackExtent = vk::Extent2D{};
}

bool Engine::Impl::upload_software_composition(std::uint32_t bufferIndex, glm::uvec4 damage)
{
    const auto cpuTiming = measure_cpu_phase(RendererCpuPhase::eHostUpload);
	if (!compositionPresenter.available() ||
		compositionPresenter.gpu_interop() ||
		!compositionReadbackAllocation || !compositionReadbackMapped ||
		compositionReadbackExtent.width == 0u ||
		compositionReadbackExtent.height == 0u)
	{
		return false;
	}
    const auto readback = composition_readback_region(compositionReadbackExtent.width,
        compositionReadbackExtent.height, damage.x, damage.y, damage.z, damage.w, false);
    if (readback.width == 0u || readback.height == 0u) return true;
    if (vmaInvalidateAllocation(
			allocator,
			compositionReadbackAllocation,
            readback.byteOffset,
            readback.byteSpan) != VK_SUCCESS)
	{
		logger->warning(
			"Vulkan could not invalidate the Windows Composition host readback buffer.");
		return false;
	}
	return compositionPresenter.upload(
		bufferIndex,
		compositionReadbackMapped,
		compositionReadbackExtent.width,
		compositionReadbackExtent.height,
        compositionReadbackExtent.width * 4u,
        { readback.x, readback.y, readback.width, readback.height });
}

std::vector<SystemBackdropRegion>
Engine::Impl::collect_system_backdrop_regions(double renderTimeSeconds)
{
	const auto cpuTiming = measure_cpu_phase(RendererCpuPhase::eBackdropGeometry);
	if (!compositionPresenter.available())
	{
		return {};
	}

    struct OrderedBackdropRegion
	{
		entt::entity entity = entt::null;
        SystemBackdropRegion region = {};
        Renderer2DShapeVisualState visual = {};
	};
	std::vector<OrderedBackdropRegion> orderedRegions;
	entt::registry& registry = renderer2DScene.registry();
	auto view = registry.view<
		const Transform2DComponent,
		const ShapeComponent,
		const SystemBackdropComponent,
		const RenderLayer2DComponent>();
	orderedRegions.reserve(view.size_hint());
	const float outputScaleX = renderExtent.width > 0u ?
		static_cast<float>(swapchain.extent.width) / static_cast<float>(renderExtent.width) : 1.0f;
	const float outputScaleY = renderExtent.height > 0u ?
		static_cast<float>(swapchain.extent.height) / static_cast<float>(renderExtent.height) : 1.0f;
	view.each([&](
		entt::entity entity,
		const Transform2DComponent& transform,
		const ShapeComponent& shape,
		const SystemBackdropComponent& backdrop,
		const RenderLayer2DComponent& layer) {
		if (!layer.visible ||
			(backdrop.region.material != GlassMaterial::eSystemGlass &&
			 backdrop.region.material != GlassMaterial::eLiquid) ||
			std::abs(transform.rotationRadians) > 0.0001f ||
			glm::length(transform.rotation3DRadians) > 0.0001f)
		{
			return;
		}
		const std::optional<Renderer2DShapeVisualState> visual =
			renderer2DScene.resolved_shape_visual_state(
				entity,
				renderTimeSeconds);
		if (!visual)
		{
			return;
		}
		const glm::vec2 framebufferSize {
			visual->rect.z,
			visual->rect.w
		};
		const glm::vec2 topLeft {
			visual->rect.x,
			visual->rect.y
		};
		if (framebufferSize.x <= 0.0f || framebufferSize.y <= 0.0f)
		{
			return;
		}

        SystemBackdropRegion region = backdrop.region;
        region.identity = static_cast<std::uint64_t>(entt::to_integral(entity)) + 1u;
		region.opacity = std::clamp(
			region.opacity * visual->opacity,
			0.0f,
			1.0f);
		region.x = topLeft.x * outputScaleX;
		region.y = topLeft.y * outputScaleY;
		region.width = framebufferSize.x * outputScaleX;
		region.height = framebufferSize.y * outputScaleY;
		const float shapeScale = std::max(outputScaleX, outputScaleY);
		if (region.deriveShapeFromEntity)
		{
			const glm::vec4 radii = ui_layout_corner_radii(registry, entity, shape) * shapeScale;
			// Preserve explicit zero-radius corners instead of letting the bridge
			// replace them with the largest custom radius as a uniform fallback.
			region.cornerRadius = shape.customCornerRadii ?
				0.0f :
				shape.cornerRadius * shapeScale;
			region.topLeftRadius = radii.x;
			region.topRightRadius = radii.y;
			region.bottomRightRadius = radii.z;
			region.bottomLeftRadius = radii.w;
			region.squircleAmount = shape.squircleAmount;
			region.squirclePower = shape.squirclePower;
			region.notchAmount = shape.notchAmount;
			region.notchDepth = shape.notchDepth * shapeScale;
			switch (shape.primitive)
			{
			case Renderer2DPrimitive::eRectangle:
				region.shape = SystemBackdropShape::eRectangle;
				break;
			case Renderer2DPrimitive::eEllipse:
				region.shape = SystemBackdropShape::eEllipse;
				break;
			case Renderer2DPrimitive::eSquircle:
				region.shape = SystemBackdropShape::eSquircle;
				break;
            case Renderer2DPrimitive::eNotchedSquircle:
                region.shape = SystemBackdropShape::eNotchedSquircle;
                break;
            case Renderer2DPrimitive::eLiquidBridge:
                region.shape = SystemBackdropShape::eLiquidBridge;
                break;
			default:
				region.shape = SystemBackdropShape::eRoundedRectangle;
				break;
			}
		}
		else
		{
			region.cornerRadius *= shapeScale;
            // Explicit native silhouettes follow the same logical corners as
            // renderer shapes. Sidebar glass keeps a square content join.
            const auto radii = ui_layout_corner_radii(registry, entity, glm::vec4 {
                region.topLeftRadius, region.topRightRadius,
                region.bottomRightRadius, region.bottomLeftRadius }) * shapeScale;
            region.topLeftRadius = radii.x;
            region.topRightRadius = radii.y;
            region.bottomRightRadius = radii.z;
            region.bottomLeftRadius = radii.w;
		}
        orderedRegions.push_back({ entity, region, *visual });
	});

    const auto contourForRegion = [](const SystemBackdropRegion& region,
        float originX, float originY) {
        SystemBackdropRegion::Contour contour {};
        contour.x = region.x - originX;
        contour.y = region.y - originY;
        contour.width = region.width;
        contour.height = region.height;
        contour.cornerRadius = region.cornerRadius;
        contour.topLeftRadius = region.topLeftRadius;
        contour.topRightRadius = region.topRightRadius;
        contour.bottomRightRadius = region.bottomRightRadius;
        contour.bottomLeftRadius = region.bottomLeftRadius;
        contour.squircleAmount = region.squircleAmount;
        contour.squirclePower = region.squirclePower;
        contour.notchAmount = region.notchAmount;
        contour.notchDepth = region.notchDepth;
        contour.shape = static_cast<std::uint32_t>(region.shape);
        return contour;
    };
    const auto contourForEntity = [&](entt::entity entity, float originX,
        float originY) -> std::optional<SystemBackdropRegion::Contour> {
        const auto* shape = registry.try_get<ShapeComponent>(entity);
        const auto visual = renderer2DScene.resolved_shape_visual_state(entity,
            renderTimeSeconds);
        if (!shape || !visual) return std::nullopt;
        SystemBackdropRegion source {};
        source.x = visual->rect.x * outputScaleX;
        source.y = visual->rect.y * outputScaleY;
        source.width = visual->rect.z * outputScaleX;
        source.height = visual->rect.w * outputScaleY;
        const float scale = std::max(outputScaleX, outputScaleY);
        const auto radii = ui_layout_corner_radii(registry, entity, *shape) * scale;
        source.cornerRadius = shape->customCornerRadii ? 0.0f :
            shape->cornerRadius * scale;
        source.topLeftRadius = radii.x;
        source.topRightRadius = radii.y;
        source.bottomRightRadius = radii.z;
        source.bottomLeftRadius = radii.w;
        source.squircleAmount = shape->squircleAmount;
        source.squirclePower = shape->squirclePower;
        source.notchAmount = shape->notchAmount;
        source.notchDepth = shape->notchDepth * scale;
        switch (shape->primitive)
        {
        case Renderer2DPrimitive::eRectangle: source.shape = SystemBackdropShape::eRectangle; break;
        case Renderer2DPrimitive::eEllipse: source.shape = SystemBackdropShape::eEllipse; break;
        case Renderer2DPrimitive::eSquircle: source.shape = SystemBackdropShape::eSquircle; break;
        case Renderer2DPrimitive::eNotchedSquircle: source.shape = SystemBackdropShape::eNotchedSquircle; break;
        case Renderer2DPrimitive::eLiquidBridge: source.shape = SystemBackdropShape::eLiquidBridge; break;
        default: source.shape = SystemBackdropShape::eRoundedRectangle; break;
        }
        return contourForRegion(source, originX, originY);
    };
    // Backing cards expose only the silhouette outside the card in front.
    for (auto& entry : orderedRegions)
    {
        const auto* cutout = registry.try_get<ShapeCutout2DComponent>(entry.entity);
        if (!cutout || !cutout->affectBackdrop || cutout->source == entt::null ||
            !registry.valid(cutout->source)) continue;
        if (const auto exclusion = contourForEntity(cutout->source,
                entry.region.x, entry.region.y))
        {
            entry.region.exclusion = *exclusion;
            entry.region.hasExclusion = true;
        }
    }
    std::unordered_set<entt::entity> mergedParticipants;
    for (auto& owner : orderedRegions)
    {
        const auto* group = registry.try_get<SystemBackdropUnionComponent>(owner.entity);
        if (!group || owner.region.material != GlassMaterial::eLiquid) continue;
        std::array<const OrderedBackdropRegion*, 3> participants {};
        std::size_t count = 0u;
        for (const auto entity : group->participants)
        {
            if (entity == entt::null || mergedParticipants.count(entity)) continue;
            const auto found = std::find_if(orderedRegions.begin(), orderedRegions.end(),
                [entity](const auto& entry) { return entry.entity == entity; });
            if (found != orderedRegions.end() &&
                found->region.material == owner.region.material &&
                found->region.liquidBackend == owner.region.liquidBackend &&
                std::none_of(participants.begin(), participants.begin() + count,
                    [entity](const auto* entry) { return entry->entity == entity; }))
                participants[count++] = &*found;
        }
        if (count < 2u || std::none_of(participants.begin(), participants.begin() + count,
                [&](const auto* entry) { return entry->entity == owner.entity; })) continue;
        float left = std::numeric_limits<float>::max();
        float top = left;
        float right = std::numeric_limits<float>::lowest();
        float bottom = right;
        for (std::size_t index = 0u; index < count; ++index)
        {
            const auto& region = participants[index]->region;
            float flare = 0.0f;
            if (region.shape == SystemBackdropShape::eNotchedSquircle)
            {
                const float progress = std::clamp((region.notchAmount - 0.85f) / 0.15f, 0.0f, 1.0f);
                const float depth = region.notchDepth > 0.5f ? region.notchDepth :
                    std::clamp(region.height * 0.62f, 0.0f, 92.0f);
                flare = std::ceil(std::clamp(depth * 0.18f, 0.0f, region.height * 0.5f) *
                    progress * progress);
            }
            left = std::min(left, region.x - flare);
            top = std::min(top, region.y);
            right = std::max(right, region.x + region.width + flare);
            bottom = std::max(bottom, region.y + region.height);
        }
        auto merged = owner.region;
        merged.x = left;
        merged.y = top;
        merged.width = right - left;
        merged.height = bottom - top;
        merged.shape = SystemBackdropShape::eRectangle;
        merged.verticalStart = 0.0f;
        merged.hasExclusion = false;
        merged.contourCount = static_cast<std::uint32_t>(count);
        merged.paintCapGradient = group->paintCapGradient;
        merged.capGradient = group->capGradient;
        merged.capColour = group->capColour;
        for (std::size_t index = 0u; index < count; ++index)
        {
            merged.contours[index] = contourForRegion(participants[index]->region, left, top);
            if (participants[index]->entity != owner.entity)
                mergedParticipants.insert(participants[index]->entity);
        }
        owner.region = merged;
    }
    orderedRegions.erase(std::remove_if(orderedRegions.begin(), orderedRegions.end(),
        [&](const auto& entry) { return mergedParticipants.count(entry.entity) != 0u; }),
        orderedRegions.end());

    // Native materials follow sharp content's paint order, so a detached shell
    // cannot cover the main island during a merge. Identities retain their own
    // shader slots when the sorted order changes.
	std::stable_sort(
		orderedRegions.begin(),
		orderedRegions.end(),
		[](const OrderedBackdropRegion& left, const OrderedBackdropRegion& right) {
            const auto& a = left.visual;
            const auto& b = right.visual;
            if (a.alwaysOnTop != b.alwaysOnTop) return b.alwaysOnTop;
            if (a.stackLayer != b.stackLayer) return a.stackLayer < b.stackLayer;
            if (a.stackOrder != b.stackOrder) return a.stackOrder < b.stackOrder;
            if (a.layer != b.layer) return a.layer < b.layer;
            if (a.order != b.order) return a.order < b.order;
			return entt::to_integral(left.entity) < entt::to_integral(right.entity);
		});
	std::vector<SystemBackdropRegion> regions;
	regions.reserve(orderedRegions.size());
	for (const OrderedBackdropRegion& entry : orderedRegions)
	{
        auto fade = entry.visual.viewportFade;
        fade.rect *= glm::vec4(outputScaleX, outputScaleY, outputScaleX, outputScaleY);
        fade.topHeight *= outputScaleY;
        fade.bottomHeight *= outputScaleY;
        const auto clip = entry.visual.clipRect * glm::vec4(outputScaleX, outputScaleY, outputScaleX, outputScaleY);
        auto clipped = renderer2d_clip_backdrop_region(entry.region, clip, fade);
        regions.insert(regions.end(), clipped.begin(), clipped.end());
	}
	return regions;
}

void Engine::Impl::apply_system_backdrop_regions(
	const std::vector<SystemBackdropRegion>& regions)
{
	const auto cpuTiming = measure_cpu_phase(RendererCpuPhase::eBackdropApply);
	if (!compositionPresenter.available())
	{
		return;
	}
	bool unchanged = regions.size() == appliedBackdropRegions.size();
	if (unchanged)
	{
		for (std::size_t index = 0u; index < regions.size(); ++index)
		{
			if (!same_backdrop_region(regions[index], appliedBackdropRegions[index]))
			{
				unchanged = false;
				break;
			}
		}
	}
	if (!unchanged && compositionPresenter.set_regions(regions))
	{
		const bool topologyChanged =
			regions.size() != appliedBackdropRegions.size() ||
			std::mismatch(
				regions.begin(),
				regions.end(),
				appliedBackdropRegions.begin(),
				[](const SystemBackdropRegion& left, const SystemBackdropRegion& right) {
					return left.material == right.material &&
						left.provider == right.provider &&
						left.shape == right.shape;
				}).first != regions.end();
		if (topologyChanged)
		{
			Logger::fetch_logger()->info(
				"Windows Composition backdrop regions applied: " +
				std::to_string(regions.size()) + ".");
		}
		appliedBackdropRegions = regions;
	}
	else if (!unchanged)
	{
		Logger::fetch_logger()->warning(
			"Windows Composition rejected the requested backdrop regions.");
	}
}

void Engine::Impl::collect_gpu_render_timings(Frame& frame)
{
	if (!gpuTimestampDiagnosticsEnabled)
	{
		return;
	}
	Frame::GpuRenderTimings timings;
	if (!frame.take_completed_gpu_timings(timings))
	{
		return;
	}
	++diagnosticGpuSamples;
	diagnosticGpuTotalMilliseconds += timings.totalMilliseconds;
	diagnosticGpuRender2DMilliseconds += timings.render2DMilliseconds;
	diagnosticGpuCompositeMilliseconds += timings.compositeMilliseconds;
	diagnosticGpuOutputMilliseconds += timings.outputMilliseconds;
	diagnosticGpuMaxMilliseconds = std::max(
		diagnosticGpuMaxMilliseconds, timings.totalMilliseconds);
}

void Engine::Impl::publish_completed_composition_frames()
{
	if (!compositionPresenter.available())
	{
		return;
	}
	for (std::size_t pending = 0u;
		pending < compositionFramePending.size(); ++pending)
	{
		if (!compositionFramePending[pending])
		{
			continue;
		}
		const VkResult fenceStatus = vkGetFenceStatus(
			logicalDevice,
			frames[pending].renderFinishedFence);
		if (fenceStatus == VK_NOT_READY)
		{
			continue;
		}
		if (fenceStatus != VK_SUCCESS)
		{
			logger->warning(
				"Windows Composition could not query a completed Vulkan frame.");
			compositionFramePending[pending] = false;
			continue;
		}

		const auto presentResult = vibrance::win32::present_completed_composition_frame(
			compositionFrameUploaded[pending],
			[&] {
				collect_gpu_render_timings(frames[pending]);
				return compositionPresenter.gpu_interop() ||
					upload_software_composition(compositionFrameBufferIndices[pending],
						frames[pending].compositionReadbackRect);
			},
			[&] {
				apply_system_backdrop_regions(frames[pending].compositionBackdropRegions);
			},
			[&] {
				const auto cpuTiming = measure_cpu_phase(RendererCpuPhase::ePresent);
				return compositionPresenter.present(
					compositionFrameBufferIndices[pending],
					false,
					frames[pending].compositionContentRect,
					frames[pending].compositionDamageRect);
			});
		if (!presentResult)
		{
			compositionFramePending[pending] = false;
			compositionRecoveryPending = true;
			continue;
		}
		if (*presentResult == CompositionPresenter::PresentResult::eDeferred)
		{
			continue;
		}
		if (*presentResult == CompositionPresenter::PresentResult::eFailed)
		{
			logger->warning(
				"Windows Composition could not present a completed Vulkan frame.");
			compositionRecoveryPending = true;
		}
		else
		{
			consecutiveCompositionRecoveries = 0u;
		}
		compositionFramePending[pending] = false;
	}
}

void Engine::Impl::draw()
{
    poll_retired_media_uploads();
	RendererDrawActivity activity;
	if (renderDiagnosticsEnabled)
	{
		const auto diagnosticsNow = std::chrono::steady_clock::now();
		if (renderDiagnosticsStart.time_since_epoch().count() == 0)
		{
			renderDiagnosticsStart = diagnosticsNow;
		}
		else if (diagnosticsNow - renderDiagnosticsStart >=
			std::chrono::seconds(1))
		{
			const double seconds = std::chrono::duration<double>(
				diagnosticsNow - renderDiagnosticsStart).count();
			std::string gpuDiagnostics;
			if (gpuTimestampDiagnosticsEnabled)
			{
				gpuDiagnostics = " GPU timestamps unavailable.";
				if (gpuTimestampDiagnosticsAvailable)
				{
					gpuDiagnostics = " Sampled GPU timings (1 in 16 submissions): no completed samples.";
					if (diagnosticGpuSamples > 0u)
					{
						const double samples = static_cast<double>(diagnosticGpuSamples);
						std::ostringstream timings;
						timings.setf(std::ios::fixed, std::ios::floatfield);
						timings.precision(3);
						timings << " Sampled GPU command buffer (1 in 16 submissions): "
							<< diagnosticGpuTotalMilliseconds / samples
							<< " ms mean, " << diagnosticGpuMaxMilliseconds
							<< " ms max, " << diagnosticGpuSamples << " samples; "
							<< "checkpoint means: 2D "
							<< diagnosticGpuRender2DMilliseconds / samples
							<< " ms, composite "
							<< diagnosticGpuCompositeMilliseconds / samples
							<< " ms, output copy "
							<< diagnosticGpuOutputMilliseconds / samples << " ms.";
						gpuDiagnostics = timings.str();
					}
				}
				gpuDiagnostics += " 2D cache rebuilds: " +
					std::to_string(diagnosticCacheRebuilds) +
					", cached batch replays: " +
					std::to_string(diagnosticCachedBatchReplays) +
					", dynamic batch replays: " +
					std::to_string(diagnosticDynamicBatchReplays) + ".";
				gpuDiagnostics += " Text glyphs/dispatches/batched dispatches: " +
					std::to_string(diagnosticTextGlyphs) + "/" +
					std::to_string(diagnosticTextDispatches) + "/" +
					std::to_string(diagnosticTextBatchDispatches) +
					"; layered barriers: " + std::to_string(diagnosticLayeredBarriers) + ".";
			}
			static constexpr std::array<const char*,
				static_cast<std::size_t>(RendererCpuPhase::eCount)> cpuPhaseNames {
				"retained check", "cadence check", "redraw check", "glyph scan",
				"Scene+command recording", "backdrop geometry", "interop acquire",
                "surface acquire", "queue submit", "GPU fence wait", "host upload", "backdrop apply", "present"
			};
			std::ostringstream cpuDiagnostics;
			cpuDiagnostics.setf(std::ios::fixed, std::ios::floatfield);
			cpuDiagnostics.precision(3);
			cpuDiagnostics << " CPU wall timings (mean/max ms, samples; frame pacing excluded): ";
			bool firstCpuPhase = true;
			for (std::size_t phase = 0u; phase < diagnosticCpuTimings.size(); ++phase)
			{
				const auto& timing = diagnosticCpuTimings[phase];
				if (timing.samples == 0u)
				{
					continue;
				}
				if (!firstCpuPhase)
				{
					cpuDiagnostics << "; ";
				}
				firstCpuPhase = false;
				cpuDiagnostics << cpuPhaseNames[phase] << ' '
					<< timing.totalMilliseconds / static_cast<double>(timing.samples)
					<< '/' << timing.maxMilliseconds << " ms, " << timing.samples;
			}
			if (firstCpuPhase)
			{
				cpuDiagnostics << "no samples";
			}
			cpuDiagnostics << '.';
			logger->info(
				"Renderer diagnostics " +
				std::to_string(framebufferWidth) + "x" +
				std::to_string(framebufferHeight) + ": " +
				std::to_string(static_cast<uint64_t>(
					diagnosticSubmissions / std::max(seconds, 0.001))) +
				" submissions/s, " +
				std::to_string(diagnosticDrawCalls) + " draw calls, " +
				std::to_string(diagnosticGenerationChanges) +
				" scene-generation changes, " +
				std::to_string(diagnosticContinuousRequests) +
				" continuous-redraw requests, " +
				std::to_string(diagnosticRetainedSyncRequests) +
                " retained-buffer sync requests; " + std::to_string(diagnosticReadbackBytes) +
                " host-readback bytes." + gpuDiagnostics + cpuDiagnostics.str());
			renderDiagnosticsStart = diagnosticsNow;
			diagnosticDrawCalls = 0u;
			diagnosticSubmissions = 0u;
            diagnosticReadbackBytes = 0u;
			diagnosticGenerationChanges = 0u;
			diagnosticContinuousRequests = 0u;
			diagnosticRetainedSyncRequests = 0u;
			diagnosticGpuSamples = 0u;
			diagnosticGpuTotalMilliseconds = 0.0;
			diagnosticGpuRender2DMilliseconds = 0.0;
			diagnosticGpuCompositeMilliseconds = 0.0;
			diagnosticGpuOutputMilliseconds = 0.0;
			diagnosticGpuMaxMilliseconds = 0.0;
			diagnosticCacheRebuilds = 0u;
			diagnosticCachedBatchReplays = 0u;
			diagnosticDynamicBatchReplays = 0u;
			diagnosticTextGlyphs = 0u;
			diagnosticTextDispatches = 0u;
			diagnosticTextBatchDispatches = 0u;
			diagnosticLayeredBarriers = 0u;
			diagnosticCpuTimings = {};
		}
		++diagnosticDrawCalls;
	}
	if (!rendererReady)
	{
		return;
	}

	if (framebufferWidth == 0 || framebufferHeight == 0)
	{
		swapchain.outdated = true;
		return;
	}
	if (compositionRecoveryPending)
	{
		const auto now = std::chrono::steady_clock::now();
		if (now < nextCompositionRecoveryDeadline) return;
		// Publication errors are observed only after the sole Vulkan frame's
		// fence has completed. Recreate this window's bridge independently;
		// other windows continue ticking whilst a failed target backs off.
		compositionRecoveryPending = false;
		nextCompositionRecoveryDeadline = now + std::chrono::seconds(1);
		if (++consecutiveCompositionRecoveries >= 2u && compositionGpuInterop)
		{
			logger->warning("Windows Composition GPU sharing repeatedly stalled; using the asynchronous host-upload transport.");
			compositionGpuInterop = false;
		}
		(void)rebuild_composition_presenter();
		renderer2DScene.mark_dirty();
	}

	{
		const auto cpuTiming = measure_cpu_phase(RendererCpuPhase::eRetainedCheck);
		const double retainedTimeSeconds = std::chrono::duration<double>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		if (rendererHostUsesIdleEventWait && !swapchain.outdated && hasSubmittedFrame &&
			renderer2DScene.frame_generation() == lastSubmittedFrameGeneration &&
			!renderer2DScene.requires_continuous_redraw_since(retainedTimeSeconds,
				lastSubmittedRenderTimeSeconds, lastSubmittedFrameGeneration) &&
			(!compositionPresenter.available() || nativeTransparencyPrimed) &&
			std::none_of(compositionFramePending.begin(),
				compositionFramePending.end(), [](bool pending) { return pending; }))
		{
			// Retained images need no timer wake-up, font scan, render-plan work,
			// or invisible GPU submission. The host waits once after all windows
			// settle instead of polling each idle renderer twice per refresh.
			activity.settled = true;
			return;
		}
	}

	if (targetFrameRate > 0u && !externallyPacedFrameActive)
	{
		const auto interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<double>(1.0 / static_cast<double>(targetFrameRate)));
		auto now = std::chrono::steady_clock::now();
		if (nextFrameDeadline.time_since_epoch().count() != 0 && now < nextFrameDeadline)
		{
			wait_for_frame_deadline(nextFrameDeadline);
			now = std::chrono::steady_clock::now();
		}
		if (nextFrameDeadline.time_since_epoch().count() == 0 ||
			now - nextFrameDeadline > interval * 2)
		{
			nextFrameDeadline = now + interval;
		}
		else
		{
			nextFrameDeadline += interval;
		}
	}
	else
	{
		nextFrameDeadline = {};
	}

	if (swapchain.outdated) 
	{
		swapchain.rebuild(logicalDevice, physicalDevice, surface, framebufferWidth, framebufferHeight, transparentFramebuffer);
		if (!swapchain.chain || swapchain.images.empty())
		{
			if (!recreate_surface())
			{
				return;
			}
			swapchain.rebuild(logicalDevice, physicalDevice, surface, framebufferWidth, framebufferHeight, transparentFramebuffer);
			if (!swapchain.chain || swapchain.images.empty())
			{
				return;
			}
		}
		renderExtent = choose_render_extent(swapchain.extent.width, swapchain.extent.height, maxRenderPixels);
		modelRenderExtent = hosted_3d_extent(renderExtent, renderer2DScene);
		if (requestedPresentationBackend == PresentationBackend::eWindowsCompositionD3D11)
		{
			rebuild_composition_presenter();
		}
		for (Frame& frame : frames)
		{
			frame.resize_resources(renderExtent, modelRenderExtent);
		}
		externalBackdropAvailable = false;
		renderer2DScene.mark_dirty();
		frameIndex = 0;
		hasSubmittedFrame = false;
		lastSubmittedFrameGeneration = 0u;
	}

	uint32_t frameCountLocal = static_cast<uint32_t>(frames.size());
	if (frameCountLocal == 0)
	{
		return;
	}

	const bool compositionAvailable = compositionPresenter.available();
	const bool compositionGpuPath =
		compositionAvailable && compositionPresenter.gpu_interop();
	const double schedulingTimeSeconds = std::chrono::duration<double>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
	uint32_t activeUiFrameRateLimit;
	{
		const auto cpuTiming = measure_cpu_phase(RendererCpuPhase::eCadenceCheck);
		activeUiFrameRateLimit =
			renderer2DScene.active_frame_rate_limit(schedulingTimeSeconds);
	}
	const uint32_t effectiveCompositionFrameRate = externallyPacedFrameActive ?
		externallyPacedFrameRate : activeUiFrameRateLimit > 0u ?
			std::min(compositionFrameRate, activeUiFrameRateLimit) :
			compositionFrameRate;
	if (targetFrameRate == 0u && !externallyPacedFrameActive)
	{
		// Composition can only display at the monitor cadence. Poll twice per
		// visible frame so completed GPU work is published between submissions,
		// without running the entire application loop at 2100 Hz.
		const uint32_t maxUncappedPollRate = compositionAvailable ?
			std::clamp(effectiveCompositionFrameRate * 2u, 120u, 480u) : 2100u;
		const auto pollInterval =
			std::chrono::duration_cast<std::chrono::steady_clock::duration>(
				std::chrono::duration<double>(
					1.0 / static_cast<double>(maxUncappedPollRate)));
		auto pollNow = std::chrono::steady_clock::now();
		if (nextCompositionPollDeadline.time_since_epoch().count() != 0 &&
			pollNow < nextCompositionPollDeadline)
		{
			ensure_frame_timer_resolution();
			if (!wait_with_high_resolution_timer(nextCompositionPollDeadline))
			{
				std::this_thread::sleep_until(nextCompositionPollDeadline);
			}
			pollNow = std::chrono::steady_clock::now();
		}
		if (nextCompositionPollDeadline.time_since_epoch().count() == 0 ||
			pollNow - nextCompositionPollDeadline > pollInterval * 2)
		{
			nextCompositionPollDeadline = pollNow + pollInterval;
		}
		else
		{
			nextCompositionPollDeadline += pollInterval;
		}
	}
	else
	{
		nextCompositionPollDeadline = {};
	}

	bool submitCompositionFrame = false;
	if (compositionAvailable &&
		compositionPresenter.buffer_count() > 0u)
	{
		// Interop frames are published as soon as their Vulkan fence completes,
		// rather than waiting until the same frame slot cycles around again.
		// This removes two to three frames of Vulkan/D3D latency.
		publish_completed_composition_frames();
		if (compositionRecoveryPending) return;

		// Keep exactly one Vulkan frame in flight for the Composition bridge.
		// DWM's latency object accepts one visible frame at a time; filling the
		// remaining frame slots only rendered stale intermediate drag positions.
		// Those slots could later be published out of order, leaving transparent
		// trails and doing two or three times the useful GPU work. Once the queued
		// frame is consumed, the next submission samples the newest scene state.
		if (std::any_of(
				compositionFramePending.begin(),
				compositionFramePending.end(),
				[](bool pending) { return pending; }))
		{
			return;
		}

		const auto compositionNow = std::chrono::steady_clock::now();
		// Treat a user limit that is within two frames/second of the reported
		// monitor rate as the same cadence. Windows commonly reports 199 Hz for
		// a nominal 200 Hz mode; running two near-identical clocks creates a
		// visible beat even though both counters look fast.
		if (externallyPacedFrameActive ||
			(targetFrameRate > 0u &&
				targetFrameRate <= effectiveCompositionFrameRate + 2u))
		{
			submitCompositionFrame = true;
			nextCompositionSubmitDeadline = {};
		}
		else if (nextCompositionSubmitDeadline.time_since_epoch().count() == 0 ||
			compositionNow >= nextCompositionSubmitDeadline)
		{
			submitCompositionFrame = true;
			const auto interval =
				std::chrono::duration_cast<std::chrono::steady_clock::duration>(
					std::chrono::duration<double>(
						1.0 / static_cast<double>(effectiveCompositionFrameRate)));
			if (nextCompositionSubmitDeadline.time_since_epoch().count() == 0 ||
				compositionNow - nextCompositionSubmitDeadline > interval * 2)
			{
				nextCompositionSubmitDeadline = compositionNow + interval;
			}
			else
			{
				// Accumulate from the ideal deadline instead of from the wake-up
				// time. This prevents scheduler jitter from turning into drift.
				nextCompositionSubmitDeadline += interval;
			}
		}

		// DWM owns the visible target in Composition mode. Keep simulation/UI
		// updates uncapped, but do not submit an invisible duplicate GPU frame
		// between two monitor refreshes.
		if (!submitCompositionFrame)
		{
			return;
		}
	}
	else if (targetFrameRate == 0u && !externallyPacedFrameActive)
	{
		// Native Vulkan still presents only useful display opportunities. The
		// public timing loop remains uncapped, while duplicate GPU submissions
		// above the monitor refresh rate are omitted.
		const auto now = std::chrono::steady_clock::now();
		if (nextNativeSubmitDeadline.time_since_epoch().count() != 0 &&
			now < nextNativeSubmitDeadline)
		{
			return;
		}
		const auto interval =
			std::chrono::duration_cast<std::chrono::steady_clock::duration>(
				std::chrono::duration<double>(
					1.0 / static_cast<double>(
						std::max(effectiveCompositionFrameRate, 30u))));
		nextNativeSubmitDeadline = now + interval;
	}
	else
	{
		nextNativeSubmitDeadline = {};
	}

	// UI builders are allowed to create arbitrary UTF-8 literals. Discover any
	// code points that were not present during the initial localisation preload
	// and expand the atlas once before this scene is submitted.
	ensure_renderer2d_scene_glyphs();

	// Model attachments are an optional part of the frame graph.  Keep their
	// valid descriptor/framebuffer placeholders at 1x1 until a Model3D entity is
	// present, then restore the full render resolution before recording it.
	const vk::Extent2D desiredModelRenderExtent =
		hosted_3d_extent(renderExtent, renderer2DScene);
	if (desiredModelRenderExtent.width != modelRenderExtent.width ||
		desiredModelRenderExtent.height != modelRenderExtent.height)
	{
		modelRenderExtent = desiredModelRenderExtent;
		for (Frame& frame : frames)
		{
			frame.resize_resources(renderExtent, modelRenderExtent);
		}
		externalBackdropAvailable = false;
		renderer2DScene.mark_dirty();
		frameIndex = 0u;
		hasSubmittedFrame = false;
		lastSubmittedFrameGeneration = 0u;
	}

	const double renderTimeSeconds = std::chrono::duration<double>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
	const uint64_t sceneFrameGeneration = renderer2DScene.frame_generation();
	const bool retainedCompositionBuffersNeedSync =
		compositionAvailable && compositionBufferDamage.has_pending();
	bool continuousRedrawRequired;
	{
		const auto cpuTiming = measure_cpu_phase(RendererCpuPhase::eRedrawCheck);
		continuousRedrawRequired =
			renderer2DScene.requires_continuous_redraw_since(renderTimeSeconds,
				lastSubmittedRenderTimeSeconds, lastSubmittedFrameGeneration);
	}
	if (renderDiagnosticsEnabled)
	{
		if (sceneFrameGeneration != diagnosticLastSceneGeneration)
		{
			diagnosticLastSceneGeneration = sceneFrameGeneration;
			++diagnosticGenerationChanges;
		}
		diagnosticContinuousRequests += continuousRedrawRequired ? 1u : 0u;
		diagnosticRetainedSyncRequests +=
			retainedCompositionBuffersNeedSync ? 1u : 0u;
	}
	if (hasSubmittedFrame &&
		sceneFrameGeneration == lastSubmittedFrameGeneration &&
		!continuousRedrawRequired &&
		(!compositionAvailable || nativeTransparencyPrimed))
	{
		// DWM and native swapchains retain the last presented image. Imported
		// Composition buffers are caught up lazily: pending damage remains attached
		// to a retained buffer and is unioned into its next real scene update. Do not
		// submit standalone frames merely to rotate through currently invisible
		// buffers, because that turns a finite scene change into perpetual GPU work.
		activity.settled = true;
		return;
	}
	Frame& frame = frames[frameIndex];
    
	vk::Result fenceResult;
	{
		const auto cpuTiming = measure_cpu_phase(RendererCpuPhase::eFenceWait);
		fenceResult = logicalDevice.waitForFences(frame.renderFinishedFence, false,
			compositionAvailable || externallyPacedFrameActive ? 0u : UINT64_MAX);
	}
	if (fenceResult == vk::Result::eTimeout)
	{
		return;
	}
	if (fenceResult != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to wait for fence.");
		return;
	}
	collect_gpu_render_timings(frame);

	const bool clearNativeSurface =
		compositionAvailable && !nativeTransparencyPrimed;
	const bool presentNativeSurface =
		!compositionAvailable || clearNativeSurface;
	uint32_t imageIndex = 0;

	const bool useExternalBackdropUnderlay =
		externalBackdropAvailable &&
		swapchain.compositeAlpha == vk::CompositeAlphaFlagBitsKHR::eOpaque;
	uint32_t compositionBufferIndex = 0u;
	vk::Image compositionImage {};
	bool compositionBufferAcquired = false;
	bool compositionImageFirstUse = true;
	glm::uvec4 pendingCompositionDamageRect { 0u };
	if (compositionPresenter.available() && compositionPresenter.buffer_count() > 0u)
	{
		const auto cpuTiming = measure_cpu_phase(RendererCpuPhase::eInteropAcquire);
		const uint32_t compositionBufferCount =
			compositionPresenter.buffer_count();
		// Some drivers publish the imported images after presenter creation.
		// Synchronise retained-buffer history at first use as well as at creation;
		// otherwise only the newest texture is cleared and older drag positions
		// reappear whenever another shared buffer rotates into view.
		compositionBufferDamage.ensure_buffer_count(
			compositionBufferCount,
			glm::uvec4 { 0u, 0u, renderExtent.width, renderExtent.height });
		if (submitCompositionFrame)
		{
			const uint32_t bufferCount = compositionBufferCount;
			for (uint32_t offset = 0u; offset < bufferCount; ++offset)
			{
				const uint32_t candidate =
					(nextCompositionBufferIndex + offset) % bufferCount;
				bool reservedByVulkan = false;
				for (std::size_t pending = 0u;
					pending < compositionFramePending.size(); ++pending)
				{
					if (compositionFramePending[pending] &&
						compositionFrameBufferIndices[pending] == candidate)
					{
						reservedByVulkan = true;
						break;
					}
				}
				if (reservedByVulkan || !compositionPresenter.acquire(candidate))
				{
					continue;
				}

				compositionBufferIndex = candidate;
				compositionBufferAcquired = true;
				if (compositionGpuPath)
				{
					compositionImage = compositionPresenter.image(candidate);
				}
				compositionImageFirstUse = compositionPresenter.first_use(candidate);
				pendingCompositionDamageRect =
					compositionBufferDamage.pending_for(candidate);
				nextCompositionBufferIndex = (candidate + 1u) % bufferCount;
				break;
			}
		}
	}
	// If all interop buffers are still in use, there is nowhere for this frame
	// to become visible. Do not record and submit a full offscreen Vulkan pass;
	// retry with the freshest scene state at the next Composition opportunity.
	if (compositionAvailable && submitCompositionFrame &&
		(!compositionBufferAcquired ||
			(compositionGpuPath && !compositionImage)))
	{
		const auto now = std::chrono::steady_clock::now();
		if (compositionAcquireWaitSince.time_since_epoch().count() == 0)
			compositionAcquireWaitSince = now;
		else if (now - compositionAcquireWaitSince >= std::chrono::seconds(1))
		{
			logger->warning("Windows Composition shared buffers did not become available; recreating this window's presenter.");
			compositionRecoveryPending = true;
		}
		return;
	}
	compositionAcquireWaitSince = {};
	// Resolve optional Composition readiness first. Acquiring a native image
	// signals its binary semaphore, which must not be abandoned if a shared
	// buffer subsequently defers during the initial transparent prime.
	if (presentNativeSurface)
	{
		VkResult acquireResult;
		{
			const auto cpuTiming = measure_cpu_phase(RendererCpuPhase::eSurfaceAcquire);
			acquireResult = vkAcquireNextImageKHR(logicalDevice, swapchain.chain,
				externallyPacedFrameActive ? 0u : UINT64_MAX,
				frame.imageAcquiredSemaphore, VK_NULL_HANDLE, &imageIndex);
		}
		if (acquireResult == VK_NOT_READY || acquireResult == VK_TIMEOUT) return;
		if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
		{
			swapchain.outdated = true;
			return;
		}
		if (acquireResult == VK_ERROR_SURFACE_LOST_KHR)
		{
			recreate_surface();
			return;
		}
		// SUBOPTIMAL still acquires an image and signals the semaphore. Consume
		// both through submission/presentation, then rebuild on the next visit.
		if (acquireResult == VK_SUBOPTIMAL_KHR) swapchain.outdated = true;
		else if (acquireResult != VK_SUCCESS)
		{
			logger->vulkan("Failed to acquire swapchain image. VkResult: " +
				std::to_string(static_cast<int>(acquireResult)));
			return;
		}
	}

	vk::Result result = logicalDevice.resetFences(frame.renderFinishedFence);
	if (result != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to reset fences.");
		return;
	}
	if (gpuTimestampDiagnosticsEnabled)
	{
		// Keep the successful-submission index across one-second log resets so
		// retained scenes and low frame rates still sample only one frame in 16.
		frame.set_gpu_timing_sample_enabled(
			gpuTimestampSubmissionIndex % kGpuTimestampSampleInterval == 0u);
	}
	{
		const auto cpuTiming = measure_cpu_phase(RendererCpuPhase::eCommandRecording);
		frame.record_command_buffer(
			imageIndex,
			renderTimeSeconds,
			externalBackdropAvailable,
			useExternalBackdropUnderlay,
			presentNativeSurface,
			clearNativeSurface,
			compositionImage,
			compositionBufferAcquired && !compositionGpuPath ?
				compositionReadbackBuffer : vk::Buffer {},
			compositionImageFirstUse,
			pendingCompositionDamageRect,
			graphicsQueueFamilyIndex);
	}
	if (compositionBufferAcquired)
	{
		// Renderer2D has now advanced transitions and resolved layout for this
		// timestamp. Retain that exact native-material state until the matching
		// Vulkan frame reaches the presentation boundary.
		frame.compositionBackdropRegions =
			collect_system_backdrop_regions(renderTimeSeconds);
	}
	else
	{
		frame.compositionBackdropRegions.clear();
	}
	vk::SubmitInfo submitInfo = {};
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &frame.commandBuffer;
	submitInfo.waitSemaphoreCount = presentNativeSurface ? 1u : 0u;
	submitInfo.pWaitSemaphores = presentNativeSurface ?
		&frame.imageAcquiredSemaphore : nullptr;
	submitInfo.signalSemaphoreCount = presentNativeSurface ? 1u : 0u;
	submitInfo.pSignalSemaphores = presentNativeSurface ?
		&frame.renderFinishedSemaphore : nullptr;
	// The acquired native image is first touched by the transfer clear/copy in
	// Frame::record. Waiting at the transfer stage keeps that one-time
	// transparent prime correctly ordered on every driver.
	vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eTransfer;
	submitInfo.pWaitDstStageMask = &waitStage;

#if defined(_WIN32)
	// The NT shared texture is a keyed-mutex resource. Hand ownership from
	// D3D11 to Vulkan at key 0 and return it at key 1 after every render. The
	// Composition bridge performs the inverse transition around its copy.
	vk::DeviceMemory compositionMemory {};
	const std::uint64_t compositionAcquireKey =
		VIBRANCE_COMPOSITION_VULKAN_ACQUIRE_KEY;
	const std::uint64_t compositionReleaseKey =
		VIBRANCE_COMPOSITION_VULKAN_RELEASE_KEY;
	const std::uint32_t compositionAcquireTimeout =
		VIBRANCE_COMPOSITION_VULKAN_ACQUIRE_TIMEOUT_MS;
	vk::Win32KeyedMutexAcquireReleaseInfoKHR keyedMutexInfo = {};
	if (compositionImage)
	{
		compositionMemory = compositionPresenter.memory(compositionBufferIndex);
		if (!compositionMemory)
		{
			logger->warning(
				"Windows Composition buffer has no imported Vulkan memory.");
			return;
		}
		keyedMutexInfo.acquireCount = 1u;
		keyedMutexInfo.pAcquireSyncs = &compositionMemory;
		keyedMutexInfo.pAcquireKeys = &compositionAcquireKey;
		keyedMutexInfo.pAcquireTimeouts = &compositionAcquireTimeout;
		keyedMutexInfo.releaseCount = 1u;
		keyedMutexInfo.pReleaseSyncs = &compositionMemory;
		keyedMutexInfo.pReleaseKeys = &compositionReleaseKey;
		submitInfo.pNext = &keyedMutexInfo;
	}
#endif

	{
		const auto cpuTiming = measure_cpu_phase(RendererCpuPhase::eQueueSubmit);
		result = graphicsQueue.submit(submitInfo, frame.renderFinishedFence);
	}
	if (result != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to submit buffer to graphics queue.");
		return;
	}
	if (renderDiagnosticsEnabled)
	{
		++diagnosticSubmissions;
        diagnosticReadbackBytes += std::uint64_t(frame.compositionReadbackRect.z) *
            frame.compositionReadbackRect.w * 4u;
	}
	if (gpuTimestampDiagnosticsEnabled)
	{
		frame.mark_gpu_timing_submitted();
		++gpuTimestampSubmissionIndex;
		const auto diagnostics = frame.renderer2D.record_diagnostics();
		diagnosticCacheRebuilds += diagnostics.cacheRebuilds;
		diagnosticCachedBatchReplays += diagnostics.cachedBatchReplays;
		diagnosticDynamicBatchReplays += diagnostics.dynamicBatchReplays;
		diagnosticTextGlyphs += diagnostics.textGlyphs;
		diagnosticTextDispatches += diagnostics.textDispatches;
		diagnosticTextBatchDispatches += diagnostics.textBatchDispatches;
		diagnosticLayeredBarriers += diagnostics.layeredBarriers;
	}
	hasSubmittedFrame = true;
	lastSubmittedFrameGeneration = sceneFrameGeneration;
	lastSubmittedRenderTimeSeconds = renderTimeSeconds;
	if (compositionBufferAcquired)
	{
		compositionBufferDamage.commit(
			compositionBufferIndex,
			frame.compositionSceneDamageRect);
	}
	if (compositionBufferAcquired)
	{
		compositionPresenter.mark_used(compositionBufferIndex);
		if (frameIndex < compositionFramePending.size())
		{
			compositionFramePending[frameIndex] = true;
			compositionFrameUploaded[frameIndex] = false;
			compositionFrameBufferIndices[frameIndex] = compositionBufferIndex;
		}
	}

	if (presentNativeSurface)
	{
		vk::PresentInfoKHR presentInfo = {};
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapchain.chain;
		presentInfo.pImageIndices = &imageIndex;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &frame.renderFinishedSemaphore;
		VkPresentInfoKHR rawPresentInfo = presentInfo;
		VkResult presentResult;
		{
			const auto cpuTiming = measure_cpu_phase(RendererCpuPhase::ePresent);
			presentResult = vkQueuePresentKHR(graphicsQueue, &rawPresentInfo);
		}

		if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
			presentResult == VK_SUBOPTIMAL_KHR)
		{
			swapchain.outdated = true;
			return;
		}
		if (presentResult == VK_ERROR_SURFACE_LOST_KHR)
		{
			recreate_surface();
			return;
		}
		if (presentResult != VK_SUCCESS)
		{
			logger->vulkan(
				"Failed to present swapchain image. VkResult: " +
				std::to_string(static_cast<int>(presentResult)));
			return;
		}
		if (clearNativeSurface)
		{
			nativeTransparencyPrimed = true;
		}
	}

	// Publish an already completed frame immediately, otherwise let the next
	// host tick poll it. A slow display or host-upload device must never hold
	// the shared UI thread in a fence or DXGI wait ahead of the other windows.
	if (compositionBufferAcquired)
	{
		publish_completed_composition_frames();
	}

	frameIndex = (frameIndex + 1) % frameCountLocal;
}

bool Engine::Impl::set_external_backdrop_rgba(
	uint32_t width,
	uint32_t height,
	const unsigned char* rgba,
	std::size_t byteCount)
{
	if (!rendererReady || !logicalDevice || !allocator || !mainCommandBuffer || !graphicsQueue)
	{
		return false;
	}
	if (width == 0u || height == 0u || rgba == nullptr)
	{
		clear_external_backdrop();
		return false;
	}
	if (width != renderExtent.width || height != renderExtent.height)
	{
		logger->vulkan("Ignored external backdrop upload with mismatched extent " +
			std::to_string(width) + "x" + std::to_string(height) + ".");
		return false;
	}
	if (graphicsQueue.waitIdle() != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to wait for the graphics queue before updating the external backdrop.");
		return false;
	}

	std::vector<StorageImage*> targets;
	targets.reserve(frames.size());
	for (Frame& frame : frames)
	{
		if (!frame.externalBackdropSurface)
		{
			return false;
		}
		targets.push_back(frame.externalBackdropSurface);
	}

	const bool uploadedAny = upload_rgba_to_images(
		allocator,
		mainCommandBuffer,
		graphicsQueue,
		targets,
		rgba,
		byteCount,
		"external backdrop");

	externalBackdropAvailable = uploadedAny;
	if (uploadedAny)
	{
		renderer2DScene.mark_dirty();
	}
	return uploadedAny;
}

void Engine::Impl::clear_external_backdrop()
{
	if (!externalBackdropAvailable)
	{
		return;
	}
	externalBackdropAvailable = false;
	renderer2DScene.mark_dirty();
}

int Engine::Impl::update_timing(double currentTimeSeconds) 
{
	currentTime = currentTimeSeconds;
	double delta = currentTime - lastTime;

	if (delta >= 1) 
    {
		int framerate{ std::max(1, int(numFrames / delta)) };
		lastTime = currentTime;
		numFrames = -1;
		frameTime = float(1000.0 / framerate);
		numFrames++;
		return framerate;
	}

	numFrames++;
	return 0;
}

bool Engine::Impl::recreate_surface()
{
	if (!createSurface || !instance)
	{
		logger->vulkan("Cannot recreate Vulkan surface without a surface callback.");
		return false;
	}

	if (logicalDevice && logicalDevice.waitIdle() != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to wait for device idle before recreating the surface.");
		return false;
	}

	if (logicalDevice)
	{
		swapchain.destroy(logicalDevice);
	}

	if (surface)
	{
		instance.destroySurfaceKHR(surface);
		surface = nullptr;
	}

	VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
	if (createSurface(instance, surfaceUserData, &rawSurface) != 0 || rawSurface == VK_NULL_HANDLE)
	{
		logger->vulkan("Failed to recreate Vulkan surface.");
		return false;
	}

	surface = rawSurface;
	swapchain.outdated = true;
	logger->vulkan("Recreated Vulkan surface.");
	return true;
}

void Engine::Impl::resize(uint32_t width, uint32_t height)
{
	if (framebufferWidth == width && framebufferHeight == height) return;

	framebufferWidth = width;
	framebufferHeight = height;
	renderExtent = choose_render_extent(width, height, maxRenderPixels);
	swapchain.outdated = true;
}

Model3DHandle Engine::Impl::load_model_3d(const std::filesystem::path& path)
{
	Model3DHandle handle = {};
	if (!logicalDevice || !allocator || !mainCommandBuffer || !graphicsQueue)
	{
		logger->vulkan("Cannot load 3D model before the renderer is ready.");
		return handle;
	}

	const std::filesystem::path absolutePath = std::filesystem::absolute(path).lexically_normal();
	const std::string cacheKey =
		absolutePath.string() + "|" + resource_file_cache_version(absolutePath);
	if (const auto existing = modelIdsByPath.find(cacheKey); existing != modelIdsByPath.end())
	{
		const auto modelIt = modelAssets.find(existing->second);
		if (modelIt != modelAssets.end())
		{
			const StorageBuffer& buffer = modelIt->second.buffer;
			handle.id = existing->second;
			handle.firstTriangle = 0u;
			handle.triangleCount = buffer.triangleCount;
			return handle;
		}
	}

	if (graphicsQueue.waitIdle() != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to wait for the graphics queue before loading a 3D model.");
		return handle;
	}

	Model3DAsset asset = load_gltf_mesh(
		absolutePath,
		allocator,
		vmaDeletionQueue,
		deviceDeletionQueue,
		mainCommandBuffer,
		graphicsQueue,
		logicalDevice,
		descriptorPools[DescriptorScope::eModel3DTexture],
		descriptorSetLayouts[DescriptorScope::eModel3DTexture]);
	if (!asset.valid())
	{
		logger->vulkan("3D model upload produced no drawable triangles: " + absolutePath.string());
		return handle;
	}

	const uint32_t modelId = nextModelId++;
	const StorageBuffer& buffer = asset.buffer;
	handle.id = modelId;
	handle.firstTriangle = 0u;
	handle.triangleCount = buffer.triangleCount;
	modelAssets.emplace(modelId, std::move(asset));
	modelIdsByPath.emplace(cacheKey, modelId);
	logger->vulkan("Registered app-owned 3D model " + absolutePath.string() +
		" as handle " + std::to_string(modelId) + " with " +
		std::to_string(handle.triangleCount) + " triangles.");
	return handle;
}

Media2DHandle Engine::Impl::load_media_2d(const std::filesystem::path& path, const Media2DLoadOptions& options)
{
	Media2DHandle handle = {};
	if (!logicalDevice || !allocator || !mainCommandBuffer || !graphicsQueue)
	{
		logger->vulkan("Cannot load 2D media before the renderer is ready.");
		return handle;
	}

	const std::filesystem::path absolutePath = std::filesystem::absolute(path).lexically_normal();
	const std::string cacheKey = media_2d_cache_key(absolutePath, options);
	if (const auto existing = mediaIdsByPath.find(cacheKey); existing != mediaIdsByPath.end())
	{
		const auto mediaIt = mediaAssets.find(existing->second);
		if (mediaIt != mediaAssets.end())
		{
			return mediaIt->second.handle();
		}
	}

	if (graphicsQueue.waitIdle() != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to wait for the graphics queue before loading 2D media.");
		return handle;
	}

	const uint32_t mediaId = nextMediaId++;
	Media2DAsset asset = {};
	handle = load_media_2d_asset(
		absolutePath,
		options,
		mediaId,
		allocator,
		vmaDeletionQueue,
		deviceDeletionQueue,
		mainCommandBuffer,
		graphicsQueue,
		logicalDevice,
		descriptorPools[DescriptorScope::eMediaTexture],
		descriptorSetLayouts[DescriptorScope::eMediaTexture],
		asset);
	if (!handle.valid())
	{
		return {};
	}

	mediaAssets.emplace(mediaId, std::move(asset));
	mediaIdsByPath.emplace(cacheKey, mediaId);
	renderer2DScene.mark_dirty();
	return handle;
}

Media2DUploadResult Engine::Impl::upload_media_2d(
    const PreparedMedia2D& prepared, uint32_t maxFrameUploads)
{
    poll_retired_media_uploads();
    if (!prepared.valid() || !logicalDevice || !allocator || !graphicsQueue)
        return { {}, true };
    const auto& key = prepared.cache_key();
    if (const auto cached = mediaIdsByPath.find(key); cached != mediaIdsByPath.end())
    {
        if (const auto asset = mediaAssets.find(cached->second); asset != mediaAssets.end())
        {
            const auto handle = asset->second.handle();
            // A synchronous caller may have completed the same media while
            // its prepared twin was still waiting for a submission to retire.
            cancel_media_2d_upload(prepared);
            return { handle, true };
        }
    }
    if (maxFrameUploads == 0u) return {};
    auto position = pendingMediaUploads.find(key);
    if (position == pendingMediaUploads.end())
    {
        const auto bytes = prepared_media_2d_upload_bytes(prepared);
        if (bytes > PreparedMediaUploadBudget::limit) return { {}, true };
        // Cancelled submissions retain their reservation until their fences
        // retire; repeated navigation cannot accumulate unlimited GPU work.
        if (preparedMediaBudget.activeUploads >= PreparedMediaUploadBudget::maximumActiveUploads ||
            bytes > PreparedMediaUploadBudget::limit - preparedMediaBudget.reservedBytes) return {};
        position = pendingMediaUploads.try_emplace(key).first;
        position->second.reservedBytes = bytes;
        (void)preparedMediaBudget.reserve(bytes);
    }
    auto& pending = position->second;
    if (pending.cancelled) return {};
    const uint32_t id = pending.asset.id != 0u ? pending.asset.id : nextMediaId++;
    const auto result = upload_prepared_media_2d_asset_async(prepared, maxFrameUploads, id,
        allocator, logicalDevice, graphicsQueue, graphicsQueueFamilyIndex,
        descriptorSetLayouts[DescriptorScope::eMediaTexture], pending);
    if (!result.finished) return result;
    if (!result.handle.valid())
    {
        cancel_media_2d_upload(prepared);
        return result;
    }
    const auto asset = mediaAssets.emplace(result.handle.id, std::move(pending.asset)).first;
    pending.publishedAsset = &asset->second;
    mediaIdsByPath.emplace(key, result.handle.id);
    pending.published = true;
    preparedMediaBudget.release(pending.reservedBytes);
    pending.reservedBytes = 0u;
    // Resource ownership stays here beside the normal Engine media cache.
    // Only the asset wrapper moves; its descriptor pool/images remain alive.
    renderer2DScene.mark_dirty();
    return result;
}

void Engine::Impl::cancel_media_2d_upload(const PreparedMedia2D& prepared)
{
    const auto found = pendingMediaUploads.find(prepared.cache_key());
    if (found == pendingMediaUploads.end() || found->second.published) return;
    found->second.cancelled = true;
    hasRetiredPreparedMediaUploads = true;
    poll_retired_media_uploads();
}

void Engine::Impl::poll_retired_media_uploads()
{
    if (!hasRetiredPreparedMediaUploads) return;
    hasRetiredPreparedMediaUploads = false;
    for (auto position = pendingMediaUploads.begin(); position != pendingMediaUploads.end();)
    {
        auto& pending = position->second;
        if (!pending.cancelled) { ++position; continue; }
        if (!prepared_media_2d_upload_retired(logicalDevice, pending))
        {
            hasRetiredPreparedMediaUploads = true;
            ++position;
            continue;
        }
        const auto publishedId = pending.publishedAsset ? pending.publishedAsset->id : 0u;
        const auto reserved = pending.reservedBytes;
        release_prepared_media_2d_upload(allocator, logicalDevice, pending);
        if (publishedId != 0u) mediaAssets.erase(publishedId);
        preparedMediaBudget.release(reserved);
        position = pendingMediaUploads.erase(position);
    }
}

void Engine::Impl::clear_pending_media_uploads()
{
    // Engine teardown already waited for its graphics queue. Completed assets
    // and any last cancelled submissions can now be released together.
    for (auto& [key, pending] : pendingMediaUploads)
        release_prepared_media_2d_upload(allocator, logicalDevice, pending);
    pendingMediaUploads.clear();
    preparedMediaBudget = {};
    hasRetiredPreparedMediaUploads = false;
}

bool Engine::Impl::set_present_mode(RendererPresentMode mode)
{
	if (swapchain.presentModePreference == mode)
	{
		return true;
	}

	swapchain.presentModePreference = mode;
	if (rendererReady)
	{
		swapchain.outdated = true;
	}
	return true;
}

RendererPresentMode Engine::Impl::present_mode_preference() const
{
	return swapchain.presentModePreference;
}

RendererPresentMode Engine::Impl::active_present_mode() const
{
	return swapchain.activePresentMode;
}

std::vector<RendererPresentMode> Engine::Impl::available_present_modes() const
{
	return swapchain.supportedPresentModes;
}

void Engine::Impl::set_target_frame_rate(uint32_t frameRate)
{
	targetFrameRate = std::min(frameRate, 1000u);
	nextFrameDeadline = {};
	nextCompositionSubmitDeadline = {};
	nextCompositionPollDeadline = {};
	nextNativeSubmitDeadline = {};
	nextCompositionBufferIndex = 0u;
}

uint32_t Engine::Impl::target_frame_rate() const
{
	return targetFrameRate;
}

uint32_t Engine::Impl::recommended_ui_update_rate() const
{
	if (externallyPacedFrameActive)
	{
		return std::clamp(externallyPacedFrameRate, 1u, 1000u);
	}
	uint32_t visibleRate = targetFrameRate;
	if (compositionPresenter.available())
	{
		const uint32_t displayRate = std::max(compositionFrameRate, 30u);
		visibleRate = visibleRate > 0u ?
			std::min(visibleRate, displayRate) : displayRate;
	}
	else if (visibleRate == 0u)
	{
		// Native uncapped rendering remains uncapped. This only prevents input,
		// layout, and media state from being recomputed thousands of times/sec.
		visibleRate = 180u;
	}
	// Sampling input and animation once per visible frame is sufficient.  The
	// previous 2x oversampling dirtied dynamic UI twice for every frame DWM
	// could display and needlessly doubled CPU-side scene work.
	return std::clamp(visibleRate, 60u, 240u);
}

AudioClipHandle Engine::Impl::load_audio_clip(const std::filesystem::path& path)
{
	return audioEngine.load_clip(path);
}

AudioEngine& Engine::Impl::audio()
{
	return audioEngine;
}

const AudioEngine& Engine::Impl::audio() const
{
	return audioEngine;
}

bool Engine::Impl::load_renderer2d_font(const std::filesystem::path& path)
{
	Renderer2DFontAtlasLoadOptions options = {};
	return load_renderer2d_font(path, options);
}

bool Engine::Impl::load_renderer2d_font(const std::filesystem::path& path, const Renderer2DFontAtlasLoadOptions& options)
{
	return load_renderer2d_font_internal(path, options, true);
}

bool Engine::Impl::load_renderer2d_font_internal(
	const std::filesystem::path& path,
	const Renderer2DFontAtlasLoadOptions& options,
	bool explicitRequest)
{
	if (!logicalDevice || !allocator || !mainCommandBuffer || !graphicsQueue)
	{
		logger->vulkan("Cannot load Renderer2D font before the renderer is ready.");
		return false;
	}

	if (graphicsQueue.waitIdle() != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to wait for the graphics queue before loading a Renderer2D font.");
		return false;
	}

	const bool loaded = renderer2DFontAtlas.load_from_file(
		path,
		options,
		allocator,
		mainCommandBuffer,
		graphicsQueue,
		logicalDevice,
		vmaDeletionQueue,
		deviceDeletionQueue);
	if (!loaded)
	{
		logger->vulkan("Renderer2D font atlas is using its fallback texture.");
		return false;
	}

	renderer2DFontPath = path;
	renderer2DFontOptions = options;
	if (explicitRequest)
	{
		// A new face or fallback chain may cover code points which an earlier
		// configuration could not. Let the next scene scan try them again.
		attemptedRenderer2DCodepoints.clear();
		lastRenderer2DGlyphScanGeneration = UINT64_MAX;
		hasRenderer2DTextFingerprint = false;
	}

	// Frames created before a runtime font load initially bind their fallback
	// image. Point both the live and retained-cache descriptor sets at the newly
	// uploaded atlas before any text using it can be submitted.
	StorageImage* fontAtlasImage = renderer2DFontAtlas.image();
	for (Frame& frame : frames)
	{
		frame.set_font_atlas_image(fontAtlasImage);
	}

	refresh_localised_texts();
	auto textView = renderer2DScene.registry().view<TextComponent>(entt::exclude<LocalisedTextComponent>);
	textView.each([this](entt::entity entity, TextComponent&) {
		apply_font_layout(
			renderer2DFontAtlas,
			renderer2DScene.registry(),
			entity);
	});
	renderer2DScene.mark_dirty();
	return true;
}

void Engine::Impl::ensure_renderer2d_scene_glyphs()
{
	const auto cpuTiming = measure_cpu_phase(RendererCpuPhase::eGlyphScan);
	const uint64_t sceneGeneration = renderer2DScene.frame_generation();
	if (sceneGeneration == lastRenderer2DGlyphScanGeneration ||
		renderer2DFontPath.empty() ||
		!renderer2DFontAtlas.loaded())
	{
		return;
	}
	lastRenderer2DGlyphScanGeneration = sceneGeneration;

	auto textView = renderer2DScene.registry().view<TextComponent>();
	uint64_t textFingerprint = 1469598103934665603ull;
	textView.each([&](entt::entity, const TextComponent& text) {
		textFingerprint ^= static_cast<uint64_t>(text.text.size());
		textFingerprint *= 1099511628211ull;
		for (const unsigned char byte : text.text)
		{
			textFingerprint ^= static_cast<uint64_t>(byte);
			textFingerprint *= 1099511628211ull;
		}
	});
	if (hasRenderer2DTextFingerprint &&
		textFingerprint == lastRenderer2DTextFingerprint)
	{
		return;
	}
	lastRenderer2DTextFingerprint = textFingerprint;
	hasRenderer2DTextFingerprint = true;

	std::string literalPreloadText;
	std::size_t newCodepointCount = 0u;
	textView.each([&](entt::entity, const TextComponent& text) {
		bool appendText = false;
		for (const uint32_t codepoint :
			renderer2DFontAtlas.missing_codepoints(text.text))
		{
			if (attemptedRenderer2DCodepoints.insert(codepoint).second)
			{
				appendText = true;
				++newCodepointCount;
			}
		}
		if (!appendText)
		{
			return;
		}
		if (!literalPreloadText.empty())
		{
			literalPreloadText.push_back('\n');
		}
		literalPreloadText += text.text;
	});

	if (literalPreloadText.empty())
	{
		return;
	}

	Renderer2DFontAtlasLoadOptions expandedOptions = renderer2DFontOptions;
	if (!expandedOptions.preloadText.empty())
	{
		expandedOptions.preloadText.push_back('\n');
	}
	expandedOptions.preloadText += literalPreloadText;
	// Runtime literals should never discard all usable text because one face is
	// missing a script. The loader reports unavailable glyphs and keeps every
	// character covered by the configured fallback chain.
	expandedOptions.requireRequestedGlyphs = false;
	logger->vulkan(
		"Expanding Renderer2D font atlas for " +
		std::to_string(newCodepointCount) +
		" code points discovered in scene text.");
	load_renderer2d_font_internal(
		renderer2DFontPath,
		expandedOptions,
		false);
	lastRenderer2DGlyphScanGeneration = renderer2DScene.frame_generation();
}

bool Engine::Impl::load_localisation_directory(const std::filesystem::path& directory)
{
	const bool loaded = localisation_.load_directory(directory);
	if (loaded)
	{
		refresh_localised_texts();
	}
	return loaded;
}

bool Engine::Impl::load_localisation_directories(
	const std::vector<std::filesystem::path>& directories)
{
	localisation_.clear();
	bool loadedAny = false;
	bool mergeExisting = false;
	for (const std::filesystem::path& directory : directories)
	{
		const bool loaded = localisation_.load_directory(directory, mergeExisting);
		loadedAny = loaded || loadedAny;
		mergeExisting = mergeExisting || loaded;
	}
	if (loadedAny)
	{
		refresh_localised_texts();
	}
	return loadedAny;
}

bool Engine::Impl::set_locale(const std::string& localeValue)
{
	if (!localisation_.set_locale(localeValue))
	{
		return false;
	}
	renderer2DScene.registry().ctx().insert_or_assign(UiLayoutDirectionComponent {
        ui_locale_layout_direction(localisation_.locale()) });
	renderer2DScene.mark_dirty();
	refresh_localised_texts();
	return true;
}

std::string Engine::Impl::locale() const
{
	return localisation_.locale();
}

std::string Engine::Impl::resolve_text(const Text& text) const
{
	return localisation_.resolve(text);
}

RenderBackend Engine::Impl::render_backend() const
{
	return renderBackend;
}

PresentationBackend Engine::Impl::presentation_backend() const
{
	return activePresentationBackend;
}

bool Engine::Impl::system_backdrop_available() const
{
	return compositionPresenter.available();
}

bool Engine::Impl::ready() const
{
	return rendererReady;
}

std::string Engine::Impl::vulkan_api_version() const
{
	if (!physicalDevice)
	{
		return {};
	}
	const std::uint32_t version = physicalDevice.getProperties().apiVersion;
	return std::to_string(vk::apiVersionMajor(version)) + "." +
		std::to_string(vk::apiVersionMinor(version)) + "." +
		std::to_string(vk::apiVersionPatch(version));
}

Localisation& Engine::Impl::localisation()
{
	return localisation_;
}

const Localisation& Engine::Impl::localisation() const
{
	return localisation_;
}

void Engine::Impl::refresh_localised_texts()
{
	auto view = renderer2DScene.registry().view<TextComponent, LocalisedTextComponent>();
	bool refreshedAny = false;
	view.each([this, &refreshedAny](entt::entity entity, TextComponent& text, const LocalisedTextComponent& localised) {
		text.text = localisation_.resolve(localised.value);
		apply_font_layout(
			renderer2DFontAtlas,
			renderer2DScene.registry(),
			entity);
		renderer2DScene.mark_dirty(entity);
		refreshedAny = true;
	});
	if (refreshedAny)
	{
		renderer2DScene.mark_dirty();
	}
}

uint32_t Engine::Impl::render_width() const
{
	return renderExtent.width;
}

uint32_t Engine::Impl::render_height() const
{
	return renderExtent.height;
}

Renderer2DScene& Engine::Impl::renderer2d_scene()
{
	return renderer2DScene;
}

const Renderer2DScene& Engine::Impl::renderer2d_scene() const
{
	return renderer2DScene;
}

Renderer2DFontAtlas& Engine::Impl::renderer2d_font_atlas()
{
	return renderer2DFontAtlas;
}

const Renderer2DFontAtlas& Engine::Impl::renderer2d_font_atlas() const
{
	return renderer2DFontAtlas;
}

Engine::Impl::~Impl()
{
	if (logicalDevice && graphicsQueue)
	{
		vk::Result result = graphicsQueue.waitIdle();
		if (result != vk::Result::eSuccess)
		{
			logger->vulkan("Failed to wait for queue to idle.");
		}
	}
	// Keep the cache alive through device use so late driver optimisation can
	// contribute to the final snapshot. Destroy it before its owning VkDevice.
	pipelineCache.reset();
	compositionPresenter.shutdown(logicalDevice);
	destroy_composition_readback();

    logger->vulkan("Exiting application.");

    clear_pending_media_uploads();

    while (vmaDeletionQueue.size() > 0) 
    {
		vmaDeletionQueue.back()(allocator);
		vmaDeletionQueue.pop_back();
	}

	for (auto &f : frames) f.free_resources();

	if (allocator) vmaDestroyAllocator(allocator);

	if (logicalDevice) swapchain.destroy(logicalDevice);

    while (deviceDeletionQueue.size() > 0) 
    {
		deviceDeletionQueue.back()(logicalDevice);
		deviceDeletionQueue.pop_back();
	}

    while (instanceDeletionQueue.size() > 0)
    {
        instanceDeletionQueue.back()(instance);
        instanceDeletionQueue.pop_back();
    }
}

std::string Engine::graphics_api_version() const { return vulkan_api_version(); }
