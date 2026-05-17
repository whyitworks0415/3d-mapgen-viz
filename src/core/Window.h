#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

struct GLFWwindow;

namespace mgv {

// Thin RAII wrapper around a GLFW window. Owns glfwInit/glfwTerminate too —
// Phase 1 has exactly one window so this is fine. If we ever need multiple
// windows, factor glfwInit out into a singleton "GlfwContext".
class Window {
public:
    Window(int width, int height, std::string title);
    ~Window();

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&)                 = delete;
    Window& operator=(Window&&)      = delete;

    bool shouldClose() const;
    void pollEvents() const;

    int  width()  const { return width_;  }
    int  height() const { return height_; }
    GLFWwindow* handle() const { return window_; }

    void getFramebufferSize(int& w, int& h) const;
    bool framebufferResized() const { return resized_; }
    void resetResizedFlag()         { resized_ = false; }

    // Helper: surface creation lives here so the renderer doesn't need GLFW.
    VkSurfaceKHR createSurface(VkInstance instance) const;

private:
    static void framebufferResizeCallback(GLFWwindow* window, int w, int h);

    GLFWwindow* window_ = nullptr;
    int         width_;
    int         height_;
    std::string title_;
    bool        resized_ = false;
};

} // namespace mgv
