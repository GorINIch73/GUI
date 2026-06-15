#include "vlk_be_gui/gui_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace vlk::vk {

namespace {

struct PushConstants {
    float framebuffer_size[2] {};
};

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

[[nodiscard]] Error find_memory_type(
    VkPhysicalDevice physical_device,
    std::uint32_t type_filter,
    VkMemoryPropertyFlags properties,
    std::uint32_t& out_type)
{
    VkPhysicalDeviceMemoryProperties memory_properties {};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
        const bool type_matches = (type_filter & (1U << index)) != 0U;
        const bool properties_match = (memory_properties.memoryTypes[index].propertyFlags & properties) == properties;
        if (type_matches && properties_match) {
            out_type = index;
            return {};
        }
    }

    return Error("failed to find suitable Vulkan memory type");
}

[[nodiscard]] VkRect2D clip_to_scissor(const gui::Rect& clip_rect, VkExtent2D extent) noexcept
{
    const float min_x = std::clamp(clip_rect.x, 0.0F, static_cast<float>(extent.width));
    const float min_y = std::clamp(clip_rect.y, 0.0F, static_cast<float>(extent.height));
    const float max_x = std::clamp(clip_rect.x + clip_rect.width, min_x, static_cast<float>(extent.width));
    const float max_y = std::clamp(clip_rect.y + clip_rect.height, min_y, static_cast<float>(extent.height));

    return VkRect2D {
        .offset = VkOffset2D {
            .x = static_cast<std::int32_t>(min_x),
            .y = static_cast<std::int32_t>(min_y),
        },
        .extent = VkExtent2D {
            .width = static_cast<std::uint32_t>(max_x - min_x),
            .height = static_cast<std::uint32_t>(max_y - min_y),
        },
    };
}

} // namespace

GuiRenderer::GuiRenderer(GuiRenderer&& other) noexcept
    : device_(other.device_)
    , descriptor_set_layout_(other.descriptor_set_layout_)
    , descriptor_pool_(other.descriptor_pool_)
    , sampler_(other.sampler_)
    , pipeline_layout_(other.pipeline_layout_)
    , pipeline_(other.pipeline_)
    , frame_buffers_(other.frame_buffers_)
    , textures_(std::move(other.textures_))
    , white_texture_(other.white_texture_)
{
    other.device_ = VK_NULL_HANDLE;
    other.descriptor_set_layout_ = VK_NULL_HANDLE;
    other.descriptor_pool_ = VK_NULL_HANDLE;
    other.sampler_ = VK_NULL_HANDLE;
    other.pipeline_layout_ = VK_NULL_HANDLE;
    other.pipeline_ = VK_NULL_HANDLE;
    other.frame_buffers_ = {};
    other.white_texture_ = {};
}

GuiRenderer& GuiRenderer::operator=(GuiRenderer&& other) noexcept
{
    if (this != &other) {
        shutdown();

        device_ = other.device_;
        descriptor_set_layout_ = other.descriptor_set_layout_;
        descriptor_pool_ = other.descriptor_pool_;
        sampler_ = other.sampler_;
        pipeline_layout_ = other.pipeline_layout_;
        pipeline_ = other.pipeline_;
        frame_buffers_ = other.frame_buffers_;
        textures_ = std::move(other.textures_);
        white_texture_ = other.white_texture_;

        other.device_ = VK_NULL_HANDLE;
        other.descriptor_set_layout_ = VK_NULL_HANDLE;
        other.descriptor_pool_ = VK_NULL_HANDLE;
        other.sampler_ = VK_NULL_HANDLE;
        other.pipeline_layout_ = VK_NULL_HANDLE;
        other.pipeline_ = VK_NULL_HANDLE;
        other.frame_buffers_ = {};
        other.white_texture_ = {};
    }

    return *this;
}

GuiRenderer::~GuiRenderer()
{
    shutdown();
}

