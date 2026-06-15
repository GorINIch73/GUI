# План реализации

Стратегическая цель проекта: полноценный C++ GUI toolkit для desktop-приложений, а не только Vulkan renderer backend. Renderer/draw-list слой ниже является MVP-фундаментом; последующие этапы должны добавить input routing, layout, widgets, text, styling/themes и application-level API.

Основной сценарий: современные desktop-приложения для баз данных, таблиц, форм и отчетов. UI должен автоматически занимать все пространство окна, размещать элементы через grid/layout систему, поддерживать resizable panes, таблицы, отчеты, кастомизацию и стили.

Для widget layer обязательно нужно отдельное demo-приложение/галерея всех виджетов. Формат составления интерфейса будет определяться итеративно вместе: immediate API, retained tree, builder DSL или гибридный подход нужно выбрать по простоте, удобству и пригодности для реальных desktop-приложений.

## Этап 0. Подготовка проекта

Результат:

- `CMakeLists.txt`.
- `src/`, `include/`, `examples/`, `shaders/`, `tests/`.
- C++20 target.
- Поиск Vulkan и GLFW через CMake/pkg-config fallback.
- Настройки warnings.

Проверка:

- Конфигурация CMake проходит на Linux.
- Пустая библиотека и example target собираются.

## Этап 1. Platform adapter

Результат:

- GLFW window wrapper.
- Event loop.
- Framebuffer size и resize flag.
- Required Vulkan instance extensions.
- Surface creation callback/API.

Проверка:

- Example создает GLFW window без OpenGL context.
- Без Vulkan renderer пока можно открыть/закрыть окно.

## Этап 2. Vulkan context

Результат:

- Instance.
- Debug messenger в Debug.
- Surface.
- Physical device selection.
- Logical device.
- Graphics/present queue discovery.

Проверка:

- На машине с рабочим ICD создается `VkInstance`, `VkDevice`, surface.
- При отсутствии драйвера ошибка понятна и не приводит к UB/crash.

## Этап 3. Swapchain

Результат:

- Query surface capabilities.
- Выбор surface format, present mode, extent.
- Swapchain images/views.
- Recreate при resize/out-of-date.
- Обработка minimized window с нулевым framebuffer size.

Проверка:

- Swapchain создается и пересоздается.
- Resize и minimize не ломают приложение.

## Этап 4. Frame resources и синхронизация

Результат:

- 2 frames in flight.
- Command pools/buffers.
- Fences.
- Acquire semaphores.
- Submit/present semaphores с учетом безопасного reuse на swapchain image.
- `begin_frame`/`end_frame`.

Проверка:

- Можно clear screen каждый кадр.
- Validation layers не ругаются на sync на smoke-test.

## Этап 5. Shaders и pipeline

Результат:

- Vertex/fragment GLSL.
- CMake shader compilation to SPIR-V.
- Pipeline layout.
- Render pass или dynamic rendering.
- Alpha blending.
- Orthographic projection.

Проверка:

- Example рисует один hardcoded triangle/quad поверх clear color.

## Этап 6. GUI draw list renderer

Результат:

- Public draw list structs.
- Dynamic vertex/index buffers.
- Upload per frame.
- Draw commands.
- Scissor rectangles.
- Texture binding API.

Проверка:

- Example рисует несколько прямоугольников.
- Clip rect визуально работает.
- Resize сохраняет корректную проекцию.

## Этап 7. Textures

Результат:

- Texture creation from RGBA8 pixels.
- Staging upload.
- Image layout transitions.
- Sampler.
- Descriptor set management.
- White texture fallback.

Проверка:

- Example рисует textured quad.
- Несколько draw commands могут использовать разные texture handles.

## Этап 8. Public API polish

Результат:

- Заголовки в `include/vlk_be_gui/`.
- Ошибки через result/status.
- RAII cleanup.
- Логи.
- README с build/run инструкциями.

Проверка:

- API можно использовать из example без доступа к internal headers.
- Shutdown проходит без validation errors.

## Этап 9. Tests и CI-ready структура

Результат:

- Unit tests для чистых функций: выбор formats/present mode/extent, draw list validation, color packing.
- Smoke-test target, который можно включить только при рабочем Vulkan runtime.
- Windows build notes.

Проверка:

- `ctest` проходит для CPU tests.
- Vulkan smoke-test можно запускать вручную.

## Этап 10. Расширения после MVP

Кандидаты:

