#include "vlk_be_gui/frame_renderer.hpp"

#include <array>
#include <string>

namespace vlk::vk {

namespace {

[[nodiscard]] std::string vk_result_to_string(VkResult result)
{
    return std::to_string(static_cast<int>(result));
}

[[nodiscard]] Error result_error(const char* operation, VkResult result)
{
    return Error(std::string(operation) + " failed with VkResult " + vk_result_to_string(result));
}

} // namespace

FrameRenderer::FrameRenderer(FrameRenderer&& other) noexcept
    : device_(other.device_)
    , command_pool_(other.command_pool_)
    , command_buffers_(other.command_buffers_)
    , frames_(other.frames_)
    , image_render_finished_(std::move(other.image_render_finished_))
    , image_fences_(std::move(other.image_fences_))
    , current_frame_(other.current_frame_)
{
    other.device_ = VK_NULL_HANDLE;
    other.command_pool_ = VK_NULL_HANDLE;
    other.command_buffers_ = {};
    other.frames_ = {};
    other.current_frame_ = 0;
}

FrameRenderer& FrameRenderer::operator=(FrameRenderer&& other) noexcept
{
    if (this != &other) {
        shutdown();

        device_ = other.device_;
        command_pool_ = other.command_pool_;
        command_buffers_ = other.command_buffers_;
        frames_ = other.frames_;
        image_render_finished_ = std::move(other.image_render_finished_);
        image_fences_ = std::move(other.image_fences_);
        current_frame_ = other.current_frame_;

        other.device_ = VK_NULL_HANDLE;
        other.command_pool_ = VK_NULL_HANDLE;
        other.command_buffers_ = {};
        other.frames_ = {};
        other.current_frame_ = 0;
    }

    return *this;
}

FrameRenderer::~FrameRenderer()
{
    shutdown();
}

Error FrameRenderer::create(
    const VulkanContext& context,
    const Swapchain& swapchain,
    FrameRenderer& out_renderer)
{
    if (context.device() == VK_NULL_HANDLE) {
        return Error("cannot create frame renderer without a Vulkan device");
    }
    if (swapchain.images().empty()) {
        return Error("cannot create frame renderer without swapchain images");
    }

    FrameRenderer renderer;
    renderer.device_ = context.device();
    VkCommandPoolCreateInfo pool_info {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = context.graphics_queue_family();

    VkResult result = vkCreateCommandPool(context.device(), &pool_info, nullptr, &renderer.command_pool_);
    if (result != VK_SUCCESS) {
        return result_error("vkCreateCommandPool", result);
    }

    if (Error error = allocate_command_buffers(renderer)) {
        return error;
    }
    if (Error error = create_sync_objects(renderer)) {
        return error;
    }
    if (Error error = create_image_sync_objects(renderer, swapchain)) {
        return error;
    }

    out_renderer = std::move(renderer);
    return {};
}

Error FrameRenderer::notify_swapchain_recreated(const Swapchain& swapchain)
{
    if (swapchain.images().empty()) {
        return Error("cannot update frame renderer for a swapchain without images");
    }

    destroy_image_sync_objects();
    return create_image_sync_objects(*this, swapchain);
}

Error FrameRenderer::begin_frame(
    const Swapchain& swapchain,
    const BeginFrameInfo& begin_info,
    FrameContext& out_frame,
    DrawFrameResult& out_result)
{
    out_frame = {};
    out_result = {};

    if (device_ == VK_NULL_HANDLE || command_pool_ == VK_NULL_HANDLE) {
        return Error("frame renderer is not initialized");
    }

    FrameSync& frame = frames_[current_frame_];

    VkResult result = vkWaitForFences(device_, 1, &frame.in_flight, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        return result_error("vkWaitForFences", result);
    }

    std::uint32_t image_index = 0;
    result = vkAcquireNextImageKHR(
        device_,
        swapchain.handle(),
        UINT64_MAX,
        frame.image_available,
        VK_NULL_HANDLE,
        &image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        out_result.status = FrameStatus::out_of_date;
        return {};
    }
    const bool acquired_suboptimal = result == VK_SUBOPTIMAL_KHR;
    if (result != VK_SUCCESS && !acquired_suboptimal) {
        return result_error("vkAcquireNextImageKHR", result);
    }
    if (acquired_suboptimal) {
        out_result.status = FrameStatus::suboptimal;
    }
    if (image_index >= image_fences_.size()) {
        return Error("swapchain returned an image index outside the tracked frame state");
    }

    if (image_fences_[image_index] != VK_NULL_HANDLE) {
        result = vkWaitForFences(device_, 1, &image_fences_[image_index], VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS) {
            return result_error("vkWaitForFences", result);
        }
    }
    image_fences_[image_index] = frame.in_flight;

    result = vkResetCommandBuffer(command_buffers_[current_frame_], 0);
    if (result != VK_SUCCESS) {
        return result_error("vkResetCommandBuffer", result);
    }

    if (Error error = begin_clear_commands(swapchain, image_index, begin_info.clear_color)) {
        return error;
    }

    out_frame.command_buffer = command_buffers_[current_frame_];
    out_frame.image_index = image_index;
    out_frame.frame_index = current_frame_;
    out_frame.extent = swapchain.extent();
    out_frame.image_format = swapchain.image_format();
    return {};
}

Error FrameRenderer::end_frame(
    const VulkanContext& context,
    const Swapchain& swapchain,
    const FrameContext& frame_context,
    EndFrameResult& out_result)
{
    out_result = {};

    if (device_ == VK_NULL_HANDLE || command_pool_ == VK_NULL_HANDLE) {
        return Error("frame renderer is not initialized");
    }
    if (frame_context.command_buffer != command_buffers_[current_frame_]) {
        return Error("frame context does not match the current frame renderer command buffer");
    }
    if (frame_context.image_index >= image_render_finished_.size()) {
        return Error("frame context image index is outside the tracked frame state");
    }

    if (Error error = end_clear_commands(swapchain, frame_context.image_index)) {
        return error;
    }

    FrameSync& frame = frames_[current_frame_];
    VkResult result = vkResetFences(device_, 1, &frame.in_flight);
    if (result != VK_SUCCESS) {
        return result_error("vkResetFences", result);
    }

    constexpr VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &frame.image_available;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffers_[current_frame_];
    VkSemaphore& render_finished = image_render_finished_[frame_context.image_index];
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_finished;

    result = vkQueueSubmit(context.graphics_queue(), 1, &submit_info, frame.in_flight);
    if (result != VK_SUCCESS) {
        return result_error("vkQueueSubmit", result);
    }

    const VkSwapchainKHR swapchain_handle = swapchain.handle();
    VkPresentInfoKHR present_info {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_finished;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain_handle;
    present_info.pImageIndices = &frame_context.image_index;

    result = vkQueuePresentKHR(context.present_queue(), &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        out_result.status = FrameStatus::out_of_date;
        current_frame_ = (current_frame_ + 1) % max_frames_in_flight;
        return {};
    }
    if (result == VK_SUBOPTIMAL_KHR) {
        out_result.status = FrameStatus::suboptimal;
        current_frame_ = (current_frame_ + 1) % max_frames_in_flight;
        return {};
    }
    if (result != VK_SUCCESS) {
        return result_error("vkQueuePresentKHR", result);
    }

    current_frame_ = (current_frame_ + 1) % max_frames_in_flight;
    return {};
}

Error FrameRenderer::draw_clear_frame(
    const VulkanContext& context,
    const Swapchain& swapchain,
    VkClearColorValue clear_color,
    DrawFrameResult& out_result)
{
    BeginFrameInfo begin_info {
        .clear_color = clear_color,
    };
    FrameContext frame_context;
    if (Error error = begin_frame(swapchain, begin_info, frame_context, out_result)) {
        return error;
    }
    if (out_result.status == FrameStatus::out_of_date || out_result.status == FrameStatus::minimized) {
        return {};
    }

    EndFrameResult end_result;
    if (Error error = end_frame(context, swapchain, frame_context, end_result)) {
        return error;
    }
    if (end_result.status != FrameStatus::ok) {
        out_result.status = end_result.status;
    }
    return {};
}

void FrameRenderer::shutdown() noexcept
{
    if (device_ != VK_NULL_HANDLE) {
        for (FrameSync& frame : frames_) {
            if (frame.image_available != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, frame.image_available, nullptr);
                frame.image_available = VK_NULL_HANDLE;
            }
            if (frame.in_flight != VK_NULL_HANDLE) {
                vkDestroyFence(device_, frame.in_flight, nullptr);
                frame.in_flight = VK_NULL_HANDLE;
            }
        }
        destroy_image_sync_objects();

        if (command_pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, command_pool_, nullptr);
            command_pool_ = VK_NULL_HANDLE;
        }
    }

    device_ = VK_NULL_HANDLE;
    command_buffers_ = {};
    image_fences_.clear();
    current_frame_ = 0;
}

Error FrameRenderer::create_sync_objects(FrameRenderer& renderer)
{
    VkSemaphoreCreateInfo semaphore_info {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (FrameSync& frame : renderer.frames_) {
        VkResult result = vkCreateSemaphore(
            renderer.device_,
            &semaphore_info,
            nullptr,
            &frame.image_available);
        if (result != VK_SUCCESS) {
            return result_error("vkCreateSemaphore", result);
        }

        result = vkCreateFence(renderer.device_, &fence_info, nullptr, &frame.in_flight);
        if (result != VK_SUCCESS) {
            return result_error("vkCreateFence", result);
        }
    }

    return {};
}

Error FrameRenderer::create_image_sync_objects(FrameRenderer& renderer, const Swapchain& swapchain)
{
    VkSemaphoreCreateInfo semaphore_info {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    renderer.image_render_finished_.resize(swapchain.images().size(), VK_NULL_HANDLE);
    renderer.image_fences_.assign(swapchain.images().size(), VK_NULL_HANDLE);

    for (VkSemaphore& semaphore : renderer.image_render_finished_) {
        const VkResult result = vkCreateSemaphore(
            renderer.device_,
            &semaphore_info,
            nullptr,
            &semaphore);
        if (result != VK_SUCCESS) {
            return result_error("vkCreateSemaphore", result);
        }
    }

    return {};
}

Error FrameRenderer::allocate_command_buffers(FrameRenderer& renderer)
{
    VkCommandBufferAllocateInfo allocate_info {};
    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.commandPool = renderer.command_pool_;
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = static_cast<std::uint32_t>(renderer.command_buffers_.size());

    const VkResult result = vkAllocateCommandBuffers(
        renderer.device_,
        &allocate_info,
        renderer.command_buffers_.data());
    if (result != VK_SUCCESS) {
        return result_error("vkAllocateCommandBuffers", result);
    }

    return {};
}

void FrameRenderer::destroy_image_sync_objects() noexcept
{
    if (device_ != VK_NULL_HANDLE) {
        for (VkSemaphore semaphore : image_render_finished_) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, semaphore, nullptr);
            }
        }
    }

