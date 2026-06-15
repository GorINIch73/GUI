#include <cassert>
#include <array>
#include <cstdint>

#include "vlk_be_gui/draw_data.hpp"
#include "vlk_be_gui/swapchain.hpp"

int main()
{
    static_assert(vlk::gui::pack_rgba(0x11, 0x22, 0x33, 0x44) == 0x44332211U);

    const vlk::gui::TextureHandle empty {};
    assert(!empty);

    const vlk::gui::TextureHandle texture {42};
    assert(texture);

    const std::array formats {
        VkSurfaceFormatKHR {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        VkSurfaceFormatKHR {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    const VkSurfaceFormatKHR format = vlk::vk::choose_swapchain_surface_format(formats);
    assert(format.format == VK_FORMAT_B8G8R8A8_SRGB);
    assert(format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);

    const std::array present_modes {
        VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_MAILBOX_KHR,
    };
    assert(vlk::vk::choose_swapchain_present_mode(present_modes) == VK_PRESENT_MODE_MAILBOX_KHR);

    const std::array fallback_present_modes {
        VK_PRESENT_MODE_IMMEDIATE_KHR,
    };
    assert(vlk::vk::choose_swapchain_present_mode(fallback_present_modes) == VK_PRESENT_MODE_FIFO_KHR);

    VkSurfaceCapabilitiesKHR capabilities {};
    capabilities.currentExtent = VkExtent2D {UINT32_MAX, UINT32_MAX};
    capabilities.minImageExtent = VkExtent2D {320, 240};
    capabilities.maxImageExtent = VkExtent2D {1920, 1080};
    const VkExtent2D clamped = vlk::vk::choose_swapchain_extent(capabilities, VkExtent2D {2560, 100});
    assert(clamped.width == 1920);
    assert(clamped.height == 240);

    capabilities.currentExtent = VkExtent2D {800, 600};
    const VkExtent2D fixed = vlk::vk::choose_swapchain_extent(capabilities, VkExtent2D {1280, 720});
    assert(fixed.width == 800);
    assert(fixed.height == 600);

    return 0;
}
