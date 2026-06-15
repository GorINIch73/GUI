#include "triangle_pipeline.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace vlk::smoke {

namespace {

[[nodiscard]] std::string vk_result_to_string(VkResult result)
{
    return std::to_string(static_cast<int>(result));
}

[[nodiscard]] Error result_error(const char* operation, VkResult result)
{
    return Error(std::string(operation) + " failed with VkResult " + vk_result_to_string(result));
}

[[nodiscard]] Error read_spirv_file(std::string_view path, std::vector<std::uint32_t>& out_words)
{
    std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
    if (!file) {
        return Error("failed to open SPIR-V file: " + std::string(path));
    }

    const std::ifstream::pos_type size = file.tellg();
    if (size <= 0 || (static_cast<std::uint64_t>(size) % sizeof(std::uint32_t)) != 0U) {
        return Error("invalid SPIR-V byte size: " + std::string(path));
    }

    out_words.resize(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(out_words.data()), size);
    if (!file) {
        return Error("failed to read SPIR-V file: " + std::string(path));
    }

    return {};
}

[[nodiscard]] Error create_shader_module(
    VkDevice device,
    std::span<const std::uint32_t> code,
    VkShaderModule& out_module)
{
    VkShaderModuleCreateInfo create_info {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = code.size_bytes();
    create_info.pCode = code.data();

    const VkResult result = vkCreateShaderModule(device, &create_info, nullptr, &out_module);
    if (result != VK_SUCCESS) {
        return result_error("vkCreateShaderModule", result);
    }

    return {};
}

} // namespace

TrianglePipeline::TrianglePipeline(TrianglePipeline&& other) noexcept
    : device_(other.device_)
    , pipeline_layout_(other.pipeline_layout_)
    , pipeline_(other.pipeline_)
{
    other.device_ = VK_NULL_HANDLE;
    other.pipeline_layout_ = VK_NULL_HANDLE;
    other.pipeline_ = VK_NULL_HANDLE;
}

TrianglePipeline& TrianglePipeline::operator=(TrianglePipeline&& other) noexcept
{
    if (this != &other) {
        shutdown();

        device_ = other.device_;
        pipeline_layout_ = other.pipeline_layout_;
        pipeline_ = other.pipeline_;

        other.device_ = VK_NULL_HANDLE;
        other.pipeline_layout_ = VK_NULL_HANDLE;
        other.pipeline_ = VK_NULL_HANDLE;
    }

    return *this;
}

TrianglePipeline::~TrianglePipeline()
{
    shutdown();
}

Error TrianglePipeline::create(
    const vk::VulkanContext& context,
    const vk::Swapchain& swapchain,
    const TrianglePipelineCreateInfo& create_info,
    TrianglePipeline& out_pipeline)
{
    std::vector<std::uint32_t> vertex_code;
    std::vector<std::uint32_t> fragment_code;
    if (Error error = read_spirv_file(create_info.vertex_shader_spv_path, vertex_code)) {
        return error;
    }
    if (Error error = read_spirv_file(create_info.fragment_shader_spv_path, fragment_code)) {
        return error;
    }

    VkShaderModule vertex_shader = VK_NULL_HANDLE;
    VkShaderModule fragment_shader = VK_NULL_HANDLE;
    if (Error error = create_shader_module(context.device(), vertex_code, vertex_shader)) {
        return error;
    }
    if (Error error = create_shader_module(context.device(), fragment_code, fragment_shader)) {
        vkDestroyShaderModule(context.device(), vertex_shader, nullptr);
        return error;
    }

    TrianglePipeline pipeline;
    pipeline.device_ = context.device();

    VkPipelineLayoutCreateInfo layout_info {};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    VkResult result = vkCreatePipelineLayout(
        context.device(),
        &layout_info,
        nullptr,
        &pipeline.pipeline_layout_);
    if (result != VK_SUCCESS) {
        vkDestroyShaderModule(context.device(), fragment_shader, nullptr);
        vkDestroyShaderModule(context.device(), vertex_shader, nullptr);
        return result_error("vkCreatePipelineLayout", result);
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages {};
    shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = vertex_shader;
    shader_stages[0].pName = "main";
    shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = fragment_shader;
    shader_stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertex_input {};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo input_assembly {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization {};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0F;

    VkPipelineMultisampleStateCreateInfo multisample {};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachment {};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
        | VK_COLOR_COMPONENT_G_BIT
        | VK_COLOR_COMPONENT_B_BIT
        | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend {};
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &color_blend_attachment;

    constexpr std::array dynamic_states {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic_state {};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<std::uint32_t>(dynamic_states.size());
    dynamic_state.pDynamicStates = dynamic_states.data();

    const VkFormat color_attachment_format = swapchain.image_format();
    VkPipelineRenderingCreateInfo rendering_info {};
    rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachmentFormats = &color_attachment_format;

    VkGraphicsPipelineCreateInfo pipeline_info {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = &rendering_info;
    pipeline_info.stageCount = static_cast<std::uint32_t>(shader_stages.size());
    pipeline_info.pStages = shader_stages.data();
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterization;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &color_blend;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = pipeline.pipeline_layout_;
    pipeline_info.renderPass = VK_NULL_HANDLE;
    pipeline_info.subpass = 0;

    result = vkCreateGraphicsPipelines(
        context.device(),
        VK_NULL_HANDLE,
        1,
        &pipeline_info,
        nullptr,
        &pipeline.pipeline_);

    vkDestroyShaderModule(context.device(), fragment_shader, nullptr);
    vkDestroyShaderModule(context.device(), vertex_shader, nullptr);

    if (result != VK_SUCCESS) {
        return result_error("vkCreateGraphicsPipelines", result);
    }

    out_pipeline = std::move(pipeline);
    return {};
}

void TrianglePipeline::draw(const vk::FrameRenderer::FrameContext& frame) const noexcept
{
    if (pipeline_ == VK_NULL_HANDLE || frame.command_buffer == VK_NULL_HANDLE) {
        return;
    }

    const VkViewport viewport {
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>(frame.extent.width),
        .height = static_cast<float>(frame.extent.height),
        .minDepth = 0.0F,
        .maxDepth = 1.0F,
    };
    const VkRect2D scissor {
        .offset = VkOffset2D {0, 0},
        .extent = frame.extent,
    };

    vkCmdSetViewport(frame.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(frame.command_buffer, 0, 1, &scissor);
    vkCmdBindPipeline(frame.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdDraw(frame.command_buffer, 3, 1, 0, 0);
}

void TrianglePipeline::shutdown() noexcept
{
    if (device_ != VK_NULL_HANDLE) {
        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }
        if (pipeline_layout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
            pipeline_layout_ = VK_NULL_HANDLE;
        }
    }

    device_ = VK_NULL_HANDLE;
}

} // namespace vlk::smoke
