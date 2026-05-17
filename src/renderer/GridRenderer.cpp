#include "renderer/GridRenderer.h"

#include "core/Camera.h"
#include "core/Logger.h"
#include "core/VulkanCheck.h"
#include "map/MapData.h"
#include "renderer/Renderer.h"
#include "renderer/Swapchain.h"
#include "renderer/VulkanContext.h"

#include <glm/glm.hpp>

#include <array>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace mgv {

namespace {

// ---------------------------------------------------------------------------
// Unit cube data, [-0.5, 0.5]^3, 24 vertices / 36 indices, flat per-face
// normals. Triangle winding is CCW when viewed from outside (we cull back
// faces with VK_FRONT_FACE_COUNTER_CLOCKWISE + CULL_BACK).
// ---------------------------------------------------------------------------
struct VtxData { glm::vec3 pos; glm::vec3 nrm; };

constexpr VtxData kCubeVerts[24] = {
    // +X
    { { 0.5f, -0.5f, -0.5f }, {  1,  0,  0 } },
    { { 0.5f,  0.5f, -0.5f }, {  1,  0,  0 } },
    { { 0.5f,  0.5f,  0.5f }, {  1,  0,  0 } },
    { { 0.5f, -0.5f,  0.5f }, {  1,  0,  0 } },
    // -X
    { { -0.5f,  0.5f, -0.5f }, { -1,  0,  0 } },
    { { -0.5f, -0.5f, -0.5f }, { -1,  0,  0 } },
    { { -0.5f, -0.5f,  0.5f }, { -1,  0,  0 } },
    { { -0.5f,  0.5f,  0.5f }, { -1,  0,  0 } },
    // +Y
    { {  0.5f,  0.5f, -0.5f }, {  0,  1,  0 } },
    { { -0.5f,  0.5f, -0.5f }, {  0,  1,  0 } },
    { { -0.5f,  0.5f,  0.5f }, {  0,  1,  0 } },
    { {  0.5f,  0.5f,  0.5f }, {  0,  1,  0 } },
    // -Y
    { { -0.5f, -0.5f, -0.5f }, {  0, -1,  0 } },
    { {  0.5f, -0.5f, -0.5f }, {  0, -1,  0 } },
    { {  0.5f, -0.5f,  0.5f }, {  0, -1,  0 } },
    { { -0.5f, -0.5f,  0.5f }, {  0, -1,  0 } },
    // +Z (top)
    { { -0.5f, -0.5f,  0.5f }, {  0,  0,  1 } },
    { {  0.5f, -0.5f,  0.5f }, {  0,  0,  1 } },
    { {  0.5f,  0.5f,  0.5f }, {  0,  0,  1 } },
    { { -0.5f,  0.5f,  0.5f }, {  0,  0,  1 } },
    // -Z (bottom)
    { { -0.5f,  0.5f, -0.5f }, {  0,  0, -1 } },
    { {  0.5f,  0.5f, -0.5f }, {  0,  0, -1 } },
    { {  0.5f, -0.5f, -0.5f }, {  0,  0, -1 } },
    { { -0.5f, -0.5f, -0.5f }, {  0,  0, -1 } },
};

constexpr uint16_t kCubeIndices[36] = {
     0,  1,  2,    0,  2,  3,
     4,  5,  6,    4,  6,  7,
     8,  9, 10,    8, 10, 11,
    12, 13, 14,   12, 14, 15,
    16, 17, 18,   16, 18, 19,
    20, 21, 22,   20, 22, 23,
};

// Try ./shaders/<name> first (works when CWD == build dir), fall back to the
// compile-time MGV_SHADER_DIR. This lets both VS debug launch and
// command-line runs find the SPIR-V blobs.
std::string findShader(const std::string& name) {
    namespace fs = std::filesystem;
    fs::path local = fs::current_path() / "shaders" / name;
    if (fs::exists(local)) return local.string();
#ifdef MGV_SHADER_DIR
    return (fs::path(MGV_SHADER_DIR) / name).string();
#else
    return local.string();
#endif
}

} // namespace

// ---------------------------------------------------------------------------
GridRenderer::GridRenderer(VulkanContext& ctx, Renderer& renderer,
                           Swapchain& swapchain)
    : ctx_(ctx), renderer_(renderer), swapchain_(swapchain) {
    createCubeGeometry();
    createInstanceBuffer();
    createUniformBuffers();
    createDescriptorSetLayout();
    createDescriptorPool();
    createDescriptorSets();
    createPipelineLayout();
    createPipeline();
    Logger::info("GridRenderer initialized");
}