- Application/widget layer.
- Layout engine.
- Full-window application shell.
- Grid/form/workspace layout.
- Resizable panes and splitters.
- Database/report widgets: data table, searchable/filterable lists, quick search/autosearch, filter bar, report viewer, form grid.
- Editable database forms: typed fields, validation, dirty state, commit/cancel.
- Fast edit/autosave mode without confirmation, with validation/error/conflict states.
- Data workflow hooks: on_change, validation, before/after save/update/delete, error/conflict handling.
- Standard widgets: buttons, labels, text inputs, checkboxes, sliders, menus, tabs, tables, lists, tree views, dialogs.
- Input routing, focus/navigation, clipboard.
- Styling/theme system.
- Custom font loading, fallback stacks and icon fonts.
- Text alignment, wrapping/no-wrap and ellipsis.
- Data formatting layer: text, numbers, currency, economic values, engineering/scientific notation, date/time/calendar.
- Widget gallery/demo app for every widget and state.
- VMA.
- Font atlas.
- FreeType/HarfBuzz text layer.
- SDL3 adapter.
- Multi-window.
- RenderDoc markers.
- GPU timing.
- Pipeline cache.
- Embedded SPIR-V.

## Ближайшая реализация

Выполнено:

1. Создать CMake skeleton.
2. Добавить GLFW-only example.
3. Добавить `include/vlk_be_gui/draw_data.hpp`.
4. Добавить internal Vulkan RAII helpers.
5. Реализовать context creation до device level.
6. Сделать graceful error path для текущей машины без Vulkan driver.
7. Проверить Vulkan runtime после перезагрузки через `vulkaninfo --summary`.
8. Добавить базовый swapchain RAII wrapper: query support, format/present mode/extent selection, images/views.
9. Расширить smoke example до создания Vulkan context + swapchain.
10. Добавить graphics/present queue handles в `VulkanContext`.
11. Добавить frame resources: command pool/buffers, fences, acquire semaphores и render-finished semaphores per swapchain image.
12. Реализовать minimal clear frame path через dynamic rendering.
13. Добавить smoke-loop с обработкой resize, minimized framebuffer `0x0`, `VK_ERROR_OUT_OF_DATE_KHR` и swapchain recreate при реальном изменении extent.
14. Разделить frame API на `begin_frame`/`end_frame`; оставить `draw_clear_frame` wrapper для простого smoke/use case.
15. Добавить CMake shader compilation через `glslc`.
16. Добавить первый dynamic rendering graphics pipeline и hardcoded triangle smoke path.
17. Добавить GUI draw list renderer: dynamic per-frame vertex/index buffers, pixel projection, alpha blending, scissor per draw command.
18. Добавить texture binding API: RGBA8 upload через staging buffer, descriptor sets, sampler и white texture fallback.
19. Заменить boolean swapchain state на явный `FrameStatus`: `ok`, `suboptimal`, `out_of_date`, `minimized`.
20. Добавить texture lifecycle API: `destroy_texture(TextureHandle)` и `is_texture_alive(TextureHandle)`.
21. Добавить deferred texture destruction queue на основе frames-in-flight, чтобы `destroy_texture` не требовал ручного `wait_idle`.
22. Вынести demo-only triangle pipeline из публичной библиотеки в `examples/smoke`.
23. Добавить README usage example для текущего backend API.
24. Добавить первый widget-layer plan в `docs/WIDGET_LAYER.md`: input routing, layout, widgets, text, styling, gallery, tests.
25. Добавить первый HTML mockup database/report workspace в `docs/mockups/database_workspace.html`.
26. Добавить первый CPU-tested layout foundation: `layout.hpp`, stack/grid layout, `fixed`/`auto_size`/`fill` tracks, margins, spans и отдельные layout tests.

Следующий coding pass:

1. Добавить базовый `UiContext`: frame lifecycle, input snapshot, id stack, root bounds, layout scope stack.
2. Добавить первые primitive widgets поверх layout foundation: label/button placeholder geometry и draw-list emission.
3. Добавить widget gallery/demo app как обязательный acceptance target для каждого нового widget.
4. Спроектировать font/text/data-format API для custom fonts, icon fonts, alignment, wrapping, currencies, decimals, dates/times/calendars.
5. Спроектировать data table/list API с автопоиском, фильтрами, сортировкой и virtual scrolling.
6. Спроектировать database form/edit API: typed fields, dirty state, validation, commit/cancel, autosave/fast-save, save/update hooks.
