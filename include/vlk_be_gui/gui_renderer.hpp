#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <vulkan/vulkan.h>

#include "vlk_be_gui/draw_data.hpp"
#include "vlk_be_gui/error.hpp"
#include "vlk_be_gui/frame_renderer.hpp"
#include "vlk_be_gui/swapchain.hpp"
#include "vlk_be_gui/vulkan_context.hpp"

namespace vlk::vk {

struct GuiRendererCreateInfo {
    std::string_view vertex_shader_spv_path;
    std::string_view fragment_shader_spv_path;
};

class GuiRenderer {
public:
    GuiRenderer() = default;
    GuiRenderer(const GuiRenderer&) = delete;
    GuiRenderer& operator=(const GuiRenderer&) = delete;
    GuiRenderer(GuiRenderer&& other) noexcept;
    GuiRenderer& operator=(GuiRenderer&& other) noexcept;
    ~GuiRenderer();

    [[nodiscard]] static Error create(
        const VulkanContext& context,
        const Swapchain& swapchain,
        const GuiRendererCreateInfo& create_info,
        GuiRenderer& out_renderer);

    [[nodiscard]] Error draw(
        const VulkanContext& context,
        const FrameRenderer::FrameContext& frame,
        const gui::DrawList& draw_list);
    [[nodiscard]] Error create_texture_rgba8(
        const VulkanContext& context,
        std::uint32_t width,
        std::uint32_t height,
        const void* rgba_pixels,
        gui::TextureHandle& out_texture);
    // GPU resources are released after any frames that referenced this texture have completed.
    void destroy_texture(gui::TextureHandle texture) noexcept;
    [[nodiscard]] bool is_texture_alive(gui::TextureHandle texture) const noexcept;

    void shutdown() noexcept;

private:
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
    };

    struct FrameBuffers {
        Buffer vertices;
        Buffer indices;
    };

    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView image_view = VK_NULL_HANDLE;
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t used_frame_mask = 0;
    };

    struct DeferredTexture {
        Texture texture;
        std::uint32_t pending_frame_mask = 0;
    };

    [[nodiscard]] static Error create_buffer(
        const VulkanContext& context,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        Buffer& out_buffer);
    [[nodiscard]] static Error ensure_buffer(
        const VulkanContext& context,
        VkDeviceSize required_size,
        VkBufferUsageFlags usage,
        Buffer& buffer);
    void destroy_buffer(Buffer& buffer) noexcept;
    [[nodiscard]] Error upload(Buffer& buffer, const void* data, VkDeviceSize size) const;
    [[nodiscard]] Error create_texture_internal(
        const VulkanContext& context,
        std::uint32_t width,
        std::uint32_t height,
        const void* rgba_pixels,
        Texture& out_texture);
    [[nodiscard]] Error create_white_texture(const VulkanContext& context);
    [[nodiscard]] Error create_image(
        const VulkanContext& context,
        std::uint32_t width,
        std::uint32_t height,
        Texture& out_texture);
    [[nodiscard]] Error allocate_texture_descriptor(Texture& texture);
    [[nodiscard]] Error submit_texture_upload(
        const VulkanContext& context,
        Buffer& staging,
        Texture& texture);
    void destroy_texture(Texture& texture) noexcept;
    void collect_deferred_textures(std::uint32_t completed_frame_index) noexcept;
    void mark_texture_used(gui::TextureHandle texture, std::uint32_t frame_index) noexcept;
    [[nodiscard]] VkDescriptorSet descriptor_for(gui::TextureHandle texture) const noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    std::array<FrameBuffers, FrameRenderer::max_frames_in_flight> frame_buffers_ {};
    std::vector<Texture> textures_;
    std::vector<DeferredTexture> deferred_textures_;
    gui::TextureHandle white_texture_ {};
};

} // namespace vlk::vk
