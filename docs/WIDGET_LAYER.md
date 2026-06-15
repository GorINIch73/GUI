# Widget layer plan

## Goal

`vlk_be_gui` должен стать полноценным C++ GUI toolkit для desktop-приложений. Vulkan renderer, draw lists, textures и swapchain остаются нижним backend layer. Верхний GUI layer должен дать простой способ составлять интерфейс из layout и widgets.

Главный критерий API: интерфейс должен писаться просто, читаться прямо и не требовать лишнего boilerplate для обычных desktop-приложений.

## Предварительное направление API

Выбранное базовое направление: гибридный подход с автоматической layout-системой, ориентированной на desktop-приложения для баз данных, таблиц, форм и отчетов.

- Retained internal state: widget ids, focus, hover/active state, text input state, scroll state, animation/state cache.
- Immediate-style user API: приложение каждый кадр описывает UI через простые вызовы/builder scopes.
- Renderer contract: GUI layer генерирует `DrawList`, backend его рисует.
- Layout-first API: позиции элементов обычно не задаются вручную; элементы автоматически размещаются по grid/layout правилам.
- Full-space application shell: корневой UI должен занимать все окно и автоматически перераспределять место при resize.
- Resizable regions: split panes, sidebars, inspectors, report preview areas, tabbed workspaces.

Такой подход должен позволить писать UI компактно, но при этом сохранить нормальный focus/input/layout state между кадрами. Главный сценарий - бизнес/desktop приложения: базы данных, таблицы, фильтры, карточки записей, отчеты, печатные формы, dashboards.

Пример целевого стиля, не финальный API:

```cpp
ui.window("Settings", [&] {
    ui.column([&] {
        ui.label("Project");
        ui.text_input("Name", project_name);
        ui.checkbox("Enable cache", cache_enabled);

        ui.row([&] {
            if (ui.button("Apply")) {
                apply_settings();
            }
            if (ui.button("Cancel")) {
                close_settings();
            }
        });
    });
});
```

Пример целевого стиля для database/report приложения:

```cpp
ui.app_shell([&] {
    ui.top_bar([&] {
        ui.menu_button("File");
        ui.toolbar_button("Refresh");
        ui.search_box("Search", query);
    });

    ui.workspace_grid([&] {
        ui.left_pane("Navigation", {.width = ui::dp(280), .resizable = true}, [&] {
            ui.tree("Tables", database_tree);
        });

        ui.main_pane([&] {
            ui.tabs(active_tab, [&] {
                ui.tab("Customers", [&] {
                    ui.filter_bar(filters);
                    ui.data_table(customers_table);
                });

                ui.tab("Report", [&] {
                    ui.report_viewer(sales_report);
                });
            });
        });

        ui.right_pane("Inspector", {.width = ui::dp(340), .resizable = true}, [&] {
            ui.form(selected_record);
        });
    });

    ui.status_bar([&] {
        ui.text(status_message);
    });
});
```

## Core systems

### Application shell

Responsibilities:

- Window lifecycle.
- Event pump integration.
- DPI/content scale.
- Frame timing.
- Clipboard hooks.
- Cursor shape.
- Optional high-level `Application` helper over `GlfwWindow` + Vulkan backend.

### Input routing

Needed:

- Mouse position/buttons/wheel.
- Keyboard keys.
- Text input events.
- Modifier state.
- Hover detection.
- Active widget capture.
- Keyboard focus.
- Tab/shift-tab navigation.
- Escape/enter handling.
- Double click timing.

Important API requirement: widgets should not directly query platform state. Platform events should be normalized into a GUI input state per frame.

### Widget identity

Needed:

- Stable widget ids.
- Scope stack for nested layouts/windows.
- Collision handling/debug diagnostics.
- Optional explicit id override.

Likely API:

```cpp
ui.push_id(object_id);
ui.button("Delete");
ui.pop_id();
```

or scoped helper:

```cpp
ui.with_id(object_id, [&] {
    ui.button("Delete");
});
```

### Layout

Layout is a core feature, not a convenience helper. The GUI is intended for applications where most screens are forms, tables, filters and reports. Manual x/y positioning should be rare.

MVP layout primitives:

