#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <vulkan/vulkan.h>

#include "vlk_be_gui/error.hpp"
#include "vlk_be_gui/vulkan_context.hpp"

namespace vlk::vk {

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities {};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
};

[[nodiscard]] VkSurfaceFormatKHR choose_swapchain_surface_format(
    std::span<const VkSurfaceFormatKHR> formats) noexcept;
[[nodiscard]] VkPresentModeKHR choose_swapchain_present_mode(
    std::span<const VkPresentModeKHR> present_modes) noexcept;
[[nodiscard]] VkExtent2D choose_swapchain_extent(
    const VkSurfaceCapabilitiesKHR& capabilities,
    VkExtent2D desired_extent) noexcept;

class Swapchain {
public:
    Swapchain() = default;
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&& other) noexcept;
    Swapchain& operator=(Swapchain&& other) noexcept;
    ~Swapchain();

    [[nodiscard]] static Error create(
        const VulkanContext& context,
        VkExtent2D desired_extent,
        Swapchain& out_swapchain);

    [[nodiscard]] Error recreate(const VulkanContext& context, VkExtent2D desired_extent);

    [[nodiscard]] VkSwapchainKHR handle() const noexcept;
    [[nodiscard]] VkFormat image_format() const noexcept;
    [[nodiscard]] VkExtent2D extent() const noexcept;
    [[nodiscard]] std::span<const VkImage> images() const noexcept;
    [[nodiscard]] std::span<const VkImageView> image_views() const noexcept;

    void shutdown() noexcept;

private:
    [[nodiscard]] static Error query_support(
        VkPhysicalDevice physical_device,
        VkSurfaceKHR surface,
        SwapchainSupport& support);
    [[nodiscard]] static Error create_image_views(Swapchain& swapchain);

    VkDevice device_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat image_format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_ {};
    std::vector<VkImage> images_;
    std::vector<VkImageView> image_views_;
};

} // namespace vlk::vk
