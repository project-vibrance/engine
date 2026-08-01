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
#include <vibranceUI/core/camera.h>
#include "../directx/composition_presenter.h"
#include <sstream>
#include <string_view>
#include <deque>
#include <functional>
#include <unordered_map>
#include <vector>
#include <array>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <vma/vk_mem_alloc.h>
#include <filesystem>
#include <chrono>
#include <cmath>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
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
		const auto writeTicks = error ? 0 : writeTime.time_since_epoch().count();
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
		return left.material == right.material &&
			left.provider == right.provider &&
			left.shape == right.shape &&
			close(left.x, right.x) && close(left.y, right.y) &&
			close(left.width, right.width) && close(left.height, right.height) &&
			close(left.cornerRadius, right.cornerRadius) &&
			close(left.topLeftRadius, right.topLeftRadius) &&
			close(left.topRightRadius, right.topRightRadius) &&
			close(left.bottomRightRadius, right.bottomRightRadius) &&
			close(left.bottomLeftRadius, right.bottomLeftRadius) &&
			close(left.squircleAmount, right.squircleAmount) &&
			close(left.squirclePower, right.squirclePower) &&
			close(left.notchAmount, right.notchAmount) &&
			close(left.notchDepth, right.notchDepth) &&
			close(left.verticalStart, right.verticalStart) &&
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
		using Clock = std::chrono::steady_clock;
		constexpr auto spinWindow = std::chrono::microseconds(250);

		for (;;)
		{
			const auto now = Clock::now();
			if (now >= deadline)
			{
				return;
			}

			const auto remaining = deadline - now;
			if (remaining > spinWindow &&
				wait_with_high_resolution_timer(deadline - spinWindow))
			{
				continue;
			}
			while (Clock::now() < deadline)
			{
#if defined(_WIN32)
				YieldProcessor();
#else
				std::this_thread::yield();
#endif
			}
			return;
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
	void update_camera(const CameraInput& input);
	void resize(uint32_t width, uint32_t height);
	Model3DHandle load_model_3d(const std::filesystem::path& path);
	Media2DHandle load_media_2d(const std::filesystem::path& path, const Media2DLoadOptions& options);
	AudioClipHandle load_audio_clip(const std::filesystem::path& path);
	AudioEngine& audio();
	const AudioEngine& audio() const;
	bool load_renderer2d_font(const std::filesystem::path& path);
	bool load_renderer2d_font(const std::filesystem::path& path, const Renderer2DFontAtlasLoadOptions& options);
	bool load_localisation_directory(const std::filesystem::path& directory);
	bool load_localisation_directories(const std::vector<std::filesystem::path>& directories);
	bool set_locale(const std::string& locale);
	std::string locale() const;
	std::string resolve_text(const Text& text) const;
	RenderBackend render_backend() const;
	PresentationBackend presentation_backend() const;
	bool system_backdrop_available() const;
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
	bool rebuild_composition_presenter();
	void update_system_backdrop_regions();
	void publish_completed_composition_frames();

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
	WindowsCompositionPresenter compositionPresenter;
	std::vector<SystemBackdropRegion> appliedBackdropRegions;

	std::unordered_map<DescriptorScope, vk::DescriptorSetLayout> descriptorSetLayouts;
	std::unordered_map<PipelineType, vk::PipelineLayout> pipelineLayouts;
	std::unordered_map<DescriptorScope, vk::DescriptorPool> descriptorPools;
	std::vector<std::unordered_map<DescriptorScope, vk::DescriptorSet>> descriptorSets;
	std::vector<Frame> frames;
	std::unordered_map<PipelineType, vk::Pipeline> pipelines;
	vk::RenderPass hosted3DRenderPass = nullptr;
	vk::SampleCountFlagBits hosted3DSamples = vk::SampleCountFlagBits::e1;
	Renderer2DScene renderer2DScene;
	Renderer2DFontAtlas renderer2DFontAtlas;

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

	std::unordered_map<uint32_t, Model3DAsset> modelAssets;
	std::unordered_map<std::string, uint32_t> modelIdsByPath;
	uint32_t nextModelId = 1;
	std::unordered_map<uint32_t, Media2DAsset> mediaAssets;
	std::unordered_map<std::string, uint32_t> mediaIdsByPath;
	uint32_t nextMediaId = 1;
	AudioEngine audioEngine;
	Localisation localisation_;
	bool externalBackdropAvailable = false;
	Camera camera;
};