    image_render_finished_.clear();
    image_fences_.clear();
}

Error FrameRenderer::begin_clear_commands(
    const Swapchain& swapchain,
    std::uint32_t image_index,
    VkClearColorValue clear_color)
{
    const VkCommandBuffer command_buffer = command_buffers_[current_frame_];

    VkCommandBufferBeginInfo begin_info {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult result = vkBeginCommandBuffer(command_buffer, &begin_info);
    if (result != VK_SUCCESS) {
        return result_error("vkBeginCommandBuffer", result);
    }

    VkImageMemoryBarrier to_color_attachment {};
    to_color_attachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_color_attachment.srcAccessMask = 0;
    to_color_attachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    to_color_attachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_color_attachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_color_attachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_color_attachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_color_attachment.image = swapchain.images()[image_index];
    to_color_attachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_color_attachment.subresourceRange.baseMipLevel = 0;
    to_color_attachment.subresourceRange.levelCount = 1;
    to_color_attachment.subresourceRange.baseArrayLayer = 0;
    to_color_attachment.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        command_buffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &to_color_attachment);

    VkRenderingAttachmentInfo color_attachment {};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView = swapchain.image_views()[image_index];
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clearValue.color = clear_color;

    VkRenderingInfo rendering_info {};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.offset = VkOffset2D {0, 0};
    rendering_info.renderArea.extent = swapchain.extent();
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &color_attachment;

    vkCmdBeginRendering(command_buffer, &rendering_info);
    return {};
}

Error FrameRenderer::end_clear_commands(
    const Swapchain& swapchain,
    std::uint32_t image_index)
{
    const VkCommandBuffer command_buffer = command_buffers_[current_frame_];

    vkCmdEndRendering(command_buffer);

    VkImageMemoryBarrier to_present {};
    to_present.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_present.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    to_present.dstAccessMask = 0;
    to_present.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_present.image = swapchain.images()[image_index];
    to_present.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_present.subresourceRange.baseMipLevel = 0;
    to_present.subresourceRange.levelCount = 1;
    to_present.subresourceRange.baseArrayLayer = 0;
    to_present.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        command_buffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &to_present);

    const VkResult result = vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        return result_error("vkEndCommandBuffer", result);
    }

    return {};
}

} // namespace vlk::vk