GridRenderer::~GridRenderer() {
    auto device = ctx_.device();
    if (device) vkDeviceWaitIdle(device);

    if (pipeline_)        vkDestroyPipeline      (device, pipeline_,       nullptr);
    if (pipelineLayout_)  vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    if (descPool_)        vkDestroyDescriptorPool(device, descPool_,       nullptr);
    if (descSetLayout_)   vkDestroyDescriptorSetLayout(device, descSetLayout_, nullptr);

    for (auto& b : uboBufs_) destroyBuffer(ctx_, b);
    destroyBuffer(ctx_, instanceBuf_);
    destroyBuffer(ctx_, indexBuf_);
    destroyBuffer(ctx_, vertexBuf_);
}

// ---------------------------------------------------------------------------
// Geometry & buffers
// ---------------------------------------------------------------------------
void GridRenderer::createCubeGeometry() {
    // Vertex buffer
    {
        const VkDeviceSize size = sizeof(kCubeVerts);
        vertexBuf_ = createBuffer(ctx_, size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            /*mapPersistently=*/true);
        std::memcpy(vertexBuf_.mapped, kCubeVerts, size);
    }
    // Index buffer
    {
        const VkDeviceSize size = sizeof(kCubeIndices);
        indexBuf_ = createBuffer(ctx_, size,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            /*mapPersistently=*/true);
        std::memcpy(indexBuf_.mapped, kCubeIndices, size);
        indexCount_ = static_cast<uint32_t>(sizeof(kCubeIndices) / sizeof(kCubeIndices[0]));
    }
}

void GridRenderer::createInstanceBuffer() {
    const VkDeviceSize size = sizeof(Instance) * maxInstances_;
    instanceBuf_ = createBuffer(ctx_, size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        /*mapPersistently=*/true);
}

void GridRenderer::createUniformBuffers() {
    uboBufs_.resize(Renderer::kMaxFramesInFlight);
    for (auto& b : uboBufs_) {
        b = createBuffer(ctx_, sizeof(UBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            /*mapPersistently=*/true);
    }
}

// ---------------------------------------------------------------------------
// Descriptors
// ---------------------------------------------------------------------------
void GridRenderer::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding b{};
    b.binding         = 0;
    b.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b.descriptorCount = 1;
    b.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = 1;
    ci.pBindings    = &b;
    VK_CHECK(vkCreateDescriptorSetLayout(ctx_.device(), &ci, nullptr, &descSetLayout_));
}

void GridRenderer::createDescriptorPool() {
    VkDescriptorPoolSize size{};
    size.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    size.descriptorCount = Renderer::kMaxFramesInFlight;

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.maxSets       = Renderer::kMaxFramesInFlight;
    ci.poolSizeCount = 1;
    ci.pPoolSizes    = &size;
    VK_CHECK(vkCreateDescriptorPool(ctx_.device(), &ci, nullptr, &descPool_));
}

void GridRenderer::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(Renderer::kMaxFramesInFlight, descSetLayout_);

    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = descPool_;
    ai.descriptorSetCount = Renderer::kMaxFramesInFlight;
    ai.pSetLayouts        = layouts.data();

    descSets_.resize(Renderer::kMaxFramesInFlight);
    VK_CHECK(vkAllocateDescriptorSets(ctx_.device(), &ai, descSets_.data()));

    for (uint32_t i = 0; i < Renderer::kMaxFramesInFlight; ++i) {
        VkDescriptorBufferInfo bi{};
        bi.buffer = uboBufs_[i].buffer;
        bi.offset = 0;
        bi.range  = sizeof(UBO);

        VkWriteDescriptorSet w{};
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = descSets_[i];
        w.dstBinding      = 0;
        w.dstArrayElement = 0;
        w.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.descriptorCount = 1;
        w.pBufferInfo     = &bi;
        vkUpdateDescriptorSets(ctx_.device(), 1, &w, 0, nullptr);
    }
}

void GridRenderer::createPipelineLayout() {
    VkPipelineLayoutCreateInfo ci{};
    ci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount = 1;
    ci.pSetLayouts    = &descSetLayout_;
    VK_CHECK(vkCreatePipelineLayout(ctx_.device(), &ci, nullptr, &pipelineLayout_));
}

