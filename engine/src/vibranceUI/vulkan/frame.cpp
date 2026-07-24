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
		updates.reserve(23);

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

		auto add_buffer_write = [&](DescriptorScope scope, uint32_t binding, StorageBuffer* buffer) {
			vk::WriteDescriptorSet writeOp = {};
			writeOp.dstSet = frame.descriptorSets[scope];
			writeOp.dstBinding = binding;
			writeOp.dstArrayElement = 0;
			writeOp.descriptorCount = 1;
			writeOp.descriptorType = vk::DescriptorType::eStorageBuffer;
			writeOp.pBufferInfo = &(buffer->descriptor);
			updates.push_back(writeOp);
		};

		add_image_write(DescriptorScope::eFrame, 0, frame.depthBuffer);
		add_image_write(DescriptorScope::eFrame, 1, frame.colorBuffer);
		add_image_write(DescriptorScope::eModelFrame, 0, frame.modelDepthBuffer);
		add_image_write(DescriptorScope::eModelFrame, 1, frame.modelColorBuffer);
		if (frame.vertexBuffer != nullptr)
		{
			add_buffer_write(DescriptorScope::eDrawCall, 0, frame.vertexBuffer);
		}

		add_image_write(DescriptorScope::ePost, 0, frame.tempSurface);
		add_image_write(DescriptorScope::ePost, 1, frame.modelColorBuffer);
		add_image_write(DescriptorScope::ePost, 2, frame.uiBlurSurface);
		add_image_write(DescriptorScope::ePost, 3, fontAtlasDescriptorImage);
		add_image_write(DescriptorScope::ePost, 4, frame.uiStaticSurface);
		add_image_write(DescriptorScope::ePost, 5, frame.uiStaticBlurSurface);
		add_image_write(DescriptorScope::ePost, 6, frame.externalBackdropSurface);
		add_image_write(DescriptorScope::ePost, 7, frame.compositionSurface);

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
	StorageBuffer* vertexBuffer,
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
	allocator(allocator), fontAtlasImage(fontAtlasImage), vertexBuffer(vertexBuffer), modelAssets(modelAssets),
	mediaAssets(mediaAssets), scene2D(scene2D), queue(queue)
{   
	this->commandBuffer = commandBuffer;

	imageAcquiredSemaphore = make_semaphore(logicalDevice, deletionQueue);
	renderFinishedSemaphore = make_semaphore(logicalDevice, deletionQueue);
	renderFinishedFence = make_fence(logicalDevice, deletionQueue);
	triangleCount = vertexBuffer != nullptr ? vertexBuffer->triangleCount : 0;
	triangleCount2D = vertexBuffer != nullptr ? vertexBuffer->triangleCount2D : 0;
	firstTriangle3D = vertexBuffer != nullptr ? vertexBuffer->firstTriangle3D : 0;
	triangleCount3D = vertexBuffer != nullptr ? vertexBuffer->triangleCount3D : 0;

	create_frame_storage_images(*this);
	update_frame_descriptor_sets(*this);
}

