#include <vibranceUI/renderer/frame.h>
#include <vibranceUI/renderer/image.h>
#include <vibranceUI/renderer/synchronisation.h>
#include <vibranceUI/core/logger.h>
#include <vibranceUI/factories/mesh_factory.h>
#include <vibranceUI/renderer/renderer2d.h>
#include <vibranceUI/renderer/renderer3d.h>
#include <array>
#include <vector>

namespace
{
	bool same_extent(vk::Extent2D a, vk::Extent2D b)
	{
		return a.width == b.width && a.height == b.height;
	}

	void delete_storage_image(StorageImage*& image)
	{
		delete image;
		image = nullptr;
	}

	void delete_color_attachment_image(ColorAttachmentImage*& image)
	{
		delete image;
		image = nullptr;
	}

	void create_frame_storage_images(Frame& frame)
	{
		// Frame images are recreated together so descriptor bindings always match extents
		frame.depthBuffer = new StorageImage(frame.allocator, vk::Format::eR32Uint, frame.renderExtent,
			frame.commandBuffer, frame.queue, frame.logicalDevice, frame.vmaDeletionQueue, frame.deviceDeletionQueue);
		frame.colorBuffer = new StorageImage(frame.allocator, vk::Format::eR8G8B8A8Unorm, frame.renderExtent,
			frame.commandBuffer, frame.queue, frame.logicalDevice, frame.vmaDeletionQueue, frame.deviceDeletionQueue,
			vk::ImageUsageFlagBits::eColorAttachment);
		frame.modelDepthBuffer = new StorageImage(frame.allocator, vk::Format::eR32Uint, frame.modelRenderExtent,
			frame.commandBuffer, frame.queue, frame.logicalDevice, frame.vmaDeletionQueue, frame.deviceDeletionQueue);
		frame.modelColorBuffer = new StorageImage(frame.allocator, vk::Format::eR8G8B8A8Unorm, frame.modelRenderExtent,
			frame.commandBuffer, frame.queue, frame.logicalDevice, frame.vmaDeletionQueue, frame.deviceDeletionQueue,
			vk::ImageUsageFlagBits::eColorAttachment);
		frame.tempSurface = new StorageImage(frame.allocator, vk::Format::eR8G8B8A8Unorm, frame.renderExtent,
			frame.commandBuffer, frame.queue, frame.logicalDevice, frame.vmaDeletionQueue, frame.deviceDeletionQueue);
		frame.compositionSurface = new StorageImage(frame.allocator, vk::Format::eR8G8B8A8Unorm, frame.renderExtent,
			frame.commandBuffer, frame.queue, frame.logicalDevice, frame.vmaDeletionQueue, frame.deviceDeletionQueue);
		frame.uiBlurSurface = new StorageImage(frame.allocator, vk::Format::eR8G8B8A8Unorm, frame.renderExtent,
			frame.commandBuffer, frame.queue, frame.logicalDevice, frame.vmaDeletionQueue, frame.deviceDeletionQueue);
		// Media's separable source blur needs an intermediate with enough colour
		// and alpha precision to avoid an extra visible 8-bit quantisation between
		// the horizontal and vertical axes.
		frame.mediaBlurSurface = new StorageImage(frame.allocator, vk::Format::eR16G16B16A16Sfloat, frame.renderExtent,
			frame.commandBuffer, frame.queue, frame.logicalDevice, frame.vmaDeletionQueue, frame.deviceDeletionQueue);
		frame.uiStaticSurface = new StorageImage(frame.allocator, vk::Format::eR8G8B8A8Unorm, frame.renderExtent,
			frame.commandBuffer, frame.queue, frame.logicalDevice, frame.vmaDeletionQueue, frame.deviceDeletionQueue,
			vk::ImageUsageFlagBits::eColorAttachment);
		frame.uiStaticBlurSurface = new StorageImage(frame.allocator, vk::Format::eR8G8B8A8Unorm, frame.renderExtent,
			frame.commandBuffer, frame.queue, frame.logicalDevice, frame.vmaDeletionQueue, frame.deviceDeletionQueue);
		frame.externalBackdropSurface = new StorageImage(frame.allocator, vk::Format::eR8G8B8A8Unorm, frame.renderExtent,
			frame.commandBuffer, frame.queue, frame.logicalDevice, frame.vmaDeletionQueue, frame.deviceDeletionQueue);
		const bool usesHosted3DResolve = frame.hosted3DSamples != vk::SampleCountFlagBits::e1;
		if (usesHosted3DResolve)
		{
			frame.hosted3DColorBuffer = new ColorAttachmentImage(frame.allocator, vk::Format::eR8G8B8A8Unorm,
				frame.modelRenderExtent, frame.hosted3DSamples, frame.logicalDevice, frame.vmaDeletionQueue,
				frame.deviceDeletionQueue);
		}
		frame.hosted3DDepthBuffer = new DepthImage(frame.allocator, vk::Format::eD32Sfloat, frame.modelRenderExtent,
			frame.hosted3DSamples, frame.logicalDevice, frame.vmaDeletionQueue, frame.deviceDeletionQueue);

		auto make_hosted3d_framebuffer = [&]() {
			std::array<vk::ImageView, 3> attachments = {
				usesHosted3DResolve ? frame.hosted3DColorBuffer->view : frame.modelColorBuffer->view,
				frame.hosted3DDepthBuffer->view,
				frame.modelColorBuffer->view
			};

			vk::FramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.renderPass = frame.hosted3DRenderPass;
			framebufferInfo.attachmentCount = usesHosted3DResolve ? 3u : 2u;
			framebufferInfo.pAttachments = attachments.data();
			framebufferInfo.width = frame.modelColorBuffer->extent.width;
			framebufferInfo.height = frame.modelColorBuffer->extent.height;
			framebufferInfo.layers = 1;

			auto result = frame.logicalDevice.createFramebuffer(framebufferInfo);
			if (result.result != vk::Result::eSuccess)
			{
				Logger::fetch_logger()->vulkan("Failed to create hosted 3D framebuffer.");
				return vk::Framebuffer {};
			}

			VkFramebuffer framebufferHandle = result.value;
			frame.deviceDeletionQueue.push_back([framebufferHandle](vk::Device device) {
				device.destroyFramebuffer(framebufferHandle);
			});
			return result.value;
		};

		frame.hosted3DFramebuffer = make_hosted3d_framebuffer();
		frame.uiCacheImagesReady = false;
	}