Error GuiRenderer::create(
    const VulkanContext& context,
    const Swapchain& swapchain,
    const GuiRendererCreateInfo& create_info,
    GuiRenderer& out_renderer)
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

    GuiRenderer renderer;
    renderer.device_ = context.device();

    VkDescriptorSetLayoutBinding sampler_binding {};
    sampler_binding.binding = 0;
    sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_binding.descriptorCount = 1;
    sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo descriptor_layout_info {};
    descriptor_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor_layout_info.bindingCount = 1;
    descriptor_layout_info.pBindings = &sampler_binding;

    VkResult result = vkCreateDescriptorSetLayout(
        context.device(),
        &descriptor_layout_info,
        nullptr,
        &renderer.descriptor_set_layout_);
    if (result != VK_SUCCESS) {
        vkDestroyShaderModule(context.device(), fragment_shader, nullptr);
        vkDestroyShaderModule(context.device(), vertex_shader, nullptr);
        return result_error("vkCreateDescriptorSetLayout", result);
    }

    VkDescriptorPoolSize pool_size {};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 64;

    VkDescriptorPoolCreateInfo descriptor_pool_info {};
    descriptor_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool_info.maxSets = 64;
    descriptor_pool_info.poolSizeCount = 1;
    descriptor_pool_info.pPoolSizes = &pool_size;

    result = vkCreateDescriptorPool(context.device(), &descriptor_pool_info, nullptr, &renderer.descriptor_pool_);
    if (result != VK_SUCCESS) {
        vkDestroyShaderModule(context.device(), fragment_shader, nullptr);
        vkDestroyShaderModule(context.device(), vertex_shader, nullptr);
        return result_error("vkCreateDescriptorPool", result);
    }

    VkSamplerCreateInfo sampler_info {};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = 0.0F;

    result = vkCreateSampler(context.device(), &sampler_info, nullptr, &renderer.sampler_);
    if (result != VK_SUCCESS) {
        vkDestroyShaderModule(context.device(), fragment_shader, nullptr);
        vkDestroyShaderModule(context.device(), vertex_shader, nullptr);
        return result_error("vkCreateSampler", result);
    }

    VkPushConstantRange push_constant_range {};
    push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo layout_info {};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &renderer.descriptor_set_layout_;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_constant_range;

    result = vkCreatePipelineLayout(
        context.device(),
        &layout_info,
        nullptr,
        &renderer.pipeline_layout_);
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

    VkVertexInputBindingDescription binding {};
    binding.binding = 0;
    binding.stride = sizeof(gui::DrawVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attributes {};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[0].offset = offsetof(gui::DrawVertex, x);
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[1].offset = offsetof(gui::DrawVertex, u);
    attributes[2].location = 2;
    attributes[2].binding = 0;
    attributes[2].format = VK_FORMAT_R8G8B8A8_UNORM;
    attributes[2].offset = offsetof(gui::DrawVertex, rgba);

    VkPipelineVertexInputStateCreateInfo vertex_input {};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertex_input.pVertexAttributeDescriptions = attributes.data();

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
    color_blend_attachment.blendEnable = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
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
    pipeline_info.layout = renderer.pipeline_layout_;
    pipeline_info.renderPass = VK_NULL_HANDLE;

    result = vkCreateGraphicsPipelines(
        context.device(),
        VK_NULL_HANDLE,
        1,
        &pipeline_info,
        nullptr,
        &renderer.pipeline_);

    vkDestroyShaderModule(context.device(), fragment_shader, nullptr);
    vkDestroyShaderModule(context.device(), vertex_shader, nullptr);

    if (result != VK_SUCCESS) {
        return result_error("vkCreateGraphicsPipelines", result);
    }

    if (Error error = renderer.create_white_texture(context)) {
        return error;
    }

    out_renderer = std::move(renderer);
    return {};
}

