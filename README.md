# vlk_be_gui

Vulkan-based C++ GUI toolkit для desktop-приложений под Linux и Windows.

Цель проекта - не только renderer backend, а полноценная GUI-библиотека для написания desktop-приложений: окна, input/event loop, layout, виджеты, текст, темы, ресурсы и Vulkan renderer. Текущий MVP строит нижний слой: platform adapter, Vulkan frame pipeline, draw lists, textures и smoke-rendering.

Для полноценного GUI слоя потребуется demo-приложение/галерея со всеми виджетами. API составления интерфейса будет проектироваться итеративно: мы вместе определим, как именно описывать UI, чтобы это было просто, понятно и удобно для реальных desktop-приложений.

## Зависимости

Arch Linux:

```bash
sudo pacman -S --needed cmake gcc vulkan-headers vulkan-icd-loader vulkan-tools vulkan-validation-layers shaderc glslang glfw
```

Для NVIDIA runtime нужен установленный и загруженный драйвер:

```bash
sudo pacman -S --needed nvidia-utils nvidia-open
```

Для AMD RADV runtime:

```bash
sudo pacman -S --needed vulkan-radeon
```

Если обновлялись kernel modules NVIDIA или сам kernel, нужна перезагрузка перед `vulkaninfo --summary`.

## Сборка

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Runtime-проверка Vulkan:

```bash
vulkaninfo --summary
./build/examples/smoke/vlk_be_gui_smoke
```

## UI mockup

Первый визуальный прототип database/report workspace:

```text
docs/mockups/database_workspace.html
```

Файл открывается напрямую в браузере и показывает целевое направление: full-window layout, navigation pane, filters/autosearch, data table, report summary, inspector form, dirty state и save/cancel actions.

## Текущий backend API

MVP-слой пока выглядит как Vulkan backend для GUI draw lists:

```cpp
auto window = vlk::platform::GlfwWindow::create(1280, 720, "app");

vlk::vk::VulkanContext context;
vlk::vk::VulkanContextCreateInfo context_info {
    .application_name = "app",
    .required_instance_extensions = window.required_instance_extensions(),
    .create_surface = [&](VkInstance instance) {
        return window.create_surface(instance);
    },
};
vlk::vk::VulkanContext::create(context_info, context);

vlk::vk::Swapchain swapchain;
vlk::vk::Swapchain::create(context, window.framebuffer_extent(), swapchain);

vlk::vk::FrameRenderer frames;
vlk::vk::FrameRenderer::create(context, swapchain, frames);

vlk::vk::GuiRenderer gui;
vlk::vk::GuiRendererCreateInfo gui_info {
    .vertex_shader_spv_path = "gui.vert.spv",
    .fragment_shader_spv_path = "gui.frag.spv",
};
vlk::vk::GuiRenderer::create(context, swapchain, gui_info, gui);

vlk::vk::FrameRenderer::FrameContext frame;
vlk::vk::FrameRenderer::DrawFrameResult frame_result;
frames.begin_frame(swapchain, {.clear_color = {{0.08F, 0.10F, 0.14F, 1.0F}}}, frame, frame_result);

if (frame_result.status == vlk::vk::FrameRenderer::FrameStatus::ok) {
    gui.draw(context, frame, draw_list);

    vlk::vk::FrameRenderer::EndFrameResult end_result;
    frames.end_frame(context, swapchain, frame, end_result);
}
```

Это не финальный widget API. Верхний слой должен дать простой способ составлять интерфейс из layout и widgets; draw lists останутся низкоуровневым backend contract.
