#include <exception>
#include <iostream>
#include <array>

#include <vulkan/vulkan.h>

#include "vlk_be_gui/draw_data.hpp"
#include "vlk_be_gui/frame_renderer.hpp"
#include "vlk_be_gui/glfw_window.hpp"
#include "vlk_be_gui/gui_renderer.hpp"
#include "vlk_be_gui/swapchain.hpp"
#include "vlk_be_gui/vulkan_context.hpp"

#include "triangle_pipeline.hpp"

namespace {

[[nodiscard]] bool recreate_swapchain(
    vlk::platform::GlfwWindow& window,
    const vlk::vk::VulkanContext& context,
    vlk::vk::Swapchain& swapchain,
    vlk::vk::FrameRenderer& renderer)
{
    VkExtent2D extent = window.framebuffer_extent();
    while ((extent.width == 0 || extent.height == 0) && !window.should_close()) {
        window.wait_events();
        extent = window.framebuffer_extent();
    }

    if (window.should_close()) {
        return true;
    }

    context.wait_idle();
    if (vlk::Error error = swapchain.recreate(context, extent)) {
        std::cerr << error.message() << '\n';
        return false;
    }
    if (vlk::Error error = renderer.notify_swapchain_recreated(swapchain)) {
        std::cerr << error.message() << '\n';
        return false;
    }

    return true;
}

} // namespace

