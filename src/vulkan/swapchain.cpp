#include "vlk_be_gui/swapchain.hpp"

#include <algorithm>
#include <array>
#include <sstream>
#include <string>

namespace vlk::vk {

namespace {

[[nodiscard]] std::string vk_result_to_string(VkResult result)
{
    return std::to_string(static_cast<int>(result));
}

[[nodiscard]] VkCompositeAlphaFlagBitsKHR choose_composite_alpha(
    VkCompositeAlphaFlagsKHR supported_flags) noexcept
{
    constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> preferred {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };

    for (VkCompositeAlphaFlagBitsKHR flag : preferred) {
        if ((supported_flags & flag) != 0U) {
            return flag;
        }
    }

    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

} // namespace

VkSurfaceFormatKHR choose_swapchain_surface_format(std::span<const VkSurfaceFormatKHR> formats) noexcept
{
    const auto preferred = std::find_if(formats.begin(), formats.end(), [](const VkSurfaceFormatKHR& format) {
        return format.format == VK_FORMAT_B8G8R8A8_SRGB
            && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });

    if (preferred != formats.end()) {
        return *preferred;
    }

    return formats.empty()
        ? VkSurfaceFormatKHR {VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
        : formats.front();
}

VkPresentModeKHR choose_swapchain_present_mode(std::span<const VkPresentModeKHR> present_modes) noexcept
{
    const auto mailbox = std::find(present_modes.begin(), present_modes.end(), VK_PRESENT_MODE_MAILBOX_KHR);
    if (mailbox != present_modes.end()) {
        return VK_PRESENT_MODE_MAILBOX_KHR;
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D choose_swapchain_extent(
    const VkSurfaceCapabilitiesKHR& capabilities,
    VkExtent2D desired_extent) noexcept
{
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }

    return VkExtent2D {
        .width = std::clamp(
            desired_extent.width,
            capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width),
        .height = std::clamp(
            desired_extent.height,
            capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height),
    };
}

Swapchain::Swapchain(Swapchain&& other) noexcept
    : device_(other.device_)
    , swapchain_(other.swapchain_)
    , image_format_(other.image_format_)
    , extent_(other.extent_)
    , images_(std::move(other.images_))
    , image_views_(std::move(other.image_views_))
{
    other.device_ = VK_NULL_HANDLE;
    other.swapchain_ = VK_NULL_HANDLE;
    other.image_format_ = VK_FORMAT_UNDEFINED;
    other.extent_ = {};
}

Swapchain& Swapchain::operator=(Swapchain&& other) noexcept
{
    if (this != &other) {
        shutdown();

        device_ = other.device_;
        swapchain_ = other.swapchain_;
        image_format_ = other.image_format_;
        extent_ = other.extent_;
        images_ = std::move(other.images_);
        image_views_ = std::move(other.image_views_);

        other.device_ = VK_NULL_HANDLE;
        other.swapchain_ = VK_NULL_HANDLE;
        other.image_format_ = VK_FORMAT_UNDEFINED;
        other.extent_ = {};
    }

    return *this;
}

Swapchain::~Swapchain()
{
    shutdown();
}

Error Swapchain::create(
    const VulkanContext& context,
    VkExtent2D desired_extent,
    Swapchain& out_swapchain)
{
    if (context.device() == VK_NULL_HANDLE || context.surface() == VK_NULL_HANDLE) {
        return Error("cannot create swapchain without a Vulkan device and surface");
    }

    SwapchainSupport support;
    if (Error error = query_support(context.physical_device(), context.surface(), support)) {
        return error;
    }
    if (support.formats.empty()) {
        return Error("Vulkan surface reports no swapchain formats");
    }
    if (support.present_modes.empty()) {
        return Error("Vulkan surface reports no present modes");
    }

    const VkSurfaceFormatKHR surface_format = choose_swapchain_surface_format(support.formats);
    const VkPresentModeKHR present_mode = choose_swapchain_present_mode(support.present_modes);
    const VkExtent2D extent = choose_swapchain_extent(support.capabilities, desired_extent);
    if (extent.width == 0 || extent.height == 0) {
        return Error("cannot create swapchain for a zero-sized framebuffer");
    }

    std::uint32_t image_count = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && image_count > support.capabilities.maxImageCount) {
        image_count = support.capabilities.maxImageCount;
    }

    const std::array queue_family_indices {
        context.graphics_queue_family(),
        context.present_queue_family(),
    };

    VkSwapchainCreateInfoKHR create_info {};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = context.surface();
    create_info.minImageCount = image_count;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (queue_family_indices[0] != queue_family_indices[1]) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = static_cast<std::uint32_t>(queue_family_indices.size());
        create_info.pQueueFamilyIndices = queue_family_indices.data();
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    create_info.preTransform = support.capabilities.currentTransform;
    create_info.compositeAlpha = choose_composite_alpha(support.capabilities.supportedCompositeAlpha);
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = VK_NULL_HANDLE;

    Swapchain swapchain;
    swapchain.device_ = context.device();
    swapchain.image_format_ = surface_format.format;
    swapchain.extent_ = extent;

    VkResult result = vkCreateSwapchainKHR(context.device(), &create_info, nullptr, &swapchain.swapchain_);
    if (result != VK_SUCCESS) {
        return Error("vkCreateSwapchainKHR failed with VkResult " + vk_result_to_string(result));
    }

    std::uint32_t actual_image_count = 0;
    result = vkGetSwapchainImagesKHR(context.device(), swapchain.swapchain_, &actual_image_count, nullptr);
    if (result != VK_SUCCESS) {
        return Error("vkGetSwapchainImagesKHR failed with VkResult " + vk_result_to_string(result));
    }

    swapchain.images_.resize(actual_image_count);
    result = vkGetSwapchainImagesKHR(
        context.device(),
        swapchain.swapchain_,
        &actual_image_count,
        swapchain.images_.data());
    if (result != VK_SUCCESS) {
        return Error("vkGetSwapchainImagesKHR failed with VkResult " + vk_result_to_string(result));
    }

    if (Error error = create_image_views(swapchain)) {
        return error;
    }

    out_swapchain = std::move(swapchain);
    return {};
}

Error Swapchain::recreate(const VulkanContext& context, VkExtent2D desired_extent)
{
    Swapchain replacement;
    if (Error error = create(context, desired_extent, replacement)) {
        return error;
    }

    *this = std::move(replacement);
    return {};
}

VkSwapchainKHR Swapchain::handle() const noexcept
{
    return swapchain_;
}

VkFormat Swapchain::image_format() const noexcept
{
    return image_format_;
}

VkExtent2D Swapchain::extent() const noexcept
{
    return extent_;
}

std::span<const VkImage> Swapchain::images() const noexcept
{
    return images_;
}

std::span<const VkImageView> Swapchain::image_views() const noexcept
{
    return image_views_;
}

void Swapchain::shutdown() noexcept
{
    if (device_ != VK_NULL_HANDLE) {
        for (VkImageView image_view : image_views_) {
            vkDestroyImageView(device_, image_view, nullptr);
        }
        image_views_.clear();

        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
    }

    device_ = VK_NULL_HANDLE;
    image_format_ = VK_FORMAT_UNDEFINED;
    extent_ = {};
    images_.clear();
}

Error Swapchain::query_support(
    VkPhysicalDevice physical_device,
    VkSurfaceKHR surface,
    SwapchainSupport& support)
{
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        physical_device,
        surface,
        &support.capabilities);
    if (result != VK_SUCCESS) {
        return Error("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed with VkResult " + vk_result_to_string(result));
    }

    std::uint32_t count = 0;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, nullptr);
    if (result != VK_SUCCESS) {
        return Error("vkGetPhysicalDeviceSurfaceFormatsKHR failed with VkResult " + vk_result_to_string(result));
    }
    support.formats.resize(count);
    if (count > 0) {
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, support.formats.data());
        if (result != VK_SUCCESS) {
            return Error("vkGetPhysicalDeviceSurfaceFormatsKHR failed with VkResult " + vk_result_to_string(result));
        }
    }

