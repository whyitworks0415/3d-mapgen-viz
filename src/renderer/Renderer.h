#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace mgv {

class VulkanContext;
class Swapchain;
class Window;

// Owns: render pass, command pool, per-frame command buffers, per-frame sync
// objects. Drives the acquire/record/submit/present loop. Phase 1 just clears
// the screen; the record callback lets higher layers (ImGui, GridRenderer)
// inject draw calls inside the render pass.
class Renderer {
public:
    static constexpr uint32_t kMaxFramesInFlight = 2;

    using RecordCallback = std::function<void(VkCommandBuffer cmd, uint32_t imageIndex)>;

    Renderer(VulkanContext& context, Swapchain& swapchain, Window& window);
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Returns false if the swapchain was recreated and the frame skipped.
    bool drawFrame(const RecordCallback& record);

    VkRenderPass renderPass()         const { return renderPass_; }
    uint32_t     currentFrameIndex()  const { return currentFrame_; }

    std::array<float, 4>& clearColor() { return clearColor_; }

private:
    void createRenderPass();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void destroySyncObjects();

    VulkanContext& context_;
    Swapchain&     swapchain_;
    Window&        window_;

    VkRenderPass                 renderPass_   = VK_NULL_HANDLE;
    VkCommandPool                commandPool_  = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    std::vector<VkSemaphore> imageAvailable_;
    std::vector<VkSemaphore> renderFinished_;
    std::vector<VkFence>     inFlight_;

    uint32_t currentFrame_ = 0;

    std::array<float, 4> clearColor_{ 0.10f, 0.12f, 0.16f, 1.0f };
};

} // namespace mgv