	void update_frame_descriptor_sets(Frame& frame)
	{
		// Descriptor scopes point shaders at the current frame surfaces and buffers
		StorageImage* fontAtlasDescriptorImage = frame.fontAtlasImage != nullptr ? frame.fontAtlasImage : frame.uiBlurSurface;
		std::vector<vk::WriteDescriptorSet> updates;
		updates.reserve(24);

		auto add_image_write = [&](DescriptorScope scope, uint32_t binding, StorageImage* image) {
			vk::WriteDescriptorSet writeOp = {};
			writeOp.dstSet = frame.descriptorSets[scope];
			writeOp.dstBinding = binding;
			writeOp.dstArrayElement = 0;
			writeOp.descriptorCount = 1;
			writeOp.descriptorType = vk::DescriptorType::eStorageImage;
			writeOp.pImageInfo = &(image->descriptor);
			updates.push_back(writeOp);
		};

		add_image_write(DescriptorScope::eFrame, 0, frame.depthBuffer);
		add_image_write(DescriptorScope::eFrame, 1, frame.colorBuffer);
		add_image_write(DescriptorScope::eModelFrame, 0, frame.modelDepthBuffer);
		add_image_write(DescriptorScope::eModelFrame, 1, frame.modelColorBuffer);
		add_image_write(DescriptorScope::ePost, 0, frame.tempSurface);
		add_image_write(DescriptorScope::ePost, 1, frame.modelColorBuffer);
		add_image_write(DescriptorScope::ePost, 2, frame.uiBlurSurface);
		add_image_write(DescriptorScope::ePost, 3, fontAtlasDescriptorImage);
		add_image_write(DescriptorScope::ePost, 4, frame.uiStaticSurface);
		add_image_write(DescriptorScope::ePost, 5, frame.uiStaticBlurSurface);
		add_image_write(DescriptorScope::ePost, 6, frame.externalBackdropSurface);
		add_image_write(DescriptorScope::ePost, 7, frame.compositionSurface);
		add_image_write(DescriptorScope::ePost, 8, frame.mediaBlurSurface);

		add_image_write(DescriptorScope::eUICache, 0, frame.depthBuffer);
		add_image_write(DescriptorScope::eUICache, 1, frame.uiStaticSurface);

		add_image_write(DescriptorScope::eUICachePost, 0, frame.tempSurface);
		add_image_write(DescriptorScope::eUICachePost, 1, frame.modelColorBuffer);
		add_image_write(DescriptorScope::eUICachePost, 2, frame.uiStaticBlurSurface);
		add_image_write(DescriptorScope::eUICachePost, 3, fontAtlasDescriptorImage);
		add_image_write(DescriptorScope::eUICachePost, 4, frame.uiStaticSurface);
		add_image_write(DescriptorScope::eUICachePost, 5, frame.uiStaticBlurSurface);
		add_image_write(DescriptorScope::eUICachePost, 6, frame.externalBackdropSurface);
		add_image_write(DescriptorScope::eUICachePost, 7, frame.compositionSurface);
		add_image_write(DescriptorScope::eUICachePost, 8, frame.mediaBlurSurface);

		frame.logicalDevice.updateDescriptorSets(static_cast<uint32_t>(updates.size()), updates.data(), 0, nullptr);
	}
}