void Frame::record_command_buffer(
	uint32_t imageIndex,
	const Camera& camera,
	double currentTimeSeconds,
	bool externalBackdropAvailable,
	bool useExternalBackdropUnderlay,
	vk::Image compositionImage,
	bool compositionImageFirstUse,
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

	transition_image_layout(commandBuffer, depthBuffer->image,
		vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
		vk::AccessFlagBits::eNone, vk::AccessFlagBits::eMemoryWrite,
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader
	);

	transition_image_layout(commandBuffer, colorBuffer->image,
		vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
		vk::AccessFlagBits::eNone, vk::AccessFlagBits::eMemoryWrite,
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader
	);
	transition_render_target(modelDepthBuffer, vk::AccessFlagBits::eMemoryWrite);
	transition_render_target(modelColorBuffer, vk::AccessFlagBits::eMemoryWrite);
	transition_render_target(tempSurface, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
	transition_render_target(compositionSurface, vk::AccessFlagBits::eShaderWrite);
	transition_render_target(uiBlurSurface, vk::AccessFlagBits::eMemoryWrite);
	prepare_cache_target(uiStaticSurface);
	prepare_cache_target(uiStaticBlurSurface);
	uiCacheImagesReady = true;

	PipelineType pipelineType = PipelineType::eClear;
	commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines[pipelineType]);
	commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayouts[pipelineType],
		0, 1, &descriptorSets[DescriptorScope::eFrame], 0, nullptr);
	uint32_t workgroupCountX = (renderExtent.width + 7) / 8;
	uint32_t workgroupCountY = (renderExtent.height + 7) / 8;
	commandBuffer.dispatch(workgroupCountX, workgroupCountY, 1);
	commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayouts[pipelineType],
		0, 1, &descriptorSets[DescriptorScope::eModelFrame], 0, nullptr);
	uint32_t modelWorkgroupCountX = (modelRenderExtent.width + 7) / 8;
	uint32_t modelWorkgroupCountY = (modelRenderExtent.height + 7) / 8;
	commandBuffer.dispatch(modelWorkgroupCountX, modelWorkgroupCountY, 1);

	barrier_render_target(depthBuffer, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
	barrier_render_target(colorBuffer, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
	barrier_render_target(modelDepthBuffer, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
	barrier_render_target(modelColorBuffer, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
	barrier_render_target(uiBlurSurface, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);

	renderer2D.record(
		commandBuffer,
		renderTarget,
		pipelines,
		descriptorSets,
		pipelineLayouts,
		0,
		triangleCount2D,
		scene2D,
		currentTimeSeconds,
		&renderer3D,
		&camera,
		firstTriangle3D,
		triangleCount3D,
		vertexBuffer,
		modelAssets,
		mediaAssets,
		colorBuffer,
		uiStaticSurface,
		modelColorBuffer,
		hosted3DRenderPass,
		hosted3DFramebuffer,
		hosted3DSamples != vk::SampleCountFlagBits::e1,
		externalBackdropAvailable);

	barrier_render_target(colorBuffer, vk::AccessFlagBits::eShaderRead);
	barrier_render_target(modelColorBuffer, vk::AccessFlagBits::eShaderRead);
	barrier_render_target(uiBlurSurface, vk::AccessFlagBits::eShaderRead);
	barrier_render_target(uiStaticSurface, vk::AccessFlagBits::eShaderRead);
	barrier_render_target(uiStaticBlurSurface, vk::AccessFlagBits::eShaderRead);
	barrier_render_target(tempSurface, vk::AccessFlagBits::eShaderWrite);
	
	renderer2D.record_composite(
		commandBuffer,
		renderTarget,
		pipelines,
		descriptorSets,
		pipelineLayouts,
		useExternalBackdropUnderlay);

	transition_image_layout(commandBuffer, tempSurface->image,
		vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal,
		vk::AccessFlagBits::eMemoryWrite, vk::AccessFlagBits::eTransferRead,
		vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer
	);

	transition_image_layout(commandBuffer, swapchain.images[imageIndex],
		vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
		vk::AccessFlagBits::eNone, vk::AccessFlagBits::eTransferWrite,
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer
	);

	copy_image_to_image(commandBuffer, tempSurface->image, swapchain.images[imageIndex], tempSurface->extent, swapchain.extent);

	transition_image_layout(commandBuffer, swapchain.images[imageIndex],
		vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::ePresentSrcKHR,
		vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eMemoryRead,
		vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe
	);

	if (compositionImage)
	{
		transition_image_layout(commandBuffer, compositionSurface->image,
			vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal,
			vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead,
			vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer
		);

		vk::ImageMemoryBarrier acquireBarrier = {};
		acquireBarrier.oldLayout = compositionImageFirstUse ?
			vk::ImageLayout::eUndefined : vk::ImageLayout::eGeneral;
		acquireBarrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
		acquireBarrier.srcAccessMask = {};
		acquireBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		acquireBarrier.srcQueueFamilyIndex = compositionImageFirstUse ?
			VK_QUEUE_FAMILY_IGNORED : VK_QUEUE_FAMILY_EXTERNAL_KHR;
		acquireBarrier.dstQueueFamilyIndex = compositionImageFirstUse ?
			VK_QUEUE_FAMILY_IGNORED : graphicsQueueFamilyIndex;
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

		copy_image_to_image(
			commandBuffer,
			compositionSurface->image,
			compositionImage,
			compositionSurface->extent,
			swapchain.extent);

		vk::ImageMemoryBarrier releaseBarrier = {};
		releaseBarrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
		releaseBarrier.newLayout = vk::ImageLayout::eGeneral;
		releaseBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
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
	delete_storage_image(uiStaticSurface);
	delete_storage_image(uiStaticBlurSurface);
	delete_storage_image(externalBackdropSurface);
	delete_color_attachment_image(hosted3DColorBuffer);
	delete hosted3DDepthBuffer;
	hosted3DDepthBuffer = nullptr;
	hosted3DFramebuffer = nullptr;
	uiCacheImagesReady = false;
}
