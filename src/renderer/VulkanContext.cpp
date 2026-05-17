#include "renderer/VulkanContext.h"

#include "core/Logger.h"
#include "core/VulkanCheck.h"
#include "core/Window.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace mgv {

// ---------------------------------------------------------------------------
// Debug messenger plumbing
// ---------------------------------------------------------------------------
namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT /*type*/,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* /*userData*/) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        Logger::error(std::string("Vk: ") + data->pMessage);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        Logger::warn(std::string("Vk: ") + data->pMessage);
    } else {
        Logger::debug(std::string("Vk: ") + data->pMessage);
    }
    return VK_FALSE;
}

VkResult createDebugMessengerEXT(VkInstance instance,
                                 const VkDebugUtilsMessengerCreateInfoEXT* ci,
                                 const VkAllocationCallbacks* alloc,
                                 VkDebugUtilsMessengerEXT* outMessenger) {
    auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (!fn) return VK_ERROR_EXTENSION_NOT_PRESENT;
    return fn(instance, ci, alloc, outMessenger);
}

void destroyDebugMessengerEXT(VkInstance instance,
                              VkDebugUtilsMessengerEXT messenger,
                              const VkAllocationCallbacks* alloc) {
    auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (fn) fn(instance, messenger, alloc);
}

void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& ci) {
    ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT     |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT  |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = vulkanDebugCallback;
}

} // namespace

// ---------------------------------------------------------------------------
// VulkanContext
// ---------------------------------------------------------------------------
VulkanContext::VulkanContext(Window& window, const char* appName) {
#if defined(MGV_DEBUG)
    enableValidation_ = true;
#endif
    createInstance(appName);
    setupDebugMessenger();
    createSurface(window);
    pickPhysicalDevice();
    selectDepthFormat();
    createLogicalDevice();
    Logger::info("VulkanContext initialized");
}

void VulkanContext::selectDepthFormat() {
    // Prefer 32-bit float depth; fall back to common 24-bit packed depth.
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };
    for (VkFormat fmt : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, fmt, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depthFormat_ = fmt;
            Logger::info(std::string("Depth format: ") + std::to_string(static_cast<int>(fmt)));
            return;
        }
    }
    throw std::runtime_error("No supported depth format on this device");
}

VulkanContext::~VulkanContext() {
    if (device_)         vkDestroyDevice(device_, nullptr);
    if (surface_)        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (debugMessenger_) destroyDebugMessengerEXT(instance_, debugMessenger_, nullptr);
    if (instance_)       vkDestroyInstance(instance_, nullptr);
}

void VulkanContext::createInstance(const char* appName) {
    if (enableValidation_ && !checkValidationLayerSupport()) {
        Logger::warn("Validation layers requested but not available — disabling");
        enableValidation_ = false;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = appName;
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName        = "MapGenViz";
    appInfo.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_2;

    auto extensions = getRequiredInstanceExtensions();

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &appInfo;
    ci.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();

    // Chain a debug messenger create-info into the instance create-info so we
    // catch validation errors from vkCreateInstance / vkDestroyInstance.
    VkDebugUtilsMessengerCreateInfoEXT dbgCi{};
    if (enableValidation_) {
        ci.enabledLayerCount   = 1;
        ci.ppEnabledLayerNames = &kValidationLayer;
        populateDebugMessengerCreateInfo(dbgCi);
        ci.pNext = &dbgCi;
    }

    VK_CHECK(vkCreateInstance(&ci, nullptr, &instance_));
}

void VulkanContext::setupDebugMessenger() {
    if (!enableValidation_) return;
    VkDebugUtilsMessengerCreateInfoEXT ci{};
    populateDebugMessengerCreateInfo(ci);
    VK_CHECK(createDebugMessengerEXT(instance_, &ci, nullptr, &debugMessenger_));
}

void VulkanContext::createSurface(Window& window) {
    surface_ = window.createSurface(instance_);
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr));
    if (deviceCount == 0) {
        throw std::runtime_error("No Vulkan physical devices found");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    VK_CHECK(vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()));

    std::multimap<int, VkPhysicalDevice, std::greater<int>> candidates;
    for (auto d : devices) candidates.emplace(rateDeviceSuitability(d), d);

    if (candidates.empty() || candidates.begin()->first <= 0) {
        throw std::runtime_error("No suitable Vulkan device (need graphics + present + swapchain)");
    }

    physicalDevice_ = candidates.begin()->second;
    queueIndices_   = findQueueFamilies(physicalDevice_);

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    Logger::info(std::string("Selected GPU: ") + props.deviceName);
}

