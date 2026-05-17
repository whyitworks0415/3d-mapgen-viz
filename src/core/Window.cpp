#include "core/Window.h"
#include "core/VulkanCheck.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdexcept>

namespace mgv {

Window::Window(int width, int height, std::string title)
    : width_(width), height_(height), title_(std::move(title)) {
    if (!glfwInit()) {
        throw std::runtime_error("glfwInit failed");
    }
    if (!glfwVulkanSupported()) {
        glfwTerminate();
        throw std::runtime_error("GLFW reports Vulkan is not supported on this system");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);

    window_ = glfwCreateWindow(width_, height_, title_.c_str(), nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("glfwCreateWindow failed");
    }

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebufferResizeCallback);
}

Window::~Window() {
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(window_) != 0;
}

void Window::pollEvents() const {
    glfwPollEvents();
}

void Window::getFramebufferSize(int& w, int& h) const {
    glfwGetFramebufferSize(window_, &w, &h);
}

VkSurfaceKHR Window::createSurface(VkInstance instance) const {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VK_CHECK(glfwCreateWindowSurface(instance, window_, nullptr, &surface));
    return surface;
}

void Window::framebufferResizeCallback(GLFWwindow* window, int w, int h) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (!self) return;
    self->width_   = w;
    self->height_  = h;
    self->resized_ = true;
}

} // namespace mgv