// ---------------------------------------------------------------------------
// Pipeline
// ---------------------------------------------------------------------------
void GridRenderer::createPipeline() {
    auto vertCode = readBinaryFile(findShader("grid.vert.spv"));
    auto fragCode = readBinaryFile(findShader("grid.frag.spv"));
    VkShaderModule vert = createShaderModule(ctx_.device(), vertCode);
    VkShaderModule frag = createShaderModule(ctx_.device(), fragCode);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName  = "main";

    // Two bindings: per-vertex (binding 0) and per-instance (binding 1).
    VkVertexInputBindingDescription bindings[2]{};
    bindings[0].binding   = 0;
    bindings[0].stride    = sizeof(Vertex);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding   = 1;
    bindings[1].stride    = sizeof(Instance);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attrs[5]{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex,   position)    };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex,   normal)      };
    attrs[2] = { 2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, translation) };
    attrs[3] = { 3, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, scale)       };
    attrs[4] = { 4, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, color)       };

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 2;
    vi.pVertexBindingDescriptions      = bindings;
    vi.vertexAttributeDescriptionCount = 5;
    vi.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;     // dynamic
    vp.scissorCount  = 1;     // dynamic

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    // Disable culling for Phase 2: we Y-flip the projection (OpenGL→Vulkan
    // convention) which inverts winding; rather than tracking that here we
    // rely on depth test. Cost at our cell count is negligible.
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState cb{};
    cb.blendEnable    = VK_FALSE;
    cb.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cbs{};
    cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbs.attachmentCount = 1;
    cbs.pAttachments    = &cb;

    std::array<VkDynamicState, 2> dyn = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynState{};
    dynState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = static_cast<uint32_t>(dyn.size());
    dynState.pDynamicStates    = dyn.data();

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount          = 2;
    pci.pStages             = stages;
    pci.pVertexInputState   = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState      = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState   = &ms;
    pci.pDepthStencilState  = &ds;
    pci.pColorBlendState    = &cbs;
    pci.pDynamicState       = &dynState;
    pci.layout              = pipelineLayout_;
    pci.renderPass          = renderer_.renderPass();
    pci.subpass             = 0;

    VK_CHECK(vkCreateGraphicsPipelines(ctx_.device(), VK_NULL_HANDLE, 1, &pci,
                                       nullptr, &pipeline_));

    vkDestroyShaderModule(ctx_.device(), vert, nullptr);
    vkDestroyShaderModule(ctx_.device(), frag, nullptr);
}

// ---------------------------------------------------------------------------
// Map upload
// ---------------------------------------------------------------------------
void GridRenderer::updateFromMap(const MapData& map) {
    // Be safe — the GPU may still be reading the old instance contents.
    // Per-frame algorithms will switch to fence-based sync; for Phase 2 a
    // global idle is fine because map changes are user-triggered & rare.
    vkDeviceWaitIdle(ctx_.device());

    Instance* dst = static_cast<Instance*>(instanceBuf_.mapped);
    uint32_t  count = 0;

    map.forEachNonEmpty([&](uint32_t x, uint32_t y, uint32_t z, const Cell& c) {
        if (count >= maxInstances_) return;

        const float h = (c.height > 0.0f) ? c.height : 1.0f;
        glm::vec3 col;
        if (c.color.a > 0) {
            col = glm::vec3(c.color) / 255.0f;
        } else {
            col = defaultColorFor(c.type);
        }

        Instance& inst = dst[count++];
        inst.translation = glm::vec3(static_cast<float>(x) + 0.5f,
                                     static_cast<float>(y) + 0.5f,
                                     static_cast<float>(z) + h * 0.5f);
        inst.scale       = glm::vec3(1.0f, 1.0f, h);
        inst.color       = col;
    });

    instanceCount_ = count;
    Logger::info("GridRenderer: uploaded " + std::to_string(count) + " instances");
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------
void GridRenderer::recordDraw(VkCommandBuffer cmd,
                              const Camera& camera,
                              VkExtent2D /*viewportExtent*/,
                              uint32_t frameIndex) {
    if (!settings_.drawGrid || instanceCount_ == 0) return;

    // Update this frame's UBO.
    UBO ubo{};
    ubo.view = camera.viewMatrix();
    ubo.proj = camera.projectionMatrix();
    ubo.lightDir = glm::vec4(glm::normalize(settings_.lightDir), settings_.ambient);
    std::memcpy(uboBufs_[frameIndex].mapped, &ubo, sizeof(UBO));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                            0, 1, &descSets_[frameIndex], 0, nullptr);

    VkBuffer vbs[]    = { vertexBuf_.buffer, instanceBuf_.buffer };
    VkDeviceSize offs[] = { 0, 0 };
    vkCmdBindVertexBuffers(cmd, 0, 2, vbs, offs);
    vkCmdBindIndexBuffer  (cmd, indexBuf_.buffer, 0, VK_INDEX_TYPE_UINT16);

    vkCmdDrawIndexed(cmd, indexCount_, instanceCount_, 0, 0, 0);
}

} // namespace mgv