Engine::Engine(const EngineCreateInfo& createInfo) : impl(std::make_unique<Impl>(createInfo))
{
	register_live_engine(this);
}

Engine::~Engine()
{
	unregister_live_engine(this);
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

void Engine::update_camera(const CameraInput& input)
{
	impl->update_camera(input);
}

void Engine::resize(uint32_t framebufferWidth, uint32_t framebufferHeight)
{
	impl->resize(framebufferWidth, framebufferHeight);
}

Model3DHandle Engine::load_model_3d(const std::filesystem::path& path)
{
	return impl->load_model_3d(path);
}

Media2DHandle Engine::load_media_2d(const std::filesystem::path& path, const Media2DLoadOptions& options)
{
	return impl->load_media_2d(path, options);
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
	return impl->load_renderer2d_font(path);
}

bool Engine::load_renderer2d_font(const std::filesystem::path& path, const Renderer2DFontAtlasLoadOptions& options)
{
	return impl->load_renderer2d_font(path, options);
}

bool Engine::load_localisation_directory(const std::filesystem::path& directory)
{
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
	const bool loaded = impl->load_localisation_directories(directories);
	if (loaded)
	{
		impl->set_locale(shared_locale());
	}

	return loaded;
}

bool Engine::set_locale(const std::string& locale)
{
	return impl->set_locale(locale);
}

bool Engine::set_shared_locale(const std::string& locale)
{
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
	modelRenderExtent(choose_render_extent(createInfo.framebufferWidth, createInfo.framebufferHeight, createInfo.maxRenderPixels)),
	transparentFramebuffer(createInfo.transparentFramebuffer),
	targetFrameRate(std::min(createInfo.targetFrameRate, 1000u)),
	audioEngine(createInfo.enableAudio)
{
    logger = Logger::fetch_logger();
#if defined(_WIN32)
	compositionFrameRate = monitor_refresh_rate(nativeWindowHandle);
#endif
    if (renderBackend != RenderBackend::eVulkan)
    {
        logger->error("The requested render backend is not implemented. vibranceUI currently renders with Vulkan.");
        return;
    }
    logger->vulkan("Initialising renderer.");

    instance = make_instance(
        createInfo.applicationName,
        createInfo.instanceExtensionCount,
        createInfo.instanceExtensions,
        instanceDeletionQueue
    );
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
	physicalDevice = choose_physical_device(instance, compositionRequested);
	if (!physicalDevice && compositionRequested)
	{
		logger->warning(
			"No Vulkan GPU supports D3D11 external-memory interop; using native Vulkan presentation.");
		compositionRequested = false;
		physicalDevice = choose_physical_device(instance, false);
	}
	if (!physicalDevice)
	{
		logger->vulkan(LogLevel::eError, "No suitable physical device was found.");
		return;
	}
	hosted3DSamples = choose_hosted3d_sample_count(physicalDevice, requestedMsaaSamples);
	logger->vulkan(std::string("Hosted 3D MSAA sample count: ") + sample_count_name(hosted3DSamples) + ".");

	logicalDevice = create_logical_device(
		physicalDevice,
		surface,
		deviceDeletionQueue,
		compositionRequested);
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
	vmaCreateAllocator(&allocatorInfo, &allocator);

	swapchain.presentModePreference = createInfo.presentMode;
	swapchain.build(logicalDevice, physicalDevice, surface, framebufferWidth, framebufferHeight, transparentFramebuffer);
	if (!swapchain.chain || swapchain.images.empty())
	{
		logger->vulkan("Swapchain creation produced no drawable images.");
		return;
	}
	if (compositionRequested && compositionPresenter.initialise(
			nativeWindowHandle,
			physicalDevice,
			logicalDevice,
			swapchain.extent.width,
			swapchain.extent.height,
			static_cast<uint32_t>(swapchain.images.size()),
			graphicsQueueFamilyIndex))
	{
		activePresentationBackend = PresentationBackend::eWindowsCompositionD3D11;
	}
	else
	{
		activePresentationBackend = PresentationBackend::eNative;
	}
	renderExtent = choose_render_extent(swapchain.extent.width, swapchain.extent.height, maxRenderPixels);
	modelRenderExtent = renderExtent;

	make_descriptor_sets();

	make_pipeline_layouts();
	hosted3DRenderPass = make_hosted3d_render_pass(logicalDevice, hosted3DSamples, deviceDeletionQueue);
	if (!hosted3DRenderPass)
	{
		logger->vulkan("Failed to create hosted 3D render pass.");
		return;
	}
	
	make_pipelines();
	const std::array requiredPipelines = {
		PipelineType::eClear,
		PipelineType::eRasteriseSmall,
		PipelineType::eRasteriseBig,
		PipelineType::eWriteColour,
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

	uint32_t frameCount = static_cast<uint32_t>(swapchain.images.size());
	std::vector<vk::DescriptorType> descriptorTypes = { vk::DescriptorType::eStorageImage, vk::DescriptorType::eStorageImage };
	descriptorPools[DescriptorScope::eFrame] = make_descriptor_pool(logicalDevice, frameCount, descriptorTypes.size(), descriptorTypes.data(), deviceDeletionQueue);
	descriptorPools[DescriptorScope::eModelFrame] = make_descriptor_pool(logicalDevice, frameCount, descriptorTypes.size(), descriptorTypes.data(), deviceDeletionQueue);
	descriptorPools[DescriptorScope::eUICache] = make_descriptor_pool(logicalDevice, frameCount, descriptorTypes.size(), descriptorTypes.data(), deviceDeletionQueue);
	descriptorTypes[0] = vk::DescriptorType::eStorageBuffer;
	descriptorPools[DescriptorScope::eDrawCall] = make_descriptor_pool(logicalDevice, frameCount, 1, descriptorTypes.data(), deviceDeletionQueue);
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
		vk::DescriptorType::eStorageImage
	};
	descriptorPools[DescriptorScope::ePost] = make_descriptor_pool(logicalDevice, frameCount, descriptorTypes.size(), descriptorTypes.data(), deviceDeletionQueue);
	descriptorPools[DescriptorScope::eUICachePost] = make_descriptor_pool(logicalDevice, frameCount, descriptorTypes.size(), descriptorTypes.data(), deviceDeletionQueue);

	if (!createInfo.defaultRenderer2DFontPath.empty() &&
		!renderer2DFontAtlas.load_from_file(
			createInfo.defaultRenderer2DFontPath,
			createInfo.defaultRenderer2DFontOptions,
			allocator,
			mainCommandBuffer,
			graphicsQueue,
			logicalDevice,
			vmaDeletionQueue,
			deviceDeletionQueue))
	{
		logger->vulkan("Renderer2D font atlas is using its fallback texture.");
	}

	descriptorSets.resize(frameCount);
	frames.reserve(frameCount);
	for (uint32_t i = 0; i < frameCount; ++i) 
	{
		vk::CommandBuffer commandBuffer = allocate_command_buffer(logicalDevice, commandPool);
		constexpr int scopeCount = 6;
		DescriptorScope scopes[scopeCount] = {
			DescriptorScope::eFrame,
			DescriptorScope::eModelFrame,
			DescriptorScope::eDrawCall,
			DescriptorScope::ePost,
			DescriptorScope::eUICache,
			DescriptorScope::eUICachePost
		};
		for (int j = 0; j < scopeCount; ++j) 
		{
			DescriptorScope scope = scopes[j];
			descriptorSets[i][scope] = allocate_descriptor_set(logicalDevice,
				descriptorPools[scope], descriptorSetLayouts[scope]);
		}

		frames.push_back(Frame(swapchain, renderExtent, modelRenderExtent, logicalDevice, pipelines, hosted3DRenderPass,
			hosted3DSamples,
			commandBuffer, graphicsQueue, deviceDeletionQueue,
			descriptorSets[i], pipelineLayouts,
			allocator, nullptr, &modelAssets, &mediaAssets, renderer2DScene, renderer2DFontAtlas.image()
		));
	}
	compositionFramePending.assign(frameCount, false);
	compositionFrameBufferIndices.assign(frameCount, 0u);

	currentTime = 0.0;
	lastTime = 0.0;
	numFrames = 0;
	rendererReady = true;
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
	descriptorSetLayouts[DescriptorScope::ePost] = builder.build(deviceDeletionQueue);
	descriptorSetLayouts[DescriptorScope::eUICache] = descriptorSetLayouts[DescriptorScope::eFrame];
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
	pipelineLayouts[PipelineType::eRasteriseBig] = renderLayout;

	builder.add(descriptorSetLayouts[DescriptorScope::eFrame]);
	builder.add(descriptorSetLayouts[DescriptorScope::ePost]);
	pipelineLayouts[PipelineType::eWriteColour] = builder.build(deviceDeletionQueue);

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
	builder.add(descriptorSetLayouts[DescriptorScope::eMediaTexture]);
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
	pipelineLayouts[PipelineType::eComposite2D] = compositeLayout;

	builder.add(descriptorSetLayouts[DescriptorScope::eModel3DTexture]);
	builder.add_push_constants(
		vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
		sizeof(Model3DPushConstants));
	pipelineLayouts[PipelineType::eModel3D] = builder.build(deviceDeletionQueue);
}

void Engine::Impl::make_pipelines() 
{
	pipelines[PipelineType::eClear] = make_compute_pipeline(
		logicalDevice, "clear_screen", pipelineLayouts[PipelineType::eClear], deviceDeletionQueue
	);
	pipelines[PipelineType::eRasteriseSmall] = make_compute_pipeline(
		logicalDevice, "rasterise_small", pipelineLayouts[PipelineType::eRasteriseSmall], deviceDeletionQueue
	);
	pipelines[PipelineType::eRasteriseBig] = make_compute_pipeline(
		logicalDevice, "rasterise_big", pipelineLayouts[PipelineType::eRasteriseBig], deviceDeletionQueue
	);
	pipelines[PipelineType::eWriteColour] = make_compute_pipeline(
		logicalDevice, "write_color", pipelineLayouts[PipelineType::eWriteColour], deviceDeletionQueue
	);
	pipelines[PipelineType::eShape2D] = make_compute_pipeline(
		logicalDevice, "shape_2d", pipelineLayouts[PipelineType::eShape2D], deviceDeletionQueue
	);
	pipelines[PipelineType::eShadow2D] = make_compute_pipeline(
		logicalDevice, "shadow_2d", pipelineLayouts[PipelineType::eShadow2D], deviceDeletionQueue
	);
	pipelines[PipelineType::eBlur2D] = make_compute_pipeline(
		logicalDevice, "blur_2d", pipelineLayouts[PipelineType::eBlur2D], deviceDeletionQueue
	);
	pipelines[PipelineType::eTextMSDF] = make_compute_pipeline(
		logicalDevice, "text_msdf", pipelineLayouts[PipelineType::eTextMSDF], deviceDeletionQueue
	);
	pipelines[PipelineType::eMedia2D] = make_compute_pipeline(
		logicalDevice, "media_2d", pipelineLayouts[PipelineType::eMedia2D], deviceDeletionQueue
	);
	pipelines[PipelineType::eCompositeHosted3D] = make_compute_pipeline(
		logicalDevice, "hosted_3d_composite", pipelineLayouts[PipelineType::eCompositeHosted3D], deviceDeletionQueue
	);
	pipelines[PipelineType::eComposite2D] = make_compute_pipeline(
		logicalDevice, "composite_2d", pipelineLayouts[PipelineType::eComposite2D], deviceDeletionQueue
	);
	pipelines[PipelineType::eModel3D] = make_graphics_pipeline(
		logicalDevice, "model_3d", pipelineLayouts[PipelineType::eModel3D], hosted3DRenderPass,
		hosted3DSamples, deviceDeletionQueue
	);
}

bool Engine::Impl::rebuild_composition_presenter()
{
	compositionPresenter.shutdown(logicalDevice);
	nativeTransparencyPrimed = false;
	nextCompositionSubmitDeadline = {};
	nextCompositionPollDeadline = {};
	nextNativeSubmitDeadline = {};
	nextCompositionBufferIndex = 0u;
	hasSubmittedFrame = false;
	lastSubmittedFrameGeneration = 0u;
	compositionBufferDamage.clear();
	std::fill(compositionFramePending.begin(), compositionFramePending.end(), false);
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
	if (compositionPresenter.initialise(
			nativeWindowHandle,
			physicalDevice,
			logicalDevice,
			swapchain.extent.width,
			swapchain.extent.height,
			static_cast<uint32_t>(swapchain.images.size()),
			graphicsQueueFamilyIndex))
	{
		activePresentationBackend = PresentationBackend::eWindowsCompositionD3D11;
		compositionBufferDamage.reset(
			compositionPresenter.buffer_count(),
			glm::uvec4(0u));
		return true;
	}
#endif
	return false;
}

void Engine::Impl::update_system_backdrop_regions()
{
	if (!compositionPresenter.available())
	{
		return;
	}

	struct OrderedBackdropRegion
	{
		entt::entity entity = entt::null;
		SystemBackdropRegion region = {};
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
		if (!layer.visible || backdrop.region.material != GlassMaterial::eSystemGlass ||
			std::abs(transform.rotationRadians) > 0.0001f ||
			glm::length(transform.rotation3DRadians) > 0.0001f)
		{
			return;
		}
		const glm::vec2 framebufferSize = shape.size * glm::abs(transform.scale);
		const glm::vec2 topLeft = transform.position - transform.origin * framebufferSize;
		if (framebufferSize.x <= 0.0f || framebufferSize.y <= 0.0f)
		{
			return;
		}

		SystemBackdropRegion region = backdrop.region;
		region.x = topLeft.x * outputScaleX;
		region.y = topLeft.y * outputScaleY;
		region.width = framebufferSize.x * outputScaleX;
		region.height = framebufferSize.y * outputScaleY;
		const float shapeScale = std::max(outputScaleX, outputScaleY);
		if (region.deriveShapeFromEntity)
		{
			const glm::vec4 radii = shape.effective_corner_radii() * shapeScale;
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
			default:
				region.shape = SystemBackdropShape::eRoundedRectangle;
				break;
			}
		}
		else
		{
			region.cornerRadius *= shapeScale;
			region.topLeftRadius *= shapeScale;
			region.topRightRadius *= shapeScale;
			region.bottomRightRadius *= shapeScale;
			region.bottomLeftRadius *= shapeScale;
		}
		orderedRegions.push_back({ entity, region });
	});

	// Composition retains one effect graph per vector slot. Material/provider/
	// shape alone is not a stable identity: two equal-material entities could
	// exchange slots when EnTT storage changed, making a moving lens reuse the
	// other entity's backdrop history. Keep the entity id as the final key so a
	// dragged region always updates its own retained graph.
	std::stable_sort(
		orderedRegions.begin(),
		orderedRegions.end(),
		[](const OrderedBackdropRegion& left, const OrderedBackdropRegion& right) {
			if (left.region.material != right.region.material)
			{
				return left.region.material < right.region.material;
			}
			if (left.region.provider != right.region.provider)
			{
				return left.region.provider < right.region.provider;
			}
			if (left.region.shape != right.region.shape)
			{
				return left.region.shape < right.region.shape;
			}
			return entt::to_integral(left.entity) < entt::to_integral(right.entity);
		});
	std::vector<SystemBackdropRegion> regions;
	regions.reserve(orderedRegions.size());
	for (const OrderedBackdropRegion& entry : orderedRegions)
	{
		regions.push_back(entry.region);
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
		appliedBackdropRegions = std::move(regions);
	}
	else if (!unchanged)
	{
		Logger::fetch_logger()->warning(
			"Windows Composition rejected the requested backdrop regions.");
	}
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

		update_system_backdrop_regions();
		const auto presentResult = compositionPresenter.present(
			compositionFrameBufferIndices[pending],
			false,
			frames[pending].compositionContentRect,
			frames[pending].compositionDamageRect);
		if (presentResult == WindowsCompositionPresenter::PresentResult::eDeferred)
		{
			continue;
		}
		if (presentResult == WindowsCompositionPresenter::PresentResult::eFailed)
		{
			logger->warning(
				"Windows Composition could not present a completed Vulkan frame.");
		}
		compositionFramePending[pending] = false;
	}
}

