#include "vlk_be_gui/glfw_window.hpp"

#include <stdexcept>
#include <string>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace vlk::platform {

namespace {

class GlfwLifetime {
public:
    GlfwLifetime()
    {
        if (glfwInit() != GLFW_TRUE) {
            throw std::runtime_error("failed to initialize GLFW");
        }
    }

    ~GlfwLifetime()
    {
        glfwTerminate();
    }
};

[[nodiscard]] GlfwLifetime& glfw_lifetime()
{
    static GlfwLifetime lifetime;
    return lifetime;
}

void framebuffer_size_callback(GLFWwindow* window, int, int)
{
    auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
    if (self != nullptr) {
        self->notify_resized();
    }
}

} // namespace

GlfwWindow::GlfwWindow(GLFWwindow* window) noexcept
    : window_(window)
{
}

GlfwWindow::GlfwWindow(GlfwWindow&& other) noexcept
    : window_(other.window_)
    , resized_(other.resized_)
{
    other.window_ = nullptr;
    other.resized_ = false;
    if (window_ != nullptr) {
        glfwSetWindowUserPointer(window_, this);
    }
}

GlfwWindow& GlfwWindow::operator=(GlfwWindow&& other) noexcept
{
    if (this != &other) {
        if (window_ != nullptr) {
            glfwDestroyWindow(window_);
        }

        window_ = other.window_;
        resized_ = other.resized_;
        other.window_ = nullptr;
        other.resized_ = false;
        if (window_ != nullptr) {
            glfwSetWindowUserPointer(window_, this);
        }
    }

    return *this;
}

GlfwWindow::~GlfwWindow()
{
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
    }
}

GlfwWindow GlfwWindow::create(int width, int height, std::string_view title)
{
    (void)glfw_lifetime();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    const std::string title_string(title);
    GLFWwindow* window = glfwCreateWindow(width, height, title_string.c_str(), nullptr, nullptr);
    if (window == nullptr) {
        throw std::runtime_error("failed to create GLFW window");
    }

    GlfwWindow result(window);
    glfwSetWindowUserPointer(window, &result);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    return result;
}

void GlfwWindow::poll_events() const
{
    glfwPollEvents();
}

void GlfwWindow::wait_events() const
{
    glfwWaitEvents();
}

bool GlfwWindow::should_close() const
{
    return window_ == nullptr || glfwWindowShouldClose(window_) == GLFW_TRUE;
}

bool GlfwWindow::was_resized()
{
    const bool result = resized_;
    resized_ = false;
    return result;
}

VkExtent2D GlfwWindow::framebuffer_extent() const
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    return VkExtent2D {
        .width = static_cast<std::uint32_t>(width > 0 ? width : 0),
        .height = static_cast<std::uint32_t>(height > 0 ? height : 0),
    };
}

std::vector<const char*> GlfwWindow::required_instance_extensions() const
{
    std::uint32_t count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&count);
    if (extensions == nullptr || count == 0) {
        throw std::runtime_error("GLFW did not provide Vulkan instance extensions");
    }

    return {extensions, extensions + count};
}

VkSurfaceKHR GlfwWindow::create_surface(VkInstance instance) const
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult result = glfwCreateWindowSurface(instance, window_, nullptr, &surface);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to create Vulkan surface");
    }

    return surface;
}

void GlfwWindow::notify_resized() noexcept
{
    resized_ = true;
}

} // namespace vlk::platform
