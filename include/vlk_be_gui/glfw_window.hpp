#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace vlk::platform {

class GlfwWindow {
public:
    GlfwWindow() = default;
    GlfwWindow(const GlfwWindow&) = delete;
    GlfwWindow& operator=(const GlfwWindow&) = delete;
    GlfwWindow(GlfwWindow&& other) noexcept;
    GlfwWindow& operator=(GlfwWindow&& other) noexcept;
    ~GlfwWindow();

    [[nodiscard]] static GlfwWindow create(int width, int height, std::string_view title);

    void poll_events() const;
    void wait_events() const;
    [[nodiscard]] bool should_close() const;
    [[nodiscard]] bool was_resized();
    [[nodiscard]] VkExtent2D framebuffer_extent() const;

    [[nodiscard]] std::vector<const char*> required_instance_extensions() const;
    [[nodiscard]] VkSurfaceKHR create_surface(VkInstance instance) const;
    void notify_resized() noexcept;

private:
    explicit GlfwWindow(GLFWwindow* window) noexcept;

    GLFWwindow* window_ = nullptr;
    bool resized_ = false;
};

} // namespace vlk::platform