int VulkanContext::rateDeviceSuitability(VkPhysicalDevice device) const {
    auto indices = findQueueFamilies(device);
    if (!indices.isComplete()) return 0;
    if (!deviceExtensionsSupported(device)) return 0;

    auto swap = querySwapchainSupport(device);
    if (swap.formats.empty() || swap.presentModes.empty()) return 0;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device, &props);

    int score = 1;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   score += 1000;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 100;
    score += static_cast<int>(props.limits.maxImageDimension2D / 1024);
    return score;
}

bool VulkanContext::deviceExtensionsSupported(VkPhysicalDevice device) const {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());

    std::set<std::string> required(deviceExtensions_.begin(), deviceExtensions_.end());
    for (const auto& ext : available) required.erase(ext.extensionName);
    return required.empty();
}

QueueFamilyIndices VulkanContext::findQueueFamilies(VkPhysicalDevice device) const {
    QueueFamilyIndices indices;

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics = i;
        }
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
        if (presentSupport) {
            indices.present = i;
        }
        if (indices.isComplete()) break;
    }
    return indices;
}

SwapchainSupportDetails VulkanContext::querySwapchainSupport(VkPhysicalDevice device) const {
    SwapchainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr);
    if (formatCount > 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, details.formats.data());
    }

    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &modeCount, nullptr);
    if (modeCount > 0) {
        details.presentModes.resize(modeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &modeCount, details.presentModes.data());
    }
    return details;
}

SwapchainSupportDetails VulkanContext::querySwapchainSupport() const {
    return querySwapchainSupport(physicalDevice_);
}

void VulkanContext::createLogicalDevice() {
    std::set<uint32_t> uniqueFamilies = { *queueIndices_.graphics, *queueIndices_.present };
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    float queuePriority = 1.0f;
    for (uint32_t fam : uniqueFamilies) {
        VkDeviceQueueCreateInfo qi{};
        qi.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = fam;
        qi.queueCount       = 1;
        qi.pQueuePriorities = &queuePriority;
        queueInfos.push_back(qi);
    }

    // Phase 1 needs no special features. Enable them as we add pipelines.
    VkPhysicalDeviceFeatures features{};

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = static_cast<uint32_t>(queueInfos.size());
    ci.pQueueCreateInfos       = queueInfos.data();
    ci.pEnabledFeatures        = &features;
    ci.enabledExtensionCount   = static_cast<uint32_t>(deviceExtensions_.size());
    ci.ppEnabledExtensionNames = deviceExtensions_.data();
    if (enableValidation_) {
        ci.enabledLayerCount   = 1;
        ci.ppEnabledLayerNames = &kValidationLayer;
    }

    VK_CHECK(vkCreateDevice(physicalDevice_, &ci, nullptr, &device_));
    vkGetDeviceQueue(device_, *queueIndices_.graphics, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, *queueIndices_.present,  0, &presentQueue_);
}

bool VulkanContext::checkValidationLayerSupport() const {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> available(count);
    vkEnumerateInstanceLayerProperties(&count, available.data());

    return std::any_of(available.begin(), available.end(),
                       [&](const VkLayerProperties& p) {
                           return std::strcmp(p.layerName, kValidationLayer) == 0;
                       });
}

std::vector<const char*> VulkanContext::getRequiredInstanceExtensions() const {
    uint32_t count = 0;
    const char** glfwExt = glfwGetRequiredInstanceExtensions(&count);
    std::vector<const char*> extensions(glfwExt, glfwExt + count);
    if (enableValidation_) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    return extensions;
}

void VulkanContext::waitIdle() const {
    if (device_) vkDeviceWaitIdle(device_);
}

} // namespace mgv