int main()
{
    try {
        auto window = vlk::platform::GlfwWindow::create(1280, 720, "vlk_be_gui smoke");

        vlk::vk::VulkanContext context;

        const auto extensions = window.required_instance_extensions();
        vlk::vk::VulkanContextCreateInfo create_info {
            .application_name = "vlk_be_gui smoke",
            .required_instance_extensions = extensions,
            .create_surface = [&](VkInstance instance) {
                return window.create_surface(instance);
            },
        };

        if (vlk::Error error = vlk::vk::VulkanContext::create(create_info, context)) {
            std::cerr << error.message() << '\n';
            return 2;
        }

        vlk::vk::Swapchain swapchain;
        if (vlk::Error error = vlk::vk::Swapchain::create(context, window.framebuffer_extent(), swapchain)) {
            std::cerr << error.message() << '\n';
            return 3;
        }

        vlk::vk::FrameRenderer renderer;
        if (vlk::Error error = vlk::vk::FrameRenderer::create(context, swapchain, renderer)) {
            std::cerr << error.message() << '\n';
            return 4;
        }

        vlk::smoke::TrianglePipeline triangle_pipeline;
        const vlk::smoke::TrianglePipelineCreateInfo triangle_create_info {
            .vertex_shader_spv_path = VLK_BE_GUI_SMOKE_VERT_SPV,
            .fragment_shader_spv_path = VLK_BE_GUI_SMOKE_FRAG_SPV,
        };
        if (vlk::Error error = vlk::smoke::TrianglePipeline::create(
                context,
                swapchain,
                triangle_create_info,
                triangle_pipeline)) {
            std::cerr << error.message() << '\n';
            return 5;
        }

        vlk::vk::GuiRenderer gui_renderer;
        const vlk::vk::GuiRendererCreateInfo gui_create_info {
            .vertex_shader_spv_path = VLK_BE_GUI_SMOKE_GUI_VERT_SPV,
            .fragment_shader_spv_path = VLK_BE_GUI_SMOKE_GUI_FRAG_SPV,
        };
        if (vlk::Error error = vlk::vk::GuiRenderer::create(
                context,
                swapchain,
                gui_create_info,
                gui_renderer)) {
            std::cerr << error.message() << '\n';
            return 6;
        }

        constexpr std::array<std::uint32_t, 16> checker_pixels {
            0xFFFFFFFFU, 0xFF303030U, 0xFFFFFFFFU, 0xFF303030U,
            0xFF303030U, 0xFFFFFFFFU, 0xFF303030U, 0xFFFFFFFFU,
            0xFFFFFFFFU, 0xFF303030U, 0xFFFFFFFFU, 0xFF303030U,
            0xFF303030U, 0xFFFFFFFFU, 0xFF303030U, 0xFFFFFFFFU,
        };
        vlk::gui::TextureHandle checker_texture;
        if (vlk::Error error = gui_renderer.create_texture_rgba8(
                context,
                4,
                4,
                checker_pixels.data(),
                checker_texture)) {
            std::cerr << error.message() << '\n';
            return 7;
        }
        if (!gui_renderer.is_texture_alive(checker_texture)) {
            std::cerr << "created GUI texture is not alive\n";
            return 7;
        }
        vlk::gui::TextureHandle temporary_texture;
        if (vlk::Error error = gui_renderer.create_texture_rgba8(
                context,
                1,
                1,
                checker_pixels.data(),
                temporary_texture)) {
            std::cerr << error.message() << '\n';
            return 7;
        }
        gui_renderer.destroy_texture(temporary_texture);
        if (gui_renderer.is_texture_alive(temporary_texture)) {
            std::cerr << "destroyed GUI texture is still alive\n";
            return 7;
        }
        (void)window.was_resized();

        const std::array gui_vertices {
            vlk::gui::DrawVertex {120.0F, 120.0F, 0.0F, 0.0F, vlk::gui::pack_rgba(242, 86, 86, 230)},
            vlk::gui::DrawVertex {420.0F, 120.0F, 0.0F, 0.0F, vlk::gui::pack_rgba(242, 86, 86, 230)},
            vlk::gui::DrawVertex {420.0F, 300.0F, 0.0F, 0.0F, vlk::gui::pack_rgba(242, 86, 86, 230)},
            vlk::gui::DrawVertex {120.0F, 300.0F, 0.0F, 0.0F, vlk::gui::pack_rgba(242, 86, 86, 230)},
            vlk::gui::DrawVertex {260.0F, 220.0F, 0.0F, 0.0F, vlk::gui::pack_rgba(72, 190, 122, 230)},
            vlk::gui::DrawVertex {620.0F, 220.0F, 1.0F, 0.0F, vlk::gui::pack_rgba(72, 190, 122, 230)},
            vlk::gui::DrawVertex {620.0F, 470.0F, 1.0F, 1.0F, vlk::gui::pack_rgba(72, 190, 122, 230)},
            vlk::gui::DrawVertex {260.0F, 470.0F, 0.0F, 1.0F, vlk::gui::pack_rgba(72, 190, 122, 230)},
        };
        const std::array<std::uint32_t, 12> gui_indices {
            0, 1, 2, 2, 3, 0,
            4, 5, 6, 6, 7, 4,
        };
        const std::array gui_commands {
            vlk::gui::DrawCommand {
                .index_offset = 0,
                .index_count = 6,
                .clip_rect = vlk::gui::Rect {80.0F, 80.0F, 380.0F, 260.0F},
            },
            vlk::gui::DrawCommand {
                .index_offset = 6,
                .index_count = 6,
                .clip_rect = vlk::gui::Rect {300.0F, 260.0F, 260.0F, 170.0F},
                .texture = checker_texture,
            },
        };
        const vlk::gui::DrawList gui_draw_list {
            .vertices = gui_vertices,
            .indices = gui_indices,
            .commands = gui_commands,
        };

        constexpr VkClearColorValue clear_color {{
            0.08F,
            0.10F,
            0.14F,
            1.0F,
        }};

        for (int frame = 0; frame < 120 && !window.should_close(); ++frame) {
            window.poll_events();
            if (window.was_resized()) {
                const VkExtent2D extent = window.framebuffer_extent();
                const VkExtent2D swapchain_extent = swapchain.extent();
                const bool extent_changed = extent.width != swapchain_extent.width
                    || extent.height != swapchain_extent.height;
                if (extent_changed && !recreate_swapchain(window, context, swapchain, renderer)) {
                    context.wait_idle();
                    return 5;
                }
            }

            vlk::vk::FrameRenderer::BeginFrameInfo begin_info {
                .clear_color = clear_color,
            };
            vlk::vk::FrameRenderer::FrameContext frame_context;
            vlk::vk::FrameRenderer::DrawFrameResult frame_result;
            if (vlk::Error error = renderer.begin_frame(swapchain, begin_info, frame_context, frame_result)) {
                std::cerr << error.message() << '\n';
                context.wait_idle();
                return 5;
            }
            if (frame_result.status != vlk::vk::FrameRenderer::FrameStatus::out_of_date
                && frame_result.status != vlk::vk::FrameRenderer::FrameStatus::minimized) {
                triangle_pipeline.draw(frame_context);
                if (vlk::Error error = gui_renderer.draw(context, frame_context, gui_draw_list)) {
                    std::cerr << error.message() << '\n';
                    context.wait_idle();
                    return 7;
                }

                vlk::vk::FrameRenderer::EndFrameResult end_result;
                if (vlk::Error error = renderer.end_frame(context, swapchain, frame_context, end_result)) {
                    std::cerr << error.message() << '\n';
                    context.wait_idle();
                    return 6;
                }
                if (end_result.status != vlk::vk::FrameRenderer::FrameStatus::ok) {
                    frame_result.status = end_result.status;
                }
            }

            if (frame_result.status == vlk::vk::FrameRenderer::FrameStatus::out_of_date
                || frame_result.status == vlk::vk::FrameRenderer::FrameStatus::suboptimal) {
                const VkExtent2D extent = window.framebuffer_extent();
                const VkExtent2D swapchain_extent = swapchain.extent();
                const bool extent_changed = extent.width != swapchain_extent.width
                    || extent.height != swapchain_extent.height;
                if (!extent_changed || frame_result.status == vlk::vk::FrameRenderer::FrameStatus::suboptimal) {
                    break;
                }
                if (!recreate_swapchain(window, context, swapchain, renderer)) {
                    context.wait_idle();
                    return 5;
                }
            }
        }

        context.wait_idle();
        std::cout << "Vulkan textured GUI draw list smoke check passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
