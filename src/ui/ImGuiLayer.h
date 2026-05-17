#pragma once

#include <vulkan/vulkan.h>

namespace mgv {

class Window;
class VulkanContext;
class Renderer;
class Swapchain;

// Wraps the Dear ImGui + GLFW + Vulkan backends. Lifecycle:
//   ctor: descriptor pool, ImGui context, backend init, font atlas
//   per frame: beginFrame() -> user draws UI -> endFrame() -> recordDrawData(cmd)
//   dtor: tear everything down in reverse order
//
// Stores explicit flags so a partial init failure still unwinds cleanly.
class ImGuiLayer {
public:
    ImGuiLayer(Window& window, VulkanContext& context,
               Renderer& renderer, Swapchain& swapchain);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&)            = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void beginFrame();
    void endFrame();
    void recordDrawData(VkCommandBuffer cmd);

private:
    void createDescriptorPool();

    Window&        window_;
    VulkanContext& context_;
    Renderer&      renderer_;
    Swapchain&     swapchain_;

    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

    bool contextCreated_     = false;
    bool glfwBackendInit_    = false;
    bool vulkanBackendInit_  = false;
};

} // namespace mgv
