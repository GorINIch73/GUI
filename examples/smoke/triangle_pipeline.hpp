#pragma once

#include <string_view>

#include <vulkan/vulkan.h>

#include "vlk_be_gui/error.hpp"
#include "vlk_be_gui/frame_renderer.hpp"
#include "vlk_be_gui/swapchain.hpp"
#include "vlk_be_gui/vulkan_context.hpp"

namespace vlk::smoke {

struct TrianglePipelineCreateInfo {
    std::string_view vertex_shader_spv_path;
    std::string_view fragment_shader_spv_path;
};

class TrianglePipeline {
public:
    TrianglePipeline() = default;
    TrianglePipeline(const TrianglePipeline&) = delete;
    TrianglePipeline& operator=(const TrianglePipeline&) = delete;
    TrianglePipeline(TrianglePipeline&& other) noexcept;
    TrianglePipeline& operator=(TrianglePipeline&& other) noexcept;
    ~TrianglePipeline();

    [[nodiscard]] static Error create(
        const vk::VulkanContext& context,
        const vk::Swapchain& swapchain,
        const TrianglePipelineCreateInfo& create_info,
        TrianglePipeline& out_pipeline);

    void draw(const vk::FrameRenderer::FrameContext& frame) const noexcept;
    void shutdown() noexcept;

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace vlk::smoke