Frame::Frame(
	Swapchain& swapchain,
	vk::Extent2D renderExtent,
	vk::Extent2D modelRenderExtent,
	vk::Device& logicalDevice,
	std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
	vk::RenderPass hosted3DRenderPass,
	vk::SampleCountFlagBits hosted3DSamples,
	vk::CommandBuffer commandBuffer,
	vk::Queue& queue,
	std::deque<std::function<void(vk::Device)>>& deletionQueue,
	std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
	std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
	VmaAllocator& allocator,
	std::unordered_map<uint32_t, Model3DAsset>* modelAssets,
	std::unordered_map<uint32_t, Media2DAsset>* mediaAssets,
	Renderer2DScene& scene2D,
	StorageImage* fontAtlasImage) :
	logicalDevice(logicalDevice),
	swapchain(swapchain), renderExtent(renderExtent), modelRenderExtent(modelRenderExtent), pipelines(pipelines),
	hosted3DRenderPass(hosted3DRenderPass),
	hosted3DSamples(hosted3DSamples),
	descriptorSets(descriptorSets), 
	pipelineLayouts(pipelineLayouts),
	allocator(allocator), fontAtlasImage(fontAtlasImage), modelAssets(modelAssets),
	mediaAssets(mediaAssets), scene2D(scene2D), queue(queue)
{   
	this->commandBuffer = commandBuffer;

	imageAcquiredSemaphore = make_semaphore(logicalDevice, deletionQueue);
	renderFinishedSemaphore = make_semaphore(logicalDevice, deletionQueue);
	renderFinishedFence = make_fence(logicalDevice, deletionQueue);
	create_frame_storage_images(*this);
	update_frame_descriptor_sets(*this);
}

