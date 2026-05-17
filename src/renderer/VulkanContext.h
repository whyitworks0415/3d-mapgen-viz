#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace mgv {

class Window;

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    bool isComplete() const { return graphics.has_value() && present.has_value(); }
};

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR        capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   presentModes;
};

// Owns the bottom of the Vulkan stack: instance, debug messenger, surface,
// physical device, logical device, queues. Created once at startup; lives
// until shutdown. Everything else (swapchain, render pass, pipelines) borrows
// references to it.
class VulkanContext {
public:
    VulkanContext(Window& window, const char* appName);
    ~VulkanContext();

    VulkanContext(const VulkanContext&)            = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkInstance       instance()        const { return instance_;       }
    VkSurfaceKHR     surface()         const { return surface_;        }
    VkPhysicalDevice physicalDevice()  const { return physicalDevice_; }
    VkDevice         device()          const { return device_;         }
    VkQueue          graphicsQueue()   const { return graphicsQueue_;  }
    VkQueue          presentQueue()    const { return presentQueue_;   }
    const QueueFamilyIndices& queueFamilies() const { return queueIndices_; }

    VkFormat depthFormat() const { return depthFormat_; }

    SwapchainSupportDetails querySwapchainSupport() const;
    void                    waitIdle() const;

    bool validationEnabled() const { return enableValidation_; }

private:
    void createInstance(const char* appName);
    void setupDebugMessenger();
    void createSurface(Window& window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void selectDepthFormat();

    bool checkValidationLayerSupport() const;
    std::vector<const char*> getRequiredInstanceExtensions() const;
    int  rateDeviceSuitability(VkPhysicalDevice device) const;
    bool deviceExtensionsSupported(VkPhysicalDevice device) const;
    QueueFamilyIndices       findQueueFamilies(VkPhysicalDevice device) const;
    SwapchainSupportDetails  querySwapchainSupport(VkPhysicalDevice device) const;

    bool enableValidation_ = false;

    VkInstance               instance_        = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_  = VK_NULL_HANDLE;
    VkSurfaceKHR             surface_         = VK_NULL_HANDLE;
    VkPhysicalDevice         physicalDevice_  = VK_NULL_HANDLE;
    VkDevice                 device_          = VK_NULL_HANDLE;
    VkQueue                  graphicsQueue_   = VK_NULL_HANDLE;
    VkQueue                  presentQueue_    = VK_NULL_HANDLE;
    QueueFamilyIndices       queueIndices_{};
    VkFormat                 depthFormat_     = VK_FORMAT_UNDEFINED;

    static constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
    std::vector<const char*> deviceExtensions_ = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
};

} // namespace mgv
