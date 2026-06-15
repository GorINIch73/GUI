# Исследование Vulkan backend для desktop GUI

Дата: 2026-06-07.

## Цель

Сделать переносимый C++/CMake Vulkan backend для desktop GUI-приложений под Linux и Windows. Backend должен быть слоем рендеринга и презентации, а не полноценным набором виджетов: он принимает GUI draw data, управляет Vulkan-ресурсами, swapchain, синхронизацией, шейдерами, текстурами и отрисовкой 2D-примитивов.

## Локальная среда

В каталоге проекта исходников пока нет. Локально доступны:

- CMake 4.3.3.
- GCC 16.1.1.
- `pkg-config vulkan`: 1.4.350.
- GLFW: 3.4.0.
- SDL3: 3.4.10.

Ограничение: `vulkaninfo --summary` падает с `Found no drivers` / `ERROR_INCOMPATIBLE_DRIVER`. Значит, на этой машине можно уверенно проверять конфигурацию CMake, компиляцию и unit-тесты CPU-части, но smoke-test с созданием `VkInstance`/swapchain потребует исправленного Vulkan ICD или другой машины.

## Источники и выводы

- CMake `FindVulkan` предоставляет `Vulkan::Vulkan`, `Vulkan::Headers`, `Vulkan::glslc`, `Vulkan::glslangValidator`; переменная `VULKAN_SDK` используется как подсказка SDK. Это позволяет сделать нормальный CMake-поиск Vulkan без ручных путей. Источник: https://cmake.org/cmake/help/latest/module/FindVulkan.html
- Vulkan WSI требует surface от оконной библиотеки. GLFW умеет создавать Vulkan surface через `glfwCreateWindowSurface`, а окно для Vulkan надо создавать без OpenGL context (`GLFW_NO_API`). Источник: https://www.glfw.org/docs/latest/vulkan_guide.html
- SDL3 тоже поддерживает Vulkan surface, но требует окно с `SDL_WINDOW_VULKAN` и instance extensions из `SDL_Vulkan_GetInstanceExtensions`. Источник: https://wiki.libsdl.org/SDL3/SDL_Vulkan_CreateSurface
- Swapchain создается явно; размеры, формат, present mode и image count выбираются из surface capabilities. Источник: https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/01_Presentation/01_Swap_chain.html
- Synchronization в Vulkan является явной ответственностью приложения. Для GUI backend лучше использовать простую схему frames-in-flight, fences для CPU/GPU pacing и semaphores для acquire/render/present. Источник: https://docs.vulkan.org/guide/latest/synchronization.html
- Khronos отдельно предупреждает про повторное использование present wait semaphore; безопасный подход - submit-finished semaphore на swapchain image, а не только на frame-in-flight. Источник: https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html
- Vulkan Memory Allocator упрощает управление памятью Vulkan и имеет MIT-лицензию. Источник: https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator

## Рекомендуемая архитектура

### Слои

1. `platform`
   - Окно, события, DPI, framebuffer size, Vulkan instance extensions, surface creation.
   - Первичная реализация: GLFW, потому что он уже доступен локально, прост для Vulkan WSI и хорошо работает на Linux/Windows.
   - SDL3 оставить как будущий альтернативный adapter, не смешивать его в первый backend.

2. `renderer/vulkan`
   - Instance, debug messenger, physical device selection, logical device, queues.
   - Swapchain lifecycle и resize/recreate.
   - Render pass или dynamic rendering.
   - Command pools/buffers, frame resources, synchronization.
   - Descriptor pools/layouts, pipeline layout, graphics pipeline.
   - Texture upload, sampler, staging buffers.
   - Optional VMA integration после первого working triangle/GUI pass.

3. `gui_backend`
   - Нейтральный API draw list:
     - vertices: position, uv, color.
     - indices.
     - clip rect/scissor.
     - texture id.
   - Backend не знает о конкретных widgets. Его задача - превратить draw list в Vulkan draw calls.

4. `examples`
   - Минимальное desktop app окно.
   - Demo GUI с прямоугольниками, текстурой, scissor и DPI resize.

