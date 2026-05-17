#include "renderer/Swapchain.h"

#include "core/Logger.h"
#include "core/VulkanCheck.h"
#include "core/Window.h"
#include "renderer/VulkanContext.h"
#include "renderer/VulkanUtils.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <limits>

namespace mgv {

Swapchain::Swapchain(VulkanContext& context, Window& window)
    : context_(context), window_(window) {
    create();
}

Swapchain::~Swapchain() {
    destroy();
}

void Swapchain::destroy() {
    auto device = context_.device();
    for (auto fb : framebuffers_) vkDestroyFramebuffer(device, fb, nullptr);
    framebuffers_.clear();
    destroyDepthAttachments();
    for (auto v : imageViews_)    vkDestroyImageView(device, v, nullptr);
    imageViews_.clear();
    if (swapchain_) {
        vkDestroySwapchainKHR(device, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

void Swapchain::destroyDepthAttachments() {
    auto device = context_.device();
    for (auto& d : depths_) {
        if (d.view)   vkDestroyImageView(device, d.view, nullptr);
        if (d.image)  vkDestroyImage    (device, d.image, nullptr);
        if (d.memory) vkFreeMemory      (device, d.memory, nullptr);
    }
    depths_.clear();
}

void Swapchain::createDepthAttachments() {
    auto device       = context_.device();
    auto depthFormat  = context_.depthFormat();

    depths_.resize(images_.size());
    for (auto& d : depths_) {
        VkImageCreateInfo ii{};
        ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType     = VK_IMAGE_TYPE_2D;
        ii.format        = depthFormat;
        ii.extent        = { extent_.width, extent_.height, 1 };
        ii.mipLevels     = 1;
        ii.arrayLayers   = 1;
        ii.samples       = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ii.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VK_CHECK(vkCreateImage(device, &ii, nullptr, &d.image));

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device, d.image, &req);

        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = findMemoryType(context_.physicalDevice(),
                                            req.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK(vkAllocateMemory(device, &ai, nullptr, &d.memory));
        VK_CHECK(vkBindImageMemory(device, d.image, d.memory, 0));

        VkImageViewCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image                           = d.image;
        vi.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        vi.format                          = depthFormat;
        vi.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
        vi.subresourceRange.baseMipLevel   = 0;
        vi.subresourceRange.levelCount     = 1;
        vi.subresourceRange.baseArrayLayer = 0;
        vi.subresourceRange.layerCount     = 1;
        VK_CHECK(vkCreateImageView(device, &vi, nullptr, &d.view));
    }
}

void Swapchain::recreate(VkRenderPass renderPass) {
    // If minimised, wait until the window has a non-zero size.
    int w = 0, h = 0;
    window_.getFramebufferSize(w, h);
    while (w == 0 || h == 0) {
        window_.getFramebufferSize(w, h);
        glfwWaitEvents();
    }

    context_.waitIdle();
    destroy();
    create();
    createFramebuffers(renderPass);
}

void Swapchain::create() {
    auto support       = context_.querySwapchainSupport();
    auto surfaceFormat = chooseFormat(support.formats);
    auto presentMode   = choosePresentMode(support.presentModes);
    auto chosenExtent  = chooseExtent(support.capabilities);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 &&
        imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = context_.surface();
    ci.minImageCount    = imageCount;
    ci.imageFormat      = surfaceFormat.format;
    ci.imageColorSpace  = surfaceFormat.colorSpace;
    ci.imageExtent      = chosenExtent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    const auto& q = context_.queueFamilies();
    uint32_t indices[2] = { *q.graphics, *q.present };
    if (*q.graphics != *q.present) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = indices;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    ci.preTransform   = support.capabilities.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode    = presentMode;
    ci.clipped        = VK_TRUE;
    ci.oldSwapchain   = VK_NULL_HANDLE;

    VK_CHECK(vkCreateSwapchainKHR(context_.device(), &ci, nullptr, &swapchain_));

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(context_.device(), swapchain_, &count, nullptr);
    images_.resize(count);
    vkGetSwapchainImagesKHR(context_.device(), swapchain_, &count, images_.data());

    imageFormat_ = surfaceFormat.format;
    extent_      = chosenExtent;

    imageViews_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); ++i) {
        VkImageViewCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image                           = images_[i];
        vi.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        vi.format                          = imageFormat_;
        vi.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        vi.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        vi.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        vi.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        vi.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.baseMipLevel   = 0;
        vi.subresourceRange.levelCount     = 1;
        vi.subresourceRange.baseArrayLayer = 0;
        vi.subresourceRange.layerCount     = 1;
        VK_CHECK(vkCreateImageView(context_.device(), &vi, nullptr, &imageViews_[i]));
    }

    createDepthAttachments();

    Logger::info("Swapchain created (" +
                 std::to_string(extent_.width) + "x" +
                 std::to_string(extent_.height) + ", " +
                 std::to_string(images_.size()) + " images)");
}

void Swapchain::createFramebuffers(VkRenderPass renderPass) {
    auto device = context_.device();

    // Both color and depth attachments are referenced by index in the render
    // pass: 0 = color, 1 = depth.
    framebuffers_.resize(imageViews_.size());
    for (size_t i = 0; i < imageViews_.size(); ++i) {
        VkImageView attachments[2] = { imageViews_[i], depths_[i].view };
        VkFramebufferCreateInfo fb{};
        fb.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass      = renderPass;
        fb.attachmentCount = 2;
        fb.pAttachments    = attachments;
        fb.width           = extent_.width;
        fb.height          = extent_.height;
        fb.layers          = 1;
        VK_CHECK(vkCreateFramebuffer(device, &fb, nullptr, &framebuffers_[i]));
    }
}

VkSurfaceFormatKHR Swapchain::chooseFormat(
        const std::vector<VkSurfaceFormatKHR>& formats) const {
    for (const auto& f : formats) {
        if (f.format     == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return formats.front();
}

VkPresentModeKHR Swapchain::choosePresentMode(
        const std::vector<VkPresentModeKHR>& modes) const {
    for (auto m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    }
    return VK_PRESENT_MODE_FIFO_KHR;  // guaranteed available
}

VkExtent2D Swapchain::chooseExtent(const VkSurfaceCapabilitiesKHR& caps) const {
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return caps.currentExtent;
    }
    int w = 0, h = 0;
    window_.getFramebufferSize(w, h);
    VkExtent2D actual{ static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
    actual.width  = std::clamp(actual.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    actual.height = std::clamp(actual.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return actual;
}

} // namespace mgv
