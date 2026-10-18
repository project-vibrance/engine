#pragma once

#include "pipeline_cache_store.h"
#include <cstring>
#include <vibranceUI/core/logger.h>
#include <vulkan/vulkan.hpp>

namespace vibrance::vulkan
{
// A cache belongs to one VkDevice. Only verified opaque binary snapshots are
// shared between windows/devices. No VkPipeline or VkPipelineCache handle escapes
// into the process seed store. Call persist only after pipeline creation and at
// shutdown, never from the render loop.
class PipelineCache
{
public:
    PipelineCache(vk::Device device, const vk::PhysicalDeviceProperties& properties,
        uint64_t shaderFingerprint) : device_(device),
        store_(make_identity(properties, shaderFingerprint))
    {
        try
        {
            const auto seed = store_.load();
            VkPipelineCacheCreateInfo info { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
            info.initialDataSize = seed.size();
            info.pInitialData = seed.empty() ? nullptr : seed.data();
            VkPipelineCache cache = VK_NULL_HANDLE;
            std::size_t seedBytes = seed.size();
            VkResult result = vkCreatePipelineCache(device_, &info, nullptr, &cache);
            if (result != VK_SUCCESS && !seed.empty())
            {
                info.initialDataSize = 0u;
                info.pInitialData = nullptr;
                seedBytes = 0u;
                result = vkCreatePipelineCache(device_, &info, nullptr, &cache);
            }
            if (result == VK_SUCCESS) cache_ = cache;
            report("Vulkan pipeline cache: " +
                std::to_string(result == VK_SUCCESS ? seedBytes : 0u) +
                " seed bytes, " + (cache_ ? "available." : "unavailable; compiling normally."));
        }
        catch (...) { /* Caching is optional when the filesystem or allocation fails. */ }
    }

    ~PipelineCache()
    {
        persist();
        if (cache_) vkDestroyPipelineCache(device_, cache_, nullptr);
    }
    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;
    vk::PipelineCache handle() const { return cache_; }

    void persist() noexcept
    {
        if (!cache_) return;
        try
        {
            // A later window may have completed more variants. Merge its binary
            // seed through a temporary cache owned by this same device.
            merge(store_.load());
            merge(store_.load_file());
            for (unsigned attempt = 0u; attempt < 3u; ++attempt)
            {
                std::size_t maximumSize = 0u;
                if (vkGetPipelineCacheData(device_, cache_, &maximumSize, nullptr) != VK_SUCCESS ||
                    maximumSize < 32u || maximumSize > kMaximumPipelineCacheBytes) return;
                std::vector<std::byte> bytes(maximumSize);
                std::size_t actualSize = bytes.size();
                const auto result = vkGetPipelineCacheData(device_, cache_, &actualSize, bytes.data());
                // A growing driver cache may outgrow the first size query.
                // Retry a bounded number of times; never persist a partial blob.
                if (result == VK_INCOMPLETE) continue;
                if (result != VK_SUCCESS || actualSize > bytes.size()) return;
                bytes.resize(actualSize);
                if (!valid_pipeline_cache_payload(bytes, store_.identity())) return;
                const bool saved = store_.publish(bytes);
                report("Vulkan pipeline cache: retained " + std::to_string(bytes.size()) +
                    " bytes in memory" + (saved ? " and on disk." : "; disk cache unavailable."));
                return;
            }
        }
        catch (...) { /* A cache failure must not prevent rendering or shutdown. */ }
    }

private:
    static void report(const std::string& message)
    {
        if (auto* logger = Logger::fetch_logger())
        {
            const char* diagnostics = std::getenv("VIBRANCE_RENDER_DIAGNOSTICS");
            if (diagnostics && std::strcmp(diagnostics, "0") != 0)
                logger->info(message);
            else
                logger->vulkan(message);
        }
    }

    static PipelineCacheIdentity make_identity(const vk::PhysicalDeviceProperties& properties,
        uint64_t shaderFingerprint)
    {
        PipelineCacheIdentity identity;
        identity.vendor = properties.vendorID;
        identity.device = properties.deviceID;
        identity.driver = properties.driverVersion;
        std::memcpy(identity.uuid.data(), properties.pipelineCacheUUID.data(), identity.uuid.size());
        identity.shaderFingerprint = shaderFingerprint;
        return identity;
    }

    void merge(std::span<const std::byte> bytes)
    {
        if (!valid_pipeline_cache_payload(bytes, store_.identity())) return;
        VkPipelineCacheCreateInfo info { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
        info.initialDataSize = bytes.size();
        info.pInitialData = bytes.data();
        VkPipelineCache source = VK_NULL_HANDLE;
        if (vkCreatePipelineCache(device_, &info, nullptr, &source) != VK_SUCCESS) return;
        // No host pipeline creation/get/merge calls overlap on this owner. The
        // default flags also retain Vulkan's internal creation synchronisation.
        (void)vkMergePipelineCaches(device_, cache_, 1u, &source);
        vkDestroyPipelineCache(device_, source, nullptr);
    }

    vk::Device device_;
    PipelineCacheStore store_;
    vk::PipelineCache cache_ {};
};
}