void Frame::record_command_buffer(
	uint32_t imageIndex,
	double currentTimeSeconds,
	bool externalBackdropAvailable,
	bool useExternalBackdropUnderlay,
	bool presentNativeSurface,
	bool clearNativeSurface,
	vk::Image compositionImage,
	vk::Buffer compositionReadbackBuffer,
	bool compositionImageFirstUse,
	glm::uvec4 pendingCompositionDamageRect,
	uint32_t graphicsQueueFamilyIndex)
{
	// Record all passes for one swapchain image, including UI cache and final composite
	Logger* logger = Logger::fetch_logger();
	Swapchain renderTarget;
	renderTarget.extent = renderExtent;

	vk::Result result = commandBuffer.reset();
	if (result != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to reset command buffer.");
		return;
	}

	vk::CommandBufferBeginInfo beginInfo = {};
	result = commandBuffer.begin(beginInfo);
	if (result != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to begin command buffer.");
		return;
	}

	auto transition_render_target = [&](StorageImage* image, vk::AccessFlags dstAccessMask) {
		transition_image_layout(commandBuffer, image->image,
			vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
			vk::AccessFlagBits::eNone, dstAccessMask,
			vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader
		);
	};

	auto barrier_render_target = [&](StorageImage* image, vk::AccessFlags dstAccessMask) {
		transition_image_layout(commandBuffer, image->image,
			vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral,
			vk::AccessFlagBits::eShaderWrite, dstAccessMask,
			vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader
		);
	};

	auto clear_render_target = [&](StorageImage* image, const vk::ClearColorValue& value) {
		transition_image_layout(commandBuffer, image->image,
			vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
			vk::AccessFlagBits::eNone, vk::AccessFlagBits::eTransferWrite,
			vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer
		);
		vk::ImageSubresourceRange range {};
		range.aspectMask = vk::ImageAspectFlagBits::eColor;
		range.levelCount = 1u;
		range.layerCount = 1u;
		commandBuffer.clearColorImage(
			image->image,
			vk::ImageLayout::eTransferDstOptimal,
			value,
			range);
		transition_image_layout(commandBuffer, image->image,
			vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral,
			vk::AccessFlagBits::eTransferWrite,
			vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eComputeShader
		);
	};

	auto prepare_cache_target = [&](StorageImage* image) {
		if (!uiCacheImagesReady)
		{
			transition_render_target(image, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
			return;
		}

		transition_image_layout(commandBuffer, image->image,
			vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral,
			vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
			vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
			vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader
		);
	};

	const bool renderHosted3D =
		!scene2D.registry().view<Model3DComponent>().empty();
	const vk::ClearColorValue transparent(
		std::array<float, 4> { 0.0f, 0.0f, 0.0f, 0.0f });
	const vk::ClearColorValue farDepth(
		std::array<std::uint32_t, 4> { 0x3f800000u, 0u, 0u, 0u });
	// The 2D renderer retains this per-frame-slot image and clears the previous
	// dispatch envelope itself. Preserve its contents here so a small moving
	// island does not force a full-screen transfer clear on every frame.
	prepare_cache_target(colorBuffer);
	if (renderHosted3D)
	{
		clear_render_target(modelDepthBuffer, farDepth);
		clear_render_target(modelColorBuffer, transparent);
	}
	const bool writeNativeContent = presentNativeSurface && !clearNativeSurface;
	if (writeNativeContent)
	{
		transition_render_target(tempSurface, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
	}
	const bool writeCompositionSurface =
		static_cast<bool>(compositionImage) ||
		static_cast<bool>(compositionReadbackBuffer);
	if (writeCompositionSurface)
	{
		transition_render_target(compositionSurface, vk::AccessFlagBits::eShaderWrite);
	}
	transition_render_target(uiBlurSurface, vk::AccessFlagBits::eMemoryWrite);
	transition_render_target(mediaBlurSurface, vk::AccessFlagBits::eMemoryWrite);
	prepare_cache_target(uiStaticSurface);
	prepare_cache_target(uiStaticBlurSurface);
	uiCacheImagesReady = true;

	barrier_render_target(uiBlurSurface, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
	barrier_render_target(mediaBlurSurface, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);

	renderer2D.record(
		commandBuffer,
		renderTarget,
		pipelines,
		descriptorSets,
		pipelineLayouts,
		scene2D,
		currentTimeSeconds,
		&renderer3D,
		modelAssets,
		mediaAssets,
		colorBuffer,
		uiStaticSurface,
		modelColorBuffer,
		hosted3DRenderPass,
		hosted3DFramebuffer,
		hosted3DSamples != vk::SampleCountFlagBits::e1,
		externalBackdropAvailable);
	glm::uvec4 contentRect = renderer2D.content_bounds();
	glm::uvec4 damageRect = renderer2D.damage_bounds();
	if (useExternalBackdropUnderlay)
	{
		contentRect = { 0u, 0u, renderExtent.width, renderExtent.height };
		damageRect = contentRect;
	}
	compositionSceneDamageRect = damageRect;
	auto union_rect = [](glm::uvec4 left, glm::uvec4 right) {
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
		return glm::uvec4 { x, y, rightEdge - x, bottomEdge - y };
	};
	// Each imported texture retains pixels between uses. Apply the current
	// entity damage plus every change this particular texture missed while it
	// was owned by D3D. First use starts from a transparent clear and receives
	// the complete live content once.
	glm::uvec4 compositionUpdateRect = union_rect(
		damageRect,
		pendingCompositionDamageRect);
	if (compositionReadbackBuffer)
	{
		// The portable Composition fallback reads a complete packed image back to
		// the host. Rebuild every pixel so transparent space is deterministic even
		// though compositionSurface itself is transient for this command buffer.
		compositionUpdateRect = {
			0u, 0u, renderExtent.width, renderExtent.height };
	}
	if (compositionImageFirstUse)
	{
		compositionUpdateRect = union_rect(contentRect, compositionUpdateRect);
	}
	compositionContentRect = { 0u, 0u, 0u, 0u };
	compositionDamageRect = { 0u, 0u, 0u, 0u };
	if (writeCompositionSurface &&
		renderExtent.width > 0u && renderExtent.height > 0u)
	{
		auto scale_floor = [](uint32_t value, uint32_t destination, uint32_t source) {
			return static_cast<uint32_t>(
				(static_cast<uint64_t>(value) * destination) / source);
		};
		auto scale_ceil = [](uint32_t value, uint32_t destination, uint32_t source) {
			return static_cast<uint32_t>(
				(static_cast<uint64_t>(value) * destination + source - 1u) / source);
		};
		auto map_rect = [&](glm::uvec4 rect) {
			if (rect.z == 0u || rect.w == 0u)
			{
				return glm::uvec4(0u);
			}
			const uint32_t right = std::min(rect.x + rect.z, renderExtent.width);
			const uint32_t bottom = std::min(rect.y + rect.w, renderExtent.height);
			const uint32_t destinationX = scale_floor(
				std::min(rect.x, renderExtent.width),
				swapchain.extent.width,
				renderExtent.width);
			const uint32_t destinationY = scale_floor(
				std::min(rect.y, renderExtent.height),
				swapchain.extent.height,
				renderExtent.height);
			const uint32_t destinationRight = scale_ceil(
				right,
				swapchain.extent.width,
				renderExtent.width);
			const uint32_t destinationBottom = scale_ceil(
				bottom,
				swapchain.extent.height,
				renderExtent.height);
			return glm::uvec4 {
				destinationX,
				destinationY,
				destinationRight - destinationX,
				destinationBottom - destinationY
			};
		};
		compositionContentRect = map_rect(contentRect);
		compositionDamageRect = map_rect(compositionUpdateRect);
	}

	barrier_render_target(colorBuffer, vk::AccessFlagBits::eShaderRead);
	if (renderHosted3D)
	{
		barrier_render_target(modelColorBuffer, vk::AccessFlagBits::eShaderRead);
	}
	barrier_render_target(uiBlurSurface, vk::AccessFlagBits::eShaderRead);
	barrier_render_target(uiStaticSurface, vk::AccessFlagBits::eShaderRead);
	barrier_render_target(uiStaticBlurSurface, vk::AccessFlagBits::eShaderRead);
	if (writeNativeContent)
	{
		barrier_render_target(tempSurface, vk::AccessFlagBits::eShaderWrite);
	}

	if (writeNativeContent || writeCompositionSurface)
	{
		renderer2D.record_composite(
			commandBuffer,
			renderTarget,
			pipelines,
			descriptorSets,
			pipelineLayouts,
			useExternalBackdropUnderlay,
			writeNativeContent,
			writeCompositionSurface,
			writeCompositionSurface ? compositionUpdateRect : contentRect);
	}

	if (presentNativeSurface)
	{
		transition_image_layout(commandBuffer, swapchain.images[imageIndex],
			vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
			vk::AccessFlagBits::eNone, vk::AccessFlagBits::eTransferWrite,
			vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer
		);

		if (clearNativeSurface)
		{
			const vk::ClearColorValue transparent(
				std::array<float, 4> { 0.0f, 0.0f, 0.0f, 0.0f });
			vk::ImageSubresourceRange range {};
			range.aspectMask = vk::ImageAspectFlagBits::eColor;
			range.levelCount = 1u;
			range.layerCount = 1u;
			commandBuffer.clearColorImage(
				swapchain.images[imageIndex],
				vk::ImageLayout::eTransferDstOptimal,
				transparent,
				range);
		}
		else
		{
			transition_image_layout(commandBuffer, tempSurface->image,
				vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal,
				vk::AccessFlagBits::eMemoryWrite, vk::AccessFlagBits::eTransferRead,
				vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer
			);
			copy_image_to_image(
				commandBuffer,
				tempSurface->image,
				swapchain.images[imageIndex],
				tempSurface->extent,
				swapchain.extent);
		}

		transition_image_layout(commandBuffer, swapchain.images[imageIndex],
			vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::ePresentSrcKHR,
			vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eMemoryRead,
			vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe
		);
	}

	if (compositionReadbackBuffer)
	{
		transition_image_layout(commandBuffer, compositionSurface->image,
			vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal,
			vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead,
			vk::PipelineStageFlagBits::eComputeShader,
			vk::PipelineStageFlagBits::eTransfer);

		vk::BufferImageCopy copyRegion = {};
		copyRegion.bufferOffset = 0u;
		copyRegion.bufferRowLength = 0u;
		copyRegion.bufferImageHeight = 0u;
		copyRegion.imageSubresource.aspectMask =
			vk::ImageAspectFlagBits::eColor;
		copyRegion.imageSubresource.layerCount = 1u;
		copyRegion.imageExtent = vk::Extent3D{
			renderExtent.width, renderExtent.height, 1u };
		commandBuffer.copyImageToBuffer(
			compositionSurface->image,
			vk::ImageLayout::eTransferSrcOptimal,
			compositionReadbackBuffer,
			copyRegion);

		vk::BufferMemoryBarrier hostReadBarrier = {};
		hostReadBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		hostReadBarrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
		hostReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		hostReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		hostReadBarrier.buffer = compositionReadbackBuffer;
		hostReadBarrier.offset = 0u;
		hostReadBarrier.size = VK_WHOLE_SIZE;
		commandBuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eHost,
			{},
			nullptr,
			hostReadBarrier,
			nullptr);
	}
	else if (compositionImage)
	{
		transition_image_layout(commandBuffer, compositionSurface->image,
			vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal,
			vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead,
			vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer
		);

		vk::ImageMemoryBarrier acquireBarrier = {};
		// These images are created by D3D11 and imported into Vulkan. Their
		// externally-owned layout is GENERAL even before Vulkan's first use;
		// treating first use as UNDEFINED permits an implementation to discard
		// the D3D allocation and its alpha plane. Qualcomm's ARM64 ICD exercises
		// that permission, producing opaque black in untouched pixels.
		acquireBarrier.oldLayout = vk::ImageLayout::eGeneral;
		acquireBarrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
		acquireBarrier.srcAccessMask = {};
		acquireBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		acquireBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL_KHR;
		acquireBarrier.dstQueueFamilyIndex = graphicsQueueFamilyIndex;
		acquireBarrier.image = compositionImage;
		acquireBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		acquireBarrier.subresourceRange.levelCount = 1u;
		acquireBarrier.subresourceRange.layerCount = 1u;
		commandBuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe,
			vk::PipelineStageFlagBits::eTransfer,
			{},
			nullptr,
			nullptr,
			acquireBarrier);

		// Initialise a shared texture once. Subsequent uses retain unchanged UI
		// pixels and receive only the accumulated entity damage for that buffer.
		vk::ImageSubresourceRange compositionRange {};
		compositionRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		compositionRange.levelCount = 1u;
		compositionRange.layerCount = 1u;
		if (compositionImageFirstUse)
		{
			commandBuffer.clearColorImage(
				compositionImage,
				vk::ImageLayout::eTransferDstOptimal,
				transparent,
				compositionRange);
		}
		if (compositionUpdateRect.z > 0u && compositionUpdateRect.w > 0u)
		{
			if (compositionImageFirstUse)
			{
				vk::ImageMemoryBarrier clearBarrier = {};
				clearBarrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
				clearBarrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
				clearBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
				clearBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
				clearBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				clearBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				clearBarrier.image = compositionImage;
				clearBarrier.subresourceRange = compositionRange;
				commandBuffer.pipelineBarrier(
					vk::PipelineStageFlagBits::eTransfer,
					vk::PipelineStageFlagBits::eTransfer,
					{},
					nullptr,
					nullptr,
					clearBarrier);
			}

			copy_image_region_to_image(
				commandBuffer,
				compositionSurface->image,
				compositionImage,
				compositionSurface->extent,
				swapchain.extent,
				vk::Rect2D {
					vk::Offset2D {
						static_cast<int32_t>(compositionUpdateRect.x),
						static_cast<int32_t>(compositionUpdateRect.y) },
					vk::Extent2D {
						compositionUpdateRect.z,
						compositionUpdateRect.w }
				});
		}

		vk::ImageMemoryBarrier releaseBarrier = {};
		releaseBarrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
		releaseBarrier.newLayout = vk::ImageLayout::eGeneral;
		releaseBarrier.srcAccessMask =
			compositionImageFirstUse ||
			(compositionUpdateRect.z > 0u && compositionUpdateRect.w > 0u) ?
			vk::AccessFlagBits::eTransferWrite : vk::AccessFlags {};
		releaseBarrier.dstAccessMask = {};
		releaseBarrier.srcQueueFamilyIndex = graphicsQueueFamilyIndex;
		releaseBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL_KHR;
		releaseBarrier.image = compositionImage;
		releaseBarrier.subresourceRange = acquireBarrier.subresourceRange;
		commandBuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eBottomOfPipe,
			{},
			nullptr,
			nullptr,
			releaseBarrier);
	}

	result = commandBuffer.end();
	if (result != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to end command buffer.");
		return;
	}
}

