# Техническое задание

## Название

`vlk_be_gui`: Vulkan-based GUI toolkit для C++ desktop-приложений.

## Назначение

Библиотека должна стать полноценным GUI toolkit для написания desktop-приложений на C++: platform layer, event/input handling, layout, набор widgets, текстовый слой, темы/стили, ресурсы и Vulkan renderer.

Основной продуктовый сценарий: современные desktop-приложения для баз данных, таблиц, форм, отчетов, CRM/admin/accounting/operational tools. Интерфейс должен автоматически занимать все доступное пространство окна, размещать элементы через layout/grid систему, поддерживать изменяемые области, кастомизацию и стили.

Текущий MVP фокусируется на нижнем rendering/backend слое: принимает 2D GUI draw lists и выводит их в desktop window через swapchain. Проект должен собираться CMake на Linux и Windows.

## Границы проекта

Входит в первую версию:

- C++20 library.
- CMake build.
- GLFW platform adapter.
- Vulkan renderer backend.
- Один window/swapchain.
- 2D textured triangle pipeline для GUI draw lists.
- Clip rect через Vulkan scissor.
- Alpha blending.
- Resize/swapchain recreation.
- Debug validation layers при наличии.
- Example app.
- CPU unit tests для чистых частей API, где это применимо.

Не входит в первую версию, но входит в стратегическую цель проекта:

- Полный набор GUI widgets для desktop-приложений: buttons, labels, inputs, checkboxes, sliders, menus, tabs, panels, tables, lists, tree views, dialogs.
- Text shaping, IME и accessibility.
- Multi-window/multi-viewport.
- Docking.
- GPU profiling UI.
- Direct3D/OpenGL backends.
- Mobile platforms.

## Целевые платформы

- Linux x86_64.
- Windows x86_64.

Минимальная runtime цель:

- Vulkan loader и driver с поддержкой Vulkan 1.2+.
- `VK_KHR_swapchain`.

## Язык и стандарты

- C++20.
- CMake 3.24+ как минимальная версия проекта, несмотря на локальный CMake 4.3.3.
- Без exceptions policy пока не фиксируется; ошибки Vulkan возвращать через typed status/result, а не через `throw` в hot path.

## Зависимости

Обязательные:

- Vulkan SDK/loader/headers.
- GLFW 3.4+.

Опциональные после MVP:

- Vulkan Memory Allocator.
- FreeType + HarfBuzz для текста.
- Catch2 или doctest для unit-тестов.

## Модули

### `vlk::gui`

Публичный GUI слой. В MVP содержит типы draw data:

- `DrawVertex`.
- `DrawCommand`.
- `DrawList`.
- `TextureHandle`.
- `Rect`.
- `Color`.

После MVP этот модуль должен вырасти в retained/immediate GUI API с layout engine, input routing, focus/navigation, widget state, styling/theme system и стандартной библиотекой widgets для desktop-приложений.

Формат составления интерфейса должен быть спроектирован отдельно и итеративно. Нужно вместе определить, будет ли это immediate API, retained tree, builder DSL или гибридный подход, с главным критерием: интерфейс должен описываться просто, удобно и без лишнего boilerplate для desktop-приложений.

Первичный план верхнего GUI слоя описан в `docs/WIDGET_LAYER.md`.

Приоритет layout API: автоматическое размещение по сеткам, form grids, split panes и full-window application shell. Ручное позиционирование должно быть исключением, а не нормальным способом построения UI.

### `vlk::platform`

GLFW adapter:

- window creation.
- event polling.
- framebuffer size.
- DPI/content scale.
- Vulkan required instance extensions.
- `VkSurfaceKHR` creation.

### `vlk::vk`

Vulkan implementation:

- instance/debug messenger.
- physical device selection.
- logical device and queues.
- swapchain.
- command pools/buffers.
- frame resources.
- descriptor sets.
- pipeline.
- buffer/image resources.
- render frame orchestration.

## Public API требования

Renderer должен поддерживать:

- `initialize`.
- `begin_frame` или `render`.
- `resize`.
- `create_texture`.
- `destroy_texture`.
- `wait_idle`.
- `shutdown`.

API должен избегать передачи сырых Vulkan handles наружу, кроме explicit interop hooks. Для первого этапа interop hooks не обязательны.

## Функциональные требования

1. Приложение создает окно и renderer.
2. Renderer выбирает физическое устройство с graphics+present support.
3. Renderer создает swapchain с подходящим format/present mode/extent.
4. Renderer компилирует/загружает SPIR-V shaders.
5. Renderer принимает draw list за кадр.
6. Renderer загружает vertex/index data в GPU-visible buffers.
7. Renderer применяет scissor для каждого draw command.
8. Renderer выводит результат через `vkQueuePresentKHR`.
9. Renderer корректно пересоздает swapchain при resize/out-of-date.
10. Renderer освобождает Vulkan resources в корректном порядке.

## Нефункциональные требования

- RAII ownership для Vulkan resources.
- Явная обработка ошибок Vulkan.
- No global mutable state в renderer.
- Debug names для Vulkan objects в Debug, если extension доступен.
- Логи ошибок с достаточным контекстом.
- Минимум 2 frames in flight.
- Документированная политика lifetime для draw data: renderer может читать draw list только во время вызова `render`, если не указано иначе.

## Проверка готовности MVP

MVP считается готовым, когда:

- `cmake -S . -B build` проходит.
- `cmake --build build` проходит.
- Example app открывает окно на машине с рабочим Vulkan ICD.
- В окне виден тестовый GUI draw list: несколько цветных прямоугольников, текстурированный quad, clipping/scissor.
- Resize окна не приводит к crash.
- Debug validation не сообщает ошибок на smoke-test.

## Проверка готовности полноценного GUI toolkit

Проект считается готовым как GUI toolkit, когда поверх renderer/backend есть:

- Базовая система приложения: window lifecycle, input events, clipboard, cursor, DPI scaling.
- Layout engine: horizontal/vertical layout, grid, constraints, scroll areas.
- Набор widgets для реальных desktop-приложений: label, button, text input, checkbox, radio, slider, combo box, menu, toolbar, tabs, table/list/tree, modal/dialog.
- Database/report widgets: virtualized data table, searchable/filterable lists, quick search/autosearch, filter bar, form grid, report viewer, resizable panes, status bar, navigation tree.
- Editable database forms: typed fields, validation, dirty state, commit/cancel, read-only/disabled states, field and form errors.
- Fast edit/autosave mode: optional saving without confirmation via commit-on-change, commit-on-focus-lost or commit-on-row-change, with validation and conflict/error handling.
- Data workflow hooks: on_change, on_validate, before/after insert/update/save/delete, save errors and conflict handling.
- Text layer: custom fonts, fallback stacks for languages, icon/symbol fonts, font atlas, Unicode text rendering, alignment, wrapping/no-wrap, ellipsis/clipping, selection/caret, IME path или документированное ограничение.
- Data formatting layer: text, wrapped text, integer, float, decimal, currency, percent/economic values, engineering/scientific notation, date, time, date-time, duration, calendar/date picker formats.
- Styling/theme API: colors, spacing, typography, widget states.
- Resource API: textures, icons, fonts.
- Примеры desktop-приложений, а не только rendering smoke tests.
- Demo-приложение/галерея всех widgets с интерактивной проверкой states, layout, focus, input и styling.

## Ограничения текущей среды

На текущей машине Vulkan loader/driver не создает instance (`ERROR_INCOMPATIBLE_DRIVER`). Поэтому runtime-проверка MVP должна выполняться после исправления драйвера или на другой Linux/Windows машине.