### Почему не делать сразу полноценный GUI toolkit

Полный GUI toolkit требует layout, input routing, accessibility, text shaping, IME, clipboard, focus model, widgets, theme system. Это отдельный продукт. Для первого этапа лучше построить надежный Vulkan renderer backend, который позже можно подключить к собственному immediate-mode GUI или адаптировать к Dear ImGui-подобным draw lists.

## API направления

Минимальный публичный API:

```cpp
struct DrawVertex {
    float x;
    float y;
    float u;
    float v;
    uint32_t rgba;
};

struct DrawCommand {
    uint32_t index_offset;
    uint32_t index_count;
    Rect clip_rect;
    TextureHandle texture;
};

struct DrawList {
    Span<const DrawVertex> vertices;
    Span<const uint32_t> indices;
    Span<const DrawCommand> commands;
};

class VulkanGuiRenderer {
public:
    bool initialize(const RendererCreateInfo& info);
    void resize(uint32_t width, uint32_t height);
    void render(const DrawList& draw_list);
    void wait_idle();
    void shutdown();
};
```

## Vulkan feature baseline

Базовая версия: Vulkan 1.2 как минимальная цель для совместимости. Если Vulkan 1.3+ доступен, можно включить dynamic rendering и synchronization2, но первый backend должен иметь fallback или явно документированное требование.

Практичный старт:

- Vulkan 1.2 minimum.
- `VK_KHR_surface`.
- Platform surface extensions через GLFW.
- `VK_KHR_swapchain`.
- Validation layers в Debug, если доступны.

## Rendering model

GUI рендерится как 2D overlay/pass:

- Orthographic projection в push constants или uniform buffer.
- Alpha blending: premultiplied или straight alpha, зафиксировать один режим.
- Scissor per draw command для clip rect.
- Один pipeline для textured colored triangles.
- Батчинг по texture id: draw commands уже сгруппированы либо backend минимизирует descriptor rebinding.

## Shader strategy

Первый вариант:

- GLSL shaders в `shaders/`.
- CMake custom commands компилируют GLSL в SPIR-V через `Vulkan::glslc` или `Vulkan::glslangValidator`.
- SPIR-V копируется/встраивается в runtime assets.

Позже можно добавить offline embedding в C++ header, чтобы Windows app не зависело от рабочей директории.

## Memory strategy

Этап 1:

- Ручное Vulkan allocation для vertex/index/staging buffers и image memory.
- Легко отладить и не тащить зависимость до первого working renderer.

Этап 2:

- Подключить VMA для долговременного ресурсооборота, texture uploads, persistent mapped buffers.

## Windowing choice

Первичный adapter: GLFW.

Причины:

- Есть локально.
- Хорошо документирован для Vulkan.
- Поддерживает Windows/Linux, X11/Wayland.
- Дает required instance extensions.

SDL3 оставляем как второй adapter, если нужен более широкий app framework: gamepad/audio/IME/clipboard и т.п.

## Основные риски

- Локальный Vulkan драйвер сейчас нерабочий, runtime validation невозможна до исправления ICD.
- Swapchain recreation и minimization на Windows/Linux требуют аккуратной обработки `VK_ERROR_OUT_OF_DATE_KHR`, `VK_SUBOPTIMAL_KHR`, нулевого framebuffer size.
- Ошибки синхронизации часто не видны без validation sync checks.
- Text rendering и Unicode shaping нельзя качественно закрыть простым ASCII bitmap font; это отдельный этап.
- Multi-window и multi-viewport усложняют ownership swapchain resources. Для первой версии нужен один window/swapchain.

## Решение для первой реализации

Первая версия должна быть узкой и проверяемой:

- Создать CMake-проект.
- GLFW example app.
- Vulkan context + swapchain.
- Один render pass/pipeline.
- Отрисовка переданного draw list с scissor и texture.
- Demo draw list без полной GUI-системы.
- Debug validation в Debug-сборке.

После этого расширять:

- VMA.
- Font atlas.
- Multiple textures.
- Renderer stats.
- SDL3 adapter.
- Multi-window.