void Frame::resize_resources(vk::Extent2D newRenderExtent, vk::Extent2D newModelRenderExtent)
{
	if (colorBuffer != nullptr &&
		modelColorBuffer != nullptr &&
		same_extent(colorBuffer->extent, newRenderExtent) &&
		same_extent(modelColorBuffer->extent, newModelRenderExtent))
	{
		return;
	}

	free_resources();
	renderExtent = newRenderExtent;
	modelRenderExtent = newModelRenderExtent;
	create_frame_storage_images(*this);
	update_frame_descriptor_sets(*this);
}

void Frame::set_font_atlas_image(StorageImage* image)
{
	fontAtlasImage = image;
	update_frame_descriptor_sets(*this);
}

void Frame::free_resources()
{
	Logger* logger = Logger::fetch_logger();
	
	if (queue.waitIdle() != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to wait for queue to idle.");
		return;
	}

	while (deviceDeletionQueue.size() > 0)
	{
		deviceDeletionQueue.back()(logicalDevice);
		deviceDeletionQueue.pop_back();
	}

	while (vmaDeletionQueue.size() > 0)
	{
		vmaDeletionQueue.back()(allocator);
		vmaDeletionQueue.pop_back();
	}

	delete_storage_image(depthBuffer);
	delete_storage_image(colorBuffer);
	delete_storage_image(modelDepthBuffer);
	delete_storage_image(modelColorBuffer);
	delete_storage_image(tempSurface);
	delete_storage_image(compositionSurface);
	delete_storage_image(uiBlurSurface);
	delete_storage_image(mediaBlurSurface);
	delete_storage_image(uiStaticSurface);
	delete_storage_image(uiStaticBlurSurface);
	delete_storage_image(externalBackdropSurface);
	delete_color_attachment_image(hosted3DColorBuffer);
	delete hosted3DDepthBuffer;
	hosted3DDepthBuffer = nullptr;
	hosted3DFramebuffer = nullptr;
	renderer2D.invalidate_dynamic_surface();
	uiCacheImagesReady = false;
}
