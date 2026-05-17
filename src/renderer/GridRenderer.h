#pragma once

#include "renderer/VulkanUtils.h"

#include <glm/glm.hpp>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace mgv {

class VulkanContext;
class Renderer;
class Swapchain;
class Camera;
class MapData;

// Draws every non-Empty cell as an instanced cube. One vertex/index buffer
// for the unit cube, one large host-visible instance buffer that's rewritten
// when the map changes, and one UBO per frame-in-flight for view/proj/light.
class GridRenderer {
public:
    GridRenderer(VulkanContext& ctx, Renderer& renderer, Swapchain& swapchain);
    ~GridRenderer();

    GridRenderer(const GridRenderer&)            = delete;
    GridRenderer& operator=(const GridRenderer&) = delete;

    // Rebuilds the instance buffer from the current map state. Safe to call
    // multiple times. Phase 2 uses vkDeviceWaitIdle internally — fine because
    // map regeneration is rare; algorithms will move this to fence-based sync.
    void updateFromMap(const MapData& map);

    // Records draws into a render-pass-active command buffer.
    void recordDraw(VkCommandBuffer cmd,
                    const Camera& camera,
                    VkExtent2D viewportExtent,
                    uint32_t frameIndex);

    uint32_t instanceCount() const { return instanceCount_; }

    // Visualization toggles exposed to the UI.
    struct Settings {
        glm::vec3  lightDir { -0.4f, -0.5f, -0.8f };
        float      ambient  = 0.25f;
        bool       drawGrid = true;
    };
    Settings& settings() { return settings_; }

private:
    struct Vertex   { glm::vec3 position; glm::vec3 normal; };
    struct Instance { glm::vec3 translation; glm::vec3 scale; glm::vec3 color; };
    struct UBO      { glm::mat4 view; glm::mat4 proj; glm::vec4 lightDir; };

    void createCubeGeometry();
    void createInstanceBuffer();
    void createUniformBuffers();
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSets();
    void createPipelineLayout();
    void createPipeline();

    VulkanContext& ctx_;
    Renderer&      renderer_;
    Swapchain&     swapchain_;

    Buffer   vertexBuf_{};
    Buffer   indexBuf_{};
    uint32_t indexCount_ = 0;

    Buffer   instanceBuf_{};
    uint32_t maxInstances_  = 1u << 18;          // 262 144 cells max
    uint32_t instanceCount_ = 0;

    std::vector<Buffer> uboBufs_;                 // size = kMaxFramesInFlight

    VkDescriptorSetLayout descSetLayout_  = VK_NULL_HANDLE;
    VkDescriptorPool      descPool_       = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descSets_;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       pipeline_      = VK_NULL_HANDLE;

    Settings settings_;
};

} // namespace mgv