Error GuiRenderer::draw(
    const VulkanContext& context,
    const FrameRenderer::FrameContext& frame,
    const gui::DrawList& draw_list)
{
    if (draw_list.vertices.empty() || draw_list.indices.empty() || draw_list.commands.empty()) {
        return {};
    }
    if (frame.frame_index >= frame_buffers_.size()) {
        return Error("frame index is outside GUI renderer frame buffer state");
    }
    collect_deferred_textures(frame.frame_index);

    FrameBuffers& buffers = frame_buffers_[frame.frame_index];
    const VkDeviceSize vertex_bytes = static_cast<VkDeviceSize>(draw_list.vertices.size_bytes());
    const VkDeviceSize index_bytes = static_cast<VkDeviceSize>(draw_list.indices.size_bytes());

    if (Error error = ensure_buffer(context, vertex_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, buffers.vertices)) {
        return error;
    }
    if (Error error = ensure_buffer(context, index_bytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, buffers.indices)) {
        return error;
    }
    if (Error error = upload(buffers.vertices, draw_list.vertices.data(), vertex_bytes)) {
        return error;
    }
    if (Error error = upload(buffers.indices, draw_list.indices.data(), index_bytes)) {
        return error;
    }

    const VkViewport viewport {
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>(frame.extent.width),
        .height = static_cast<float>(frame.extent.height),
        .minDepth = 0.0F,
        .maxDepth = 1.0F,
    };
    const VkDeviceSize vertex_offset = 0;
    const PushConstants push_constants {
        .framebuffer_size = {
            static_cast<float>(frame.extent.width),
            static_cast<float>(frame.extent.height),
        },
    };

    vkCmdBindPipeline(frame.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdSetViewport(frame.command_buffer, 0, 1, &viewport);
    vkCmdPushConstants(
        frame.command_buffer,
        pipeline_layout_,
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(push_constants),
        &push_constants);
    vkCmdBindVertexBuffers(frame.command_buffer, 0, 1, &buffers.vertices.buffer, &vertex_offset);
    vkCmdBindIndexBuffer(frame.command_buffer, buffers.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

    for (const gui::DrawCommand& command : draw_list.commands) {
        if (command.index_count == 0) {
            continue;
        }

        const VkRect2D scissor = clip_to_scissor(command.clip_rect, frame.extent);
        if (scissor.extent.width == 0 || scissor.extent.height == 0) {
            continue;
        }

        const VkDescriptorSet descriptor_set = descriptor_for(command.texture);
        mark_texture_used(command.texture, frame.frame_index);
        vkCmdBindDescriptorSets(
            frame.command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout_,
            0,
            1,
            &descriptor_set,
            0,
            nullptr);
        vkCmdSetScissor(frame.command_buffer, 0, 1, &scissor);
        vkCmdDrawIndexed(frame.command_buffer, command.index_count, 1, command.index_offset, 0, 0);
    }

    return {};
}

void GuiRenderer::shutdown() noexcept
{
    if (device_ != VK_NULL_HANDLE) {
        for (FrameBuffers& frame_buffers : frame_buffers_) {
            destroy_buffer(frame_buffers.vertices);
            destroy_buffer(frame_buffers.indices);
        }
        for (Texture& texture : textures_) {
            destroy_texture(texture);
        }
        textures_.clear();
        for (DeferredTexture& deferred : deferred_textures_) {
            destroy_texture(deferred.texture);
        }
        deferred_textures_.clear();
        white_texture_ = {};

        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }
        if (pipeline_layout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
            pipeline_layout_ = VK_NULL_HANDLE;
        }
        if (sampler_ != VK_NULL_HANDLE) {
            vkDestroySampler(device_, sampler_, nullptr);
            sampler_ = VK_NULL_HANDLE;
        }
        if (descriptor_pool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
            descriptor_pool_ = VK_NULL_HANDLE;
        }
        if (descriptor_set_layout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);
            descriptor_set_layout_ = VK_NULL_HANDLE;
        }
    }

    device_ = VK_NULL_HANDLE;
}