    result = vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, nullptr);
    if (result != VK_SUCCESS) {
        return Error("vkGetPhysicalDeviceSurfacePresentModesKHR failed with VkResult " + vk_result_to_string(result));
    }
    support.present_modes.resize(count);
    if (count > 0) {
        result = vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, support.present_modes.data());
        if (result != VK_SUCCESS) {
            return Error("vkGetPhysicalDeviceSurfacePresentModesKHR failed with VkResult " + vk_result_to_string(result));
        }
    }

    return {};
}

Error Swapchain::create_image_views(Swapchain& swapchain)
{
    swapchain.image_views_.reserve(swapchain.images_.size());

    for (VkImage image : swapchain.images_) {
        VkImageViewCreateInfo create_info {};
        create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        create_info.image = image;
        create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        create_info.format = swapchain.image_format_;
        create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        create_info.subresourceRange.baseMipLevel = 0;
        create_info.subresourceRange.levelCount = 1;
        create_info.subresourceRange.baseArrayLayer = 0;
        create_info.subresourceRange.layerCount = 1;

        VkImageView image_view = VK_NULL_HANDLE;
        const VkResult result = vkCreateImageView(swapchain.device_, &create_info, nullptr, &image_view);
        if (result != VK_SUCCESS) {
            return Error("vkCreateImageView failed with VkResult " + vk_result_to_string(result));
        }

        swapchain.image_views_.push_back(image_view);
    }

    return {};
}

} // namespace vlk::vk
