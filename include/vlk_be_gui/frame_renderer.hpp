#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include "vlk_be_gui/error.hpp"
#include "vlk_be_gui/swapchain.hpp"
#include "vlk_be_gui/vulkan_context.hpp"

namespace vlk::vk {

class FrameRenderer {
public:
    static constexpr std::uint32_t max_frames_in_flight = 2;

    enum class FrameStatus {
        ok,
        suboptimal,
        out_of_date,
        minimized,
    };

    FrameRenderer() = default;
    FrameRenderer(const FrameRenderer&) = delete;
    FrameRenderer& operator=(const FrameRenderer&) = delete;
    FrameRenderer(FrameRenderer&& other) noexcept;
    FrameRenderer& operator=(FrameRenderer&& other) noexcept;
    ~FrameRenderer();

    struct DrawFrameResult {
        FrameStatus status = FrameStatus::ok;
    };

    struct BeginFrameInfo {
        VkClearColorValue clear_color {};
    };

    struct FrameContext {
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        std::uint32_t image_index = 0;
        std::uint32_t frame_index = 0;
        VkExtent2D extent {};
        VkFormat image_format = VK_FORMAT_UNDEFINED;
    };

    struct EndFrameResult {
        FrameStatus status = FrameStatus::ok;
    };

    [[nodiscard]] static Error create(
        const VulkanContext& context,
        const Swapchain& swapchain,
        FrameRenderer& out_renderer);

    [[nodiscard]] Error notify_swapchain_recreated(const Swapchain& swapchain);
    [[nodiscard]] Error begin_frame(
        const Swapchain& swapchain,
        const BeginFrameInfo& begin_info,
        FrameContext& out_frame,
        DrawFrameResult& out_result);
    [[nodiscard]] Error end_frame(
        const VulkanContext& context,
        const Swapchain& swapchain,
        const FrameContext& frame,
        EndFrameResult& out_result);
    [[nodiscard]] Error draw_clear_frame(
        const VulkanContext& context,
        const Swapchain& swapchain,
        VkClearColorValue clear_color,
        DrawFrameResult& out_result);

    void shutdown() noexcept;

private:
    struct FrameSync {
        VkSemaphore image_available = VK_NULL_HANDLE;
        VkFence in_flight = VK_NULL_HANDLE;
    };

    [[nodiscard]] static Error create_sync_objects(FrameRenderer& renderer);
    [[nodiscard]] static Error create_image_sync_objects(FrameRenderer& renderer, const Swapchain& swapchain);
    [[nodiscard]] static Error allocate_command_buffers(FrameRenderer& renderer);
    void destroy_image_sync_objects() noexcept;
    [[nodiscard]] Error begin_clear_commands(
        const Swapchain& swapchain,
        std::uint32_t image_index,
        VkClearColorValue clear_color);
    [[nodiscard]] Error end_clear_commands(
        const Swapchain& swapchain,
        std::uint32_t image_index);

    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, max_frames_in_flight> command_buffers_ {};
    std::array<FrameSync, max_frames_in_flight> frames_ {};
    std::vector<VkSemaphore> image_render_finished_;
    std::vector<VkFence> image_fences_;
    std::uint32_t current_frame_ = 0;
};

} // namespace vlk::vk
