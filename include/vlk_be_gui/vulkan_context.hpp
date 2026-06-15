#pragma once

#include <climits>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <vulkan/vulkan.h>

#include "vlk_be_gui/error.hpp"

namespace vlk::vk {

struct VulkanContextCreateInfo {
    std::string_view application_name = "vlk_be_gui";
    std::vector<const char*> required_instance_extensions {};
    std::function<VkSurfaceKHR(VkInstance)> create_surface;
};

class VulkanContext {
public:
    VulkanContext() = default;
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&& other) noexcept;
    VulkanContext& operator=(VulkanContext&& other) noexcept;
    ~VulkanContext();

    [[nodiscard]] static Error create(const VulkanContextCreateInfo& create_info, VulkanContext& out_context);

    [[nodiscard]] VkInstance instance() const noexcept;
    [[nodiscard]] VkSurfaceKHR surface() const noexcept;
    [[nodiscard]] VkPhysicalDevice physical_device() const noexcept;
    [[nodiscard]] VkDevice device() const noexcept;
    [[nodiscard]] VkQueue graphics_queue() const noexcept;
    [[nodiscard]] VkQueue present_queue() const noexcept;
    [[nodiscard]] std::uint32_t graphics_queue_family() const noexcept;
    [[nodiscard]] std::uint32_t present_queue_family() const noexcept;

    void wait_idle() const;
    void shutdown() noexcept;

private:
    struct QueueFamilies {
        std::uint32_t graphics = UINT32_MAX;
        std::uint32_t present = UINT32_MAX;
    };

    [[nodiscard]] static bool is_complete(const QueueFamilies& families) noexcept;
    [[nodiscard]] static Error create_instance(
        const VulkanContextCreateInfo& create_info,
        VkInstance& instance);
    [[nodiscard]] static bool validation_layers_available();
    [[nodiscard]] static std::vector<const char*> enabled_layers();
    [[nodiscard]] static QueueFamilies find_queue_families(
        VkPhysicalDevice physical_device,
        VkSurfaceKHR surface);
    [[nodiscard]] static Error pick_physical_device(
        VkInstance instance,
        VkSurfaceKHR surface,
        VkPhysicalDevice& physical_device,
        QueueFamilies& queue_families);
    [[nodiscard]] static Error create_logical_device(
        VkPhysicalDevice physical_device,
        const QueueFamilies& queue_families,
        VkDevice& device,
        VkQueue& graphics_queue,
        VkQueue& present_queue);

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    QueueFamilies queue_families_ {};
};

} // namespace vlk::vk
