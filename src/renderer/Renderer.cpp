#include "renderer/Renderer.h"

#include "core/Logger.h"
#include "core/VulkanCheck.h"
#include "core/Window.h"
#include "renderer/Swapchain.h"
#include "renderer/VulkanContext.h"

#include <stdexcept>

namespace mgv {

Renderer::Renderer(VulkanContext& context, Swapchain& swapchain, Window& window)
    : context_(context), swapchain_(swapchain), window_(window) {
    createRenderPass();
    swapchain_.createFramebuffers(renderPass_);
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
    Logger::info("Renderer initialized");
}

Renderer::~Renderer() {
    auto device = context_.device();
    if (device) vkDeviceWaitIdle(device);

    destroySyncObjects();
    if (commandPool_) vkDestroyCommandPool(device, commandPool_, nullptr);
    if (renderPass_)  vkDestroyRenderPass(device, renderPass_, nullptr);
}

void Renderer::createRenderPass() {
    VkAttachmentDescription attachments[2]{};

    // Color
    attachments[0].format         = swapchain_.imageFormat();
    attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Depth
    attachments[1].format         = context_.depthFormat();
    attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 2;
    ci.pAttachments    = attachments;
    ci.subpassCount    = 1;
    ci.pSubpasses      = &subpass;
    ci.dependencyCount = 1;
    ci.pDependencies   = &dep;

    VK_CHECK(vkCreateRenderPass(context_.device(), &ci, nullptr, &renderPass_));
}

void Renderer::createCommandPool() {
    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = *context_.queueFamilies().graphics;
    VK_CHECK(vkCreateCommandPool(context_.device(), &ci, nullptr, &commandPool_));
}

void Renderer::createCommandBuffers() {
    commandBuffers_.resize(kMaxFramesInFlight);

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = commandPool_;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = kMaxFramesInFlight;
    VK_CHECK(vkAllocateCommandBuffers(context_.device(), &ai, commandBuffers_.data()));
}

void Renderer::createSyncObjects() {
    imageAvailable_.resize(kMaxFramesInFlight);
    renderFinished_.resize(kMaxFramesInFlight);
    inFlight_.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        VK_CHECK(vkCreateSemaphore(context_.device(), &sci, nullptr, &imageAvailable_[i]));
        VK_CHECK(vkCreateSemaphore(context_.device(), &sci, nullptr, &renderFinished_[i]));
        VK_CHECK(vkCreateFence    (context_.device(), &fci, nullptr, &inFlight_[i]));
    }
}

void Renderer::destroySyncObjects() {
    auto device = context_.device();
    for (auto s : imageAvailable_) vkDestroySemaphore(device, s, nullptr);
    for (auto s : renderFinished_) vkDestroySemaphore(device, s, nullptr);
    for (auto f : inFlight_)       vkDestroyFence(device, f, nullptr);
    imageAvailable_.clear();
    renderFinished_.clear();
    inFlight_.clear();
}

bool Renderer::drawFrame(const RecordCallback& record) {
    auto device = context_.device();

    vkWaitForFences(device, 1, &inFlight_[currentFrame_], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult acquire = vkAcquireNextImageKHR(
        device, swapchain_.handle(), UINT64_MAX,
        imageAvailable_[currentFrame_], VK_NULL_HANDLE, &imageIndex);

    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_.recreate(renderPass_);
        return false;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("vkAcquireNextImageKHR failed");
    }

    // Only reset the fence once we know we'll submit work to it.
    vkResetFences(device, 1, &inFlight_[currentFrame_]);

    VkCommandBuffer cmd = commandBuffers_[currentFrame_];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin));

    VkClearValue clears[2]{};
    clears[0].color        = { { clearColor_[0], clearColor_[1], clearColor_[2], clearColor_[3] } };
    clears[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass        = renderPass_;
    rpBegin.framebuffer       = swapchain_.framebuffers()[imageIndex];
    rpBegin.renderArea.offset = { 0, 0 };
    rpBegin.renderArea.extent = swapchain_.extent();
    rpBegin.clearValueCount   = 2;
    rpBegin.pClearValues      = clears;
    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    // Set dynamic viewport/scissor — we'll require them once pipelines exist;
    // for now this is harmless and keeps the command buffer well-formed.
    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(swapchain_.extent().width);
    viewport.height   = static_cast<float>(swapchain_.extent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swapchain_.extent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    if (record) record(cmd, imageIndex);

    vkCmdEndRenderPass(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &imageAvailable_[currentFrame_];
    submit.pWaitDstStageMask    = waitStages;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &renderFinished_[currentFrame_];
    VK_CHECK(vkQueueSubmit(context_.graphicsQueue(), 1, &submit, inFlight_[currentFrame_]));

    VkSwapchainKHR swapchains[] = { swapchain_.handle() };
    VkPresentInfoKHR present{};
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &renderFinished_[currentFrame_];
    present.swapchainCount     = 1;
    present.pSwapchains        = swapchains;
    present.pImageIndices      = &imageIndex;

    VkResult presentResult = vkQueuePresentKHR(context_.presentQueue(), &present);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR ||
        window_.framebufferResized()) {
        window_.resetResizedFlag();
        swapchain_.recreate(renderPass_);
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("vkQueuePresentKHR failed");
    }

    currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;
    return true;
}

} // namespace mgv