- `row`.
- `column`.
- `grid`.
- `form_grid`.
- `workspace_grid`.
- `splitter`.
- `resizable_pane`.
- `spacer`.
- `separator`.
- `scroll_area`.
- fixed size.
- fill remaining.
- min/max constraints.
- padding and spacing.

Layout should work in pixels after DPI scaling. Widgets should report desired size, then receive final rect.

Required layout behavior:

- Root UI fills the whole window by default.
- Rows/columns support fixed, auto and fill tracks.
- Grid supports column/row spans.
- Form layout aligns labels and editors automatically.
- Split panes remember user-resized sizes.
- Tables and report views can fill remaining space.
- Layout responds to window resize without per-widget manual positioning.
- Minimum sizes prevent panels from collapsing into unusable states.
- DPI scaling is applied before layout measurement.

Initial track model:

```cpp
ui.grid({
    .columns = {ui::fixed(280), ui::splitter(), ui::fill(), ui::splitter(), ui::fixed(340)},
    .rows = {ui::auto_size(), ui::fill(), ui::auto_size()},
}, [&] {
    // top bar, panes, status bar
});
```

Current implementation status:

- Added `include/vlk_be_gui/layout.hpp`.
- Added pure CPU layout functions for stack and grid.
- Added `fixed`, `auto_size` and `fill` tracks.
- Added item margins and row/column spans.
- Added `vlk_be_gui_layout_tests`.

### Database/report application widgets

The toolkit should prioritize widgets needed by database, admin, accounting, CRM, reporting and operational desktop apps.

High-priority widgets:

- `data_table`: sortable/filterable/resizable columns, row selection, keyboard navigation.
- `data_list`: searchable/filterable list for lookup/reference data.
- `form`: label/editor grid for record editing.
- `filter_bar`: compact filters for tables/lists/reports.
- `quick_search`: automatic search box for tables and lists.
- `report_viewer`: paged report preview, zoom, export/print hooks later.
- `tree`: navigation and hierarchy browsing.
- `tabs`: multiple open tables/reports/forms.
- `splitter` and `resizable_pane`.
- `toolbar` and `status_bar`.
- `search_box`.
- `pagination` or virtual scrolling for large data.
- `calendar`.
- `date_picker`.
- `time_picker`.
- typed field editors for text, decimal, currency, date and time.
- editable fields with validation, dirty state and commit/cancel behavior.

Data widgets should be virtualized early. Large tables/reports must not require geometry for every row when only a viewport is visible.

Tables and lists for database apps must support:

- Built-in quick search/autosearch.
- Column filters.
- Global filter bar.
- Sort by column.
- Multi-column sort later.
- Row selection and multi-select.
- Keyboard navigation.
- Incremental filtering without rebuilding the whole widget tree.
- Virtual scrolling for large datasets.
- Custom cell formatting by data type.
- Optional row actions/context menu.
- Clear empty/loading/error states.
- Inline cell editing.
- Row-level dirty state.
- Commit/cancel editing.

### Editable fields and data binding

Database applications need convenient editing fields, not just visual inputs. Field widgets should understand data type, validation, formatting, dirty state and save/update flow.

Required field behavior:

- Typed editors: text, multiline text, integer, decimal, currency, float, engineering/scientific, date, time, date-time, enum/list, boolean.
- Display format and edit format can differ.
- Parse and validate on input.
- Validate on focus lost.
- Validate before save.
- Dirty state: original value vs edited value.
- Commit/cancel per field.
- Commit/cancel per form.
- Optional fast edit mode: save changes without confirmation.
- Commit-on-change for fields where immediate saving is safe.
- Commit-on-focus-lost for quick database editing.
- Commit-on-row-change for editable tables.
- Required/optional marker.
- Read-only/disabled states.
- Error text and warning text.
- Input masks where useful.
- Min/max and precision constraints for numbers.
- Calendar/date picker integration for date fields.
- Lookup/dropdown search for foreign-key/reference fields.

Proposed form model:

