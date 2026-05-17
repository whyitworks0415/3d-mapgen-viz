#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace mgv {

class VulkanContext;

// Tiny RAII-friendly wrapper. Caller owns lifetime via destroyBuffer().
struct Buffer {
    VkBuffer       buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize   size   = 0;
    void*          mapped = nullptr;          // non-null iff host-visible and mapped
};

uint32_t findMemoryType(VkPhysicalDevice device,
                        uint32_t typeFilter,
                        VkMemoryPropertyFlags props);

// Allocates, binds, and (if HOST_VISIBLE) maps the buffer. The caller must
// pair this with destroyBuffer().
Buffer createBuffer(VulkanContext& ctx,
                    VkDeviceSize size,
                    VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags props,
                    bool mapPersistently = false);

void destroyBuffer(VulkanContext& ctx, Buffer& buf);

// Reads a binary file into memory. Used for SPIR-V shader blobs.
std::vector<char> readBinaryFile(const std::string& path);

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code);

} // namespace mgv