void Engine::Impl::draw()
{
	if (!rendererReady)
	{
		return;
	}

	if (framebufferWidth == 0 || framebufferHeight == 0) 
	{
		swapchain.outdated = true;
		return;
	}

	if (targetFrameRate > 0u)
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
		modelRenderExtent = renderExtent;
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
	if (targetFrameRate == 0u)
	{
		// Keep uncapped timing observably above 2000 Hz without continuously
		// pumping Win32/GLFW work or spinning an entire CPU core.
		constexpr uint32_t maxUncappedPollRate = 2100u;
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
		if (targetFrameRate > 0u &&
			targetFrameRate <= compositionFrameRate + 2u)
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
						1.0 / static_cast<double>(compositionFrameRate)));
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
	else if (targetFrameRate == 0u)
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
						std::max(compositionFrameRate, 30u))));
		nextNativeSubmitDeadline = now + interval;
	}
	else
	{
		nextNativeSubmitDeadline = {};
	}

	const double renderTimeSeconds = std::chrono::duration<double>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
	const uint64_t sceneFrameGeneration = renderer2DScene.frame_generation();
	if (hasSubmittedFrame &&
		sceneFrameGeneration == lastSubmittedFrameGeneration &&
		!renderer2DScene.requires_continuous_redraw(renderTimeSeconds) &&
		(!compositionAvailable || nativeTransparencyPrimed))
	{
		// DWM and native swapchains retain the last presented image. When no UI
		// state or time-based content changed, another full render/copy is pure
		// duplicate work and can be omitted safely.
		return;
	}
	Frame& frame = frames[frameIndex];
    
	vk::Result fenceResult = logicalDevice.waitForFences(frame.renderFinishedFence, false, UINT64_MAX);
	if (fenceResult != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to wait for fence.");
		return;
	}

	const bool clearNativeSurface =
		compositionAvailable && !nativeTransparencyPrimed;
	const bool presentNativeSurface =
		!compositionAvailable || clearNativeSurface;
	uint32_t imageIndex = 0;
	if (presentNativeSurface)
	{
		const VkResult acquireResult = vkAcquireNextImageKHR(
			logicalDevice,
			swapchain.chain,
			UINT64_MAX,
			frame.imageAcquiredSemaphore,
			VK_NULL_HANDLE,
			&imageIndex);

		if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR ||
			acquireResult == VK_SUBOPTIMAL_KHR)
		{
			swapchain.outdated = true;
			return;
		}
		if (acquireResult == VK_ERROR_SURFACE_LOST_KHR)
		{
			recreate_surface();
			return;
		}
		if (acquireResult != VK_SUCCESS)
		{
			logger->vulkan(
				"Failed to acquire swapchain image. VkResult: " +
				std::to_string(static_cast<int>(acquireResult)));
			return;
		}
	}

	const bool useExternalBackdropUnderlay =
		externalBackdropAvailable &&
		swapchain.compositeAlpha == vk::CompositeAlphaFlagBitsKHR::eOpaque;
	uint32_t compositionBufferIndex = 0u;
	vk::Image compositionImage {};
	bool compositionImageFirstUse = true;
	glm::uvec4 pendingCompositionDamageRect { 0u };
	if (compositionPresenter.available() && compositionPresenter.buffer_count() > 0u)
	{
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
				compositionImage = compositionPresenter.image(candidate);
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
	if (compositionAvailable && submitCompositionFrame && !compositionImage)
	{
		return;
	}

	vk::Result result = logicalDevice.resetFences(frame.renderFinishedFence);
	if (result != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to reset fences.");
		return;
	}
	frame.record_command_buffer(
		imageIndex,
		camera,
		renderTimeSeconds,
		externalBackdropAvailable,
		useExternalBackdropUnderlay,
		presentNativeSurface,
		clearNativeSurface,
		compositionImage,
		compositionImageFirstUse,
		pendingCompositionDamageRect,
		graphicsQueueFamilyIndex);
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

	result = graphicsQueue.submit(submitInfo, frame.renderFinishedFence);
	if (result != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to submit buffer to graphics queue.");
		return;
	}
	hasSubmittedFrame = true;
	lastSubmittedFrameGeneration = sceneFrameGeneration;
	if (compositionImage)
	{
		compositionBufferDamage.commit(
			compositionBufferIndex,
			frame.compositionSceneDamageRect);
	}
	if (compositionImage)
	{
		compositionPresenter.mark_used(compositionBufferIndex);
		if (frameIndex < compositionFramePending.size())
		{
			compositionFramePending[frameIndex] = true;
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
		const VkResult presentResult =
			vkQueuePresentKHR(graphicsQueue, &rawPresentInfo);

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

	// Uncapped/high-rate rendering remains pipelined. Explicit rates at or below
	// the monitor cadence publish in the same frame so Vulkan content and the
	// D3D Composition regions cannot drift apart by multiple frame slots.
	const bool pipelineComposition =
		targetFrameRate == 0u ||
		targetFrameRate > compositionFrameRate + 2u;
	if (compositionImage && !pipelineComposition)
	{
		const vk::Result compositionFenceResult = logicalDevice.waitForFences(
			frame.renderFinishedFence,
			true,
			UINT64_MAX);
		if (compositionFenceResult == vk::Result::eSuccess)
		{
			update_system_backdrop_regions();
			const auto presentResult = compositionPresenter.present(
				compositionBufferIndex,
				true,
				frame.compositionContentRect,
				frame.compositionDamageRect);
			if (presentResult ==
				WindowsCompositionPresenter::PresentResult::eFailed)
			{
				logger->warning("Windows Composition could not present the shared Vulkan frame.");
			}
			if (frameIndex < compositionFramePending.size() &&
				presentResult !=
					WindowsCompositionPresenter::PresentResult::eDeferred)
			{
				compositionFramePending[frameIndex] = false;
			}
		}
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

void Engine::Impl::update_camera(const CameraInput& input)
{
	camera.velocity = glm::vec3(input.right, input.up, input.forward);
	camera.yaw += input.yawDelta;
	camera.pitch += input.pitchDelta;
	camera.update(input.deltaSeconds);
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
	modelRenderExtent = renderExtent;
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
			handle.firstTriangle = buffer.firstTriangle3D;
			handle.triangleCount = buffer.triangleCount3D > 0 ? buffer.triangleCount3D : buffer.triangleCount;
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
	handle.firstTriangle = buffer.firstTriangle3D;
	handle.triangleCount = buffer.triangleCount3D > 0 ? buffer.triangleCount3D : buffer.triangleCount;
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
	std::stringstream cacheKeyBuilder;
	cacheKeyBuilder << absolutePath.string() << "|"
		<< resource_file_cache_version(absolutePath) << "|"
		<< options.rasterWidth << "x" << options.rasterHeight << "|"
		<< (options.srgb ? "srgb" : "linear") << "|"
		<< (options.generateMipmaps ? "mips" : "nomips") << "|"
		<< (options.premultiplyAlpha ? "premul" : "straight") << "|"
		<< "maxFrames=" << options.maxAnimationFrames << "|"
		<< "svgFrames=" << options.svgAnimationFrames << "|"
		<< "svgRate=" << options.svgAnimationFrameRate << "|"
		<< "videoFrames=" << options.maxVideoFrames << "|"
		<< "videoPixels=" << options.maxVideoPixels;
	const std::string cacheKey = cacheKeyBuilder.str();
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

	refresh_localised_texts();
	auto textView = renderer2DScene.registry().view<TextComponent>(entt::exclude<LocalisedTextComponent>);
	textView.each([this](entt::entity, TextComponent& text) {
		Renderer2DTextLayout layout = renderer2DFontAtlas.layout_text(text.text, text.fontSize);
		text.atlasId = renderer2DFontAtlas.atlas_id();
		text.msdfPixelRange = renderer2DFontAtlas.pixel_range();
		text.bounds = layout.bounds;
		text.glyphs = std::move(layout.glyphs);
	});
	renderer2DScene.mark_dirty();
	return true;
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
		Renderer2DTextLayout layout = renderer2DFontAtlas.layout_text(text.text, text.fontSize);
		text.atlasId = renderer2DFontAtlas.atlas_id();
		text.msdfPixelRange = renderer2DFontAtlas.pixel_range();
		text.bounds = layout.bounds;
		text.glyphs = std::move(layout.glyphs);
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
	if (logicalDevice)
	{
		vk::Result result = graphicsQueue.waitIdle();
		if (result != vk::Result::eSuccess)
		{
			logger->vulkan("Failed to wait for queue to idle.");
		}
	}
	compositionPresenter.shutdown(logicalDevice);

    logger->vulkan("Exiting application.");

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
