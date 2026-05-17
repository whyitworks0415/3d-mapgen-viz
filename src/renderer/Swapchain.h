#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace mgv {

class Window;
class VulkanContext;

// Owns the swapchain, its image views, and the framebuffers tied to a render
// pass. Framebuffer creation is deferred (createFramebuffers) because the
// renderer creates the render pass after the swapchain knows its image format.
class Swapchain {
public:
    Swapchain(VulkanContext& context, Window& window);
    ~Swapchain();

    Swapchain(const Swapchain&)            = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    // Called once after the renderer constructs its render pass. Also called
    // again from recreate() when the surface size changes.
    void createFramebuffers(VkRenderPass renderPass);

    // Wait for non-zero framebuffer size, then tear down & rebuild swapchain
    // and framebuffers. Caller must own the render pass.
    void recreate(VkRenderPass renderPass);

    VkSwapchainKHR handle()      const { return swapchain_;   }
    VkFormat       imageFormat() const { return imageFormat_; }
    VkExtent2D     extent()      const { return extent_;      }
    uint32_t       imageCount()  const { return static_cast<uint32_t>(images_.size()); }
    const std::vector<VkFramebuffer>& framebuffers() const { return framebuffers_; }

private:
    void create();
    void destroy();

    VkSurfaceFormatKHR chooseFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    VkPresentModeKHR   choosePresentMode(const std::vector<VkPresentModeKHR>& modes) const;
    VkExtent2D         chooseExtent(const VkSurfaceCapabilitiesKHR& caps) const;

    VulkanContext& context_;
    Window&        window_;

    struct DepthAttachment {
        VkImage        image  = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView    view   = VK_NULL_HANDLE;
    };

    void createDepthAttachments();
    void destroyDepthAttachments();

    VkSwapchainKHR               swapchain_   = VK_NULL_HANDLE;
    VkFormat                     imageFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D                   extent_{};
    std::vector<VkImage>         images_;
    std::vector<VkImageView>     imageViews_;
    std::vector<DepthAttachment> depths_;
    std::vector<VkFramebuffer>   framebuffers_;
};

} // namespace mgv