```cpp
ui.form(record, [&] {
    ui.field("Name", record.name)
        .required()
        .max_length(120);

    ui.currency_field("Balance", record.balance)
        .currency("USD")
        .precision(2);

    ui.date_field("Contract date", record.contract_date);

    ui.lookup_field("Customer", record.customer_id)
        .source(customers_lookup)
        .searchable(true);
});
```

### Database triggers/hooks

The GUI layer should support application hooks for database workflows. These are not database engine triggers; they are UI/data-binding lifecycle callbacks around editing, validation, saving and updating.

Required hooks:

- `on_change`: field value changed.
- `on_validate`: field/form validation.
- `before_insert`.
- `after_insert`.
- `before_update`.
- `after_update`.
- `before_save`.
- `after_save`.
- `before_delete`.
- `after_delete`.
- `on_save_error`.
- `on_conflict`.
- `on_reload`.

Required data workflow state:

- New record.
- Existing record.
- Dirty record.
- Saving.
- Save succeeded.
- Save failed.
- Validation failed.
- Conflict/outdated record.
- Deleted record.

Database workflow requirements:

- Hooks can veto save/update/delete by returning validation errors.
- Hooks can transform values before save.
- Forms can show field-level and form-level errors.
- Table rows can show unsaved/invalid states.
- Save operations can be async.
- UI can disable actions while save is in progress.
- Transaction-like batch save flow should be possible at application level.
- Autosave/fast-save mode can save without confirmation when enabled.
- Fast-save mode must still run validation and conflict checks.
- Failed autosave must keep the edited value visible and mark the field/row as failed.
- Fast-save should be configurable per field, form, table, or whole screen.
- Destructive actions such as delete should remain confirmable unless explicitly disabled by the application.

Sketch:

```cpp
ui.form(record)
    .on_validate(validate_customer)
    .before_save([&](auto& draft) {
        draft.updated_at = clock.now();
        return SaveDecision::continue_;
    })
    .after_save([&](const auto& saved) {
        audit_log.record("customer saved", saved.id);
    })
    .on_save_error([&](const SaveError& error) {
        ui.show_error(error.message);
    });
```

Fast edit sketch:

```cpp
ui.data_table(customers)
    .editing({
        .mode = EditMode::cell,
        .save = SaveMode::on_focus_lost,
        .confirm = false,
    })
    .before_update(validate_customer_row)
    .after_update(refresh_totals)
    .on_save_error(show_row_error);
```

### Styling/theme

Needed:

- Color roles: background, panel, text, muted text, accent, danger, border, selection.
- Widget states: normal, hovered, active, disabled, focused.
- Spacing scale.
- Border radius.
- Border width.
- Font roles.

Style should be overrideable by scope:

```cpp
ui.with_style(overrides, [&] {
    ui.button("Danger");
});
```

Modern desktop look requirements:

- Clean, dense, professional UI suitable for repeated daily work.
- Restrained spacing; avoid marketing-style oversized components.
- Strong table/form readability.
- Clear hover/focus/selection states.
- Theme tokens for application, panel, table, form, report and accent roles.
- Per-application style overrides without rewriting widgets.
- Support light and dark themes.

### Text

Milestones:

1. Custom font loading from files and memory.
2. Font fallback stacks for languages/scripts.
3. Icon/symbol font support for toolbar/menu/status glyphs.
4. Font atlas/cache.
5. Basic UTF-8 decoding.
6. Text shaping path for complex scripts.
7. Text measurement.
8. Text draw commands.
9. Text input caret/selection.
10. Text alignment and wrapping modes.
11. FreeType/HarfBuzz path.
12. IME path.

Text is a blocker for production widgets such as labels, inputs, tables and menus.

Required font features:

- Load custom `.ttf`/`.otf` fonts from files or memory.
- Configure font roles: UI, monospace, heading, table, report, icon.
- Configure fallback chains for languages and missing glyphs.
- Support icon fonts or symbol fonts for toolbar/menu/status icons.
- Per-widget font role override.
- DPI-aware font sizing.
- Font atlas rebuild/cache strategy.
- Missing glyph diagnostics.

Required text layout features:

- Horizontal alignment: left, center, right, justify where practical.
- Vertical alignment inside cells/controls: top, center, bottom.
- Single-line text with clipping/ellipsis.
- Multi-line text with word wrap.
- Multi-line text without wrap.
- Explicit newline handling.
- Text measurement for layout.
- Table-cell text alignment independent from column alignment.
- Report text alignment suitable for printed/exported layouts.