Error GuiRenderer::create_buffer(
    const VulkanContext& context,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    Buffer& out_buffer)
{
    VkBufferCreateInfo buffer_info {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(context.device(), &buffer_info, nullptr, &out_buffer.buffer);
    if (result != VK_SUCCESS) {
        return result_error("vkCreateBuffer", result);
    }

    VkMemoryRequirements requirements {};
    vkGetBufferMemoryRequirements(context.device(), out_buffer.buffer, &requirements);

    std::uint32_t memory_type = 0;
    if (Error error = find_memory_type(
            context.physical_device(),
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            memory_type)) {
        vkDestroyBuffer(context.device(), out_buffer.buffer, nullptr);
        out_buffer.buffer = VK_NULL_HANDLE;
        return error;
    }

    VkMemoryAllocateInfo allocate_info {};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = memory_type;

    result = vkAllocateMemory(context.device(), &allocate_info, nullptr, &out_buffer.memory);
    if (result != VK_SUCCESS) {
        vkDestroyBuffer(context.device(), out_buffer.buffer, nullptr);
        out_buffer.buffer = VK_NULL_HANDLE;
        return result_error("vkAllocateMemory", result);
    }

    result = vkBindBufferMemory(context.device(), out_buffer.buffer, out_buffer.memory, 0);
    if (result != VK_SUCCESS) {
        vkFreeMemory(context.device(), out_buffer.memory, nullptr);
        vkDestroyBuffer(context.device(), out_buffer.buffer, nullptr);
        out_buffer = {};
        return result_error("vkBindBufferMemory", result);
    }

    out_buffer.size = size;
    return {};
}

Error GuiRenderer::ensure_buffer(
    const VulkanContext& context,
    VkDeviceSize required_size,
    VkBufferUsageFlags usage,
    Buffer& buffer)
{
    if (buffer.buffer != VK_NULL_HANDLE && buffer.size >= required_size) {
        return {};
    }

    if (buffer.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(context.device(), buffer.buffer, nullptr);
        vkFreeMemory(context.device(), buffer.memory, nullptr);
        buffer = {};
    }

    return create_buffer(context, required_size, usage, buffer);
}

void GuiRenderer::destroy_buffer(Buffer& buffer) noexcept
{
    if (device_ != VK_NULL_HANDLE) {
        if (buffer.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, buffer.buffer, nullptr);
        }
        if (buffer.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, buffer.memory, nullptr);
        }
    }

    buffer = {};
}

Error GuiRenderer::upload(Buffer& buffer, const void* data, VkDeviceSize size) const
{
    void* mapped = nullptr;
    const VkResult result = vkMapMemory(device_, buffer.memory, 0, size, 0, &mapped);
    if (result != VK_SUCCESS) {
        return result_error("vkMapMemory", result);
    }

    std::memcpy(mapped, data, static_cast<std::size_t>(size));
    vkUnmapMemory(device_, buffer.memory);
    return {};
}

Error GuiRenderer::create_texture_rgba8(
    const VulkanContext& context,
    std::uint32_t width,
    std::uint32_t height,
    const void* rgba_pixels,
    gui::TextureHandle& out_texture)
{
    out_texture = {};
    if (width == 0 || height == 0 || rgba_pixels == nullptr) {
        return Error("cannot create texture from empty RGBA8 pixels");
    }

    Texture texture;
    if (Error error = create_texture_internal(context, width, height, rgba_pixels, texture)) {
        return error;
    }

    for (std::size_t index = 0; index < textures_.size(); ++index) {
        if (textures_[index].image == VK_NULL_HANDLE) {
            textures_[index] = texture;
            out_texture = gui::TextureHandle {static_cast<std::uint64_t>(index + 1)};
            return {};
        }
    }

    textures_.push_back(texture);
    out_texture = gui::TextureHandle {static_cast<std::uint64_t>(textures_.size())};
    return {};
}

void GuiRenderer::destroy_texture(gui::TextureHandle texture) noexcept
{
    if (!texture || texture.value == white_texture_.value || texture.value > textures_.size()) {
        return;
    }

    Texture& stored = textures_[static_cast<std::size_t>(texture.value - 1)];
    if (stored.image == VK_NULL_HANDLE) {
        return;
    }

    Texture retired = stored;
    stored = {};
    if (retired.used_frame_mask == 0) {
        destroy_texture(retired);
        return;
    }

    deferred_textures_.push_back(DeferredTexture {
        .texture = retired,
        .pending_frame_mask = retired.used_frame_mask,
    });
}

bool GuiRenderer::is_texture_alive(gui::TextureHandle texture) const noexcept
{
    return texture
        && texture.value <= textures_.size()
        && textures_[static_cast<std::size_t>(texture.value - 1)].image != VK_NULL_HANDLE;
}

