#include "renderer/VulkanUtils.h"

#include "core/VulkanCheck.h"
#include "renderer/VulkanContext.h"

#include <fstream>
#include <stdexcept>

namespace mgv {

uint32_t findMemoryType(VkPhysicalDevice device,
                        uint32_t typeFilter,
                        VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(device, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        const bool typeOk = (typeFilter & (1u << i)) != 0;
        const bool propsOk =
            (memProps.memoryTypes[i].propertyFlags & props) == props;
        if (typeOk && propsOk) return i;
    }
    throw std::runtime_error("No matching Vulkan memory type");
}

Buffer createBuffer(VulkanContext& ctx,
                    VkDeviceSize size,
                    VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags props,
                    bool mapPersistently) {
    Buffer out{};
    out.size = size;

    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = size;
    bi.usage       = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(ctx.device(), &bi, nullptr, &out.buffer));

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(ctx.device(), out.buffer, &req);

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(ctx.physicalDevice(),
                                        req.memoryTypeBits, props);
    VK_CHECK(vkAllocateMemory(ctx.device(), &ai, nullptr, &out.memory));
    VK_CHECK(vkBindBufferMemory(ctx.device(), out.buffer, out.memory, 0));

    if (mapPersistently && (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
        VK_CHECK(vkMapMemory(ctx.device(), out.memory, 0, size, 0, &out.mapped));
    }
    return out;
}

void destroyBuffer(VulkanContext& ctx, Buffer& buf) {
    if (buf.mapped) {
        vkUnmapMemory(ctx.device(), buf.memory);
        buf.mapped = nullptr;
    }
    if (buf.buffer) vkDestroyBuffer(ctx.device(), buf.buffer, nullptr);
    if (buf.memory) vkFreeMemory   (ctx.device(), buf.memory, nullptr);
    buf = Buffer{};
}

std::vector<char> readBinaryFile(const std::string& path) {
    std::ifstream f(path, std::ios::ate | std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open file: " + path);
    const auto size = static_cast<size_t>(f.tellg());
    std::vector<char> buf(size);
    f.seekg(0);
    f.read(buf.data(), static_cast<std::streamsize>(size));
    return buf;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule m = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device, &ci, nullptr, &m));
    return m;
}

} // namespace mgv
