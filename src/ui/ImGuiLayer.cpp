#include "ui/ImGuiLayer.h"

#include "core/Logger.h"
#include "core/VulkanCheck.h"
#include "core/Window.h"
#include "renderer/Renderer.h"
#include "renderer/Swapchain.h"
#include "renderer/VulkanContext.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <stdexcept>

namespace mgv {

namespace {
void imguiVkCheck(VkResult err) {
    if (err != VK_SUCCESS) {
        Logger::error(std::string("ImGui Vulkan: ") + vkResultToString(err));
    }
}
} // namespace

ImGuiLayer::ImGuiLayer(Window& window, VulkanContext& context,
                       Renderer& renderer, Swapchain& swapchain)
    : window_(window), context_(context),
      renderer_(renderer), swapchain_(swapchain) {

    createDescriptorPool();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    contextCreated_ = true;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForVulkan(window_.handle(), true)) {
        throw std::runtime_error("ImGui_ImplGlfw_InitForVulkan failed");
    }
    glfwBackendInit_ = true;

    ImGui_ImplVulkan_InitInfo init{};
    init.Instance       = context_.instance();
    init.PhysicalDevice = context_.physicalDevice();
    init.Device         = context_.device();
    init.QueueFamily    = *context_.queueFamilies().graphics;
    init.Queue          = context_.graphicsQueue();
    init.DescriptorPool = descriptorPool_;
    init.RenderPass     = renderer_.renderPass();
    init.Subpass        = 0;
    init.MinImageCount  = Renderer::kMaxFramesInFlight;
    init.ImageCount     = swapchain_.imageCount();
    init.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;
    init.Allocator      = nullptr;
    init.CheckVkResultFn = imguiVkCheck;

    if (!ImGui_ImplVulkan_Init(&init)) {
        throw std::runtime_error("ImGui_ImplVulkan_Init failed");
    }
    vulkanBackendInit_ = true;

    if (!ImGui_ImplVulkan_CreateFontsTexture()) {
        throw std::runtime_error("ImGui_ImplVulkan_CreateFontsTexture failed");
    }

    Logger::info("ImGuiLayer initialized");
}

ImGuiLayer::~ImGuiLayer() {
    auto device = context_.device();
    if (vulkanBackendInit_) {
        if (device) vkDeviceWaitIdle(device);
        ImGui_ImplVulkan_Shutdown();
    }
    if (glfwBackendInit_)  ImGui_ImplGlfw_Shutdown();
    if (contextCreated_)   ImGui::DestroyContext();
    if (descriptorPool_ && device) {
        vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
    }
}

void ImGuiLayer::createDescriptorPool() {
    // Conservative pool sizing — ImGui only uses CombinedImageSampler in
    // practice, but the larger buckets keep us future-proof for textured
    // panels (e.g. visualization textures).
    VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER,                1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1000 }
    };

    const uint32_t kPoolSizeCount = static_cast<uint32_t>(sizeof(sizes) / sizeof(sizes[0]));

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    ci.maxSets       = 1000 * kPoolSizeCount;
    ci.poolSizeCount = kPoolSizeCount;
    ci.pPoolSizes    = sizes;

    VK_CHECK(vkCreateDescriptorPool(context_.device(), &ci, nullptr, &descriptorPool_));
}

void ImGuiLayer::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::endFrame() {
    ImGui::Render();
}

void ImGuiLayer::recordDrawData(VkCommandBuffer cmd) {
    ImDrawData* draw = ImGui::GetDrawData();
    if (draw && draw->TotalVtxCount > 0) {
        ImGui_ImplVulkan_RenderDrawData(draw, cmd);
    } else if (draw) {
        // Still emit the (empty) call so ImGui state is flushed correctly.
        ImGui_ImplVulkan_RenderDrawData(draw, cmd);
    }
}

} // namespace mgv