### Data formatting

Database/report applications need typed display and editing formats. Formatting should be part of the GUI layer, not hand-coded in every app screen.

Required data formats:

- Plain text.
- Wrapped text.
- Non-wrapped text with ellipsis/clipping.
- Integer.
- Float.
- Decimal/fixed precision.
- Currency.
- Percent/economic values.
- Engineering/scientific notation.
- Date.
- Time.
- Date-time.
- Duration.
- Boolean.
- Enum/list values.

Formatting requirements:

- Locale-aware formatting where possible.
- Custom decimal precision.
- Thousands separators.
- Currency symbol/code placement.
- Negative value styling.
- Null/empty value display policy.
- Parse/format round-trip for editable fields.
- Validation state and error text.
- Alignment defaults by type: text left, numbers right, dates centered or configurable.

Widgets impacted:

- `data_table` column formatters.
- `form` field editors.
- `filter_bar` typed filters.
- `report_viewer` formatted values.
- `text_input` with typed parsing/validation.
- `calendar` and date/time pickers.
- `data_table`, `data_list` and lookup/dropdown search.
- database-bound forms and editable tables with save/update hooks.

Calendar/date widgets:

- Date picker.
- Date range picker.
- Time picker.
- Month/year navigation.
- Locale-aware first day of week.
- Disabled dates/ranges.
- Keyboard navigation.
- Date formatting and parsing.

## Initial widgets

First set:

- `label`.
- `button`.
- `checkbox`.
- `radio`.
- `slider_float`.
- `slider_int`.
- `text_input`.
- `combo_box`.
- `search_box`.
- `separator`.
- `image`.
- `panel`.
- `scroll_area`.
- `toolbar`.
- `status_bar`.
- `splitter`.
- `form_grid`.
- `calendar`.
- `date_picker`.
- `time_picker`.

Second set:

- `menu_bar`.
- `menu`.
- `toolbar`.
- `tabs`.
- `table`.
- `list`.
- `tree`.
- `modal`.
- `dialog`.
- `tooltip`.
- `data_table`.
- `data_list`.
- `report_viewer`.
- `filter_bar`.
- `quick_search`.
- `pagination`.
- advanced calendar/date range picker.

## Widget gallery

Widget gallery/demo app is mandatory.

Every widget must have a gallery page covering:

- Default state.
- Hovered state where practical.
- Active/pressed state where practical.
- Disabled state.
- Focused state.
- Keyboard navigation.
- DPI scaling.
- Long text.
- Narrow layout.
- Theme variations.
- Error/validation state where relevant.
- Dense database-app layout.
- Full-window resize behavior.
- Style/theme variations.

Database/report gallery pages are required:

- Table with many rows and columns.
- Searchable/filterable table.
- Searchable/filterable list.
- Master-detail layout: navigation tree, table, inspector form.
- Report preview layout.
- Filterable table screen.
- Resizable panes and splitters.

The gallery should be the acceptance target for widget work. A widget is not considered complete until it is visible and interactive in the gallery.

## Testing

CPU tests should cover pure logic:

- ID generation and scope stack.
- Layout measurement and placement.
- Input routing.
- Focus navigation.
- Text editing state.
- Font fallback and text measurement.
- Data formatting and parsing.
- Table/list filtering and search matching.
- Field validation, dirty state and commit/cancel transitions.
- Save/update/delete hook ordering.
- Widget state transitions.
- Color packing and geometry generation.

Runtime smoke tests should cover:

- Window creation.
- Frame rendering.
- Resize/minimize.
- Texture upload.
- Basic widget gallery launch.

## Open design decisions

These should be decided together before implementation:

1. Immediate-style API vs retained tree vs builder DSL vs hybrid.
2. How explicit widget ids should be.
3. Whether layout uses flex-like rules, constraints, or a smaller custom model.
4. How styling overrides are scoped.
5. Whether the first text layer starts with a built-in bitmap font or immediately uses FreeType.
6. How much of the platform shell is included in the core library versus examples.