Error GuiRenderer::create_texture_internal(
    const VulkanContext& context,
    std::uint32_t width,
    std::uint32_t height,
    const void* rgba_pixels,
    Texture& out_texture)
{
    const VkDeviceSize upload_size = static_cast<VkDeviceSize>(width) * height * 4U;

    Buffer staging;
    if (Error error = create_buffer(context, upload_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging)) {
        return error;
    }
    if (Error error = upload(staging, rgba_pixels, upload_size)) {
        destroy_buffer(staging);
        return error;
    }

    Texture texture;
    if (Error error = create_image(context, width, height, texture)) {
        destroy_buffer(staging);
        return error;
    }
    if (Error error = submit_texture_upload(context, staging, texture)) {
        destroy_texture(texture);
        destroy_buffer(staging);
        return error;
    }
    destroy_buffer(staging);

    VkImageViewCreateInfo view_info {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = texture.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    VkResult result = vkCreateImageView(device_, &view_info, nullptr, &texture.image_view);
    if (result != VK_SUCCESS) {
        destroy_texture(texture);
        return result_error("vkCreateImageView", result);
    }

    if (Error error = allocate_texture_descriptor(texture)) {
        destroy_texture(texture);
        return error;
    }

    out_texture = texture;
    return {};
}

Error GuiRenderer::create_white_texture(const VulkanContext& context)
{
    constexpr std::uint32_t white_pixel = 0xFFFFFFFFU;
    return create_texture_rgba8(context, 1, 1, &white_pixel, white_texture_);
}

Error GuiRenderer::create_image(
    const VulkanContext& context,
    std::uint32_t width,
    std::uint32_t height,
    Texture& out_texture)
{
    VkImageCreateInfo image_info {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent = VkExtent3D {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateImage(context.device(), &image_info, nullptr, &out_texture.image);
    if (result != VK_SUCCESS) {
        return result_error("vkCreateImage", result);
    }

    VkMemoryRequirements requirements {};
    vkGetImageMemoryRequirements(context.device(), out_texture.image, &requirements);

    std::uint32_t memory_type = 0;
    if (Error error = find_memory_type(
            context.physical_device(),
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            memory_type)) {
        vkDestroyImage(context.device(), out_texture.image, nullptr);
        out_texture.image = VK_NULL_HANDLE;
        return error;
    }

    VkMemoryAllocateInfo allocate_info {};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = memory_type;

    result = vkAllocateMemory(context.device(), &allocate_info, nullptr, &out_texture.memory);
    if (result != VK_SUCCESS) {
        vkDestroyImage(context.device(), out_texture.image, nullptr);
        out_texture.image = VK_NULL_HANDLE;
        return result_error("vkAllocateMemory", result);
    }

    result = vkBindImageMemory(context.device(), out_texture.image, out_texture.memory, 0);
    if (result != VK_SUCCESS) {
        destroy_texture(out_texture);
        return result_error("vkBindImageMemory", result);
    }

    out_texture.width = width;
    out_texture.height = height;
    return {};
}

Error GuiRenderer::allocate_texture_descriptor(Texture& texture)
{
    VkDescriptorSetAllocateInfo allocate_info {};
    allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate_info.descriptorPool = descriptor_pool_;
    allocate_info.descriptorSetCount = 1;
    allocate_info.pSetLayouts = &descriptor_set_layout_;

    VkResult result = vkAllocateDescriptorSets(device_, &allocate_info, &texture.descriptor_set);
    if (result != VK_SUCCESS) {
        return result_error("vkAllocateDescriptorSets", result);
    }

    VkDescriptorImageInfo image_info {};
    image_info.sampler = sampler_;
    image_info.imageView = texture.image_view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = texture.descriptor_set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_info;

    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    return {};
}

Error GuiRenderer::submit_texture_upload(
    const VulkanContext& context,
    Buffer& staging,
    Texture& texture)
{
    VkCommandPoolCreateInfo pool_info {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = context.graphics_queue_family();

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkResult result = vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool);
    if (result != VK_SUCCESS) {
        return result_error("vkCreateCommandPool", result);
    }

    VkCommandBufferAllocateInfo allocate_info {};
    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.commandPool = command_pool;
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    result = vkAllocateCommandBuffers(device_, &allocate_info, &command_buffer);
    if (result != VK_SUCCESS) {
        vkDestroyCommandPool(device_, command_pool, nullptr);
        return result_error("vkAllocateCommandBuffers", result);
    }

    VkCommandBufferBeginInfo begin_info {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(command_buffer, &begin_info);
    if (result != VK_SUCCESS) {
        vkDestroyCommandPool(device_, command_pool, nullptr);
        return result_error("vkBeginCommandBuffer", result);
    }

    VkImageMemoryBarrier to_transfer {};
    to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.image = texture.image;
    to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_transfer.subresourceRange.levelCount = 1;
    to_transfer.subresourceRange.layerCount = 1;
    to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(
        command_buffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &to_transfer);

    VkBufferImageCopy copy {};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = VkExtent3D {texture.width, texture.height, 1};
    vkCmdCopyBufferToImage(
        command_buffer,
        staging.buffer,
        texture.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copy);

    VkImageMemoryBarrier to_shader {};
    to_shader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_shader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_shader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_shader.image = texture.image;
    to_shader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_shader.subresourceRange.levelCount = 1;
    to_shader.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &to_shader);

    result = vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        vkDestroyCommandPool(device_, command_pool, nullptr);
        return result_error("vkEndCommandBuffer", result);
    }

    VkSubmitInfo submit_info {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    result = vkQueueSubmit(context.graphics_queue(), 1, &submit_info, VK_NULL_HANDLE);
    if (result == VK_SUCCESS) {
        result = vkQueueWaitIdle(context.graphics_queue());
    }
    vkDestroyCommandPool(device_, command_pool, nullptr);

    if (result != VK_SUCCESS) {
        return result_error("texture upload submit/wait", result);
    }

    return {};
}

void GuiRenderer::destroy_texture(Texture& texture) noexcept
{
    if (device_ != VK_NULL_HANDLE) {
        if (texture.image_view != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, texture.image_view, nullptr);
        }
        if (texture.image != VK_NULL_HANDLE) {
            vkDestroyImage(device_, texture.image, nullptr);
        }
        if (texture.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, texture.memory, nullptr);
        }
    }
    texture = {};
}

void GuiRenderer::collect_deferred_textures(std::uint32_t completed_frame_index) noexcept
{
    if (completed_frame_index >= FrameRenderer::max_frames_in_flight) {
        return;
    }

    const std::uint32_t completed_mask = 1U << completed_frame_index;
    auto write = deferred_textures_.begin();
    for (auto read = deferred_textures_.begin(); read != deferred_textures_.end(); ++read) {
        read->pending_frame_mask &= ~completed_mask;
        if (read->pending_frame_mask == 0) {
            destroy_texture(read->texture);
            continue;
        }

        if (write != read) {
            *write = std::move(*read);
        }
        ++write;
    }
    deferred_textures_.erase(write, deferred_textures_.end());
}

void GuiRenderer::mark_texture_used(gui::TextureHandle texture, std::uint32_t frame_index) noexcept
{
    if (frame_index >= FrameRenderer::max_frames_in_flight) {
        return;
    }

    const gui::TextureHandle resolved = is_texture_alive(texture) ? texture : white_texture_;
    if (!is_texture_alive(resolved)) {
        return;
    }

    textures_[static_cast<std::size_t>(resolved.value - 1)].used_frame_mask |= 1U << frame_index;
}

VkDescriptorSet GuiRenderer::descriptor_for(gui::TextureHandle texture) const noexcept
{
    if (is_texture_alive(texture)) {
        return textures_[static_cast<std::size_t>(texture.value - 1)].descriptor_set;
    }
    if (!is_texture_alive(white_texture_)) {
        return VK_NULL_HANDLE;
    }
    return textures_[static_cast<std::size_t>(white_texture_.value - 1)].descriptor_set;
}

} // namespace vlk::vk
