#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "vlk_be_gui/draw_data.hpp"

namespace vlk::gui {

struct Size {
    float width = 0.0F;
    float height = 0.0F;
};

struct EdgeInsets {
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;
};

enum class Axis {
    horizontal,
    vertical,
};

enum class TrackKind {
    fixed,
    auto_size,
    fill,
};

struct Track {
    TrackKind kind = TrackKind::auto_size;
    float value = 0.0F;
    float min = 0.0F;
    float max = 0.0F;
};

struct LayoutItem {
    Size preferred_size {};
    Size min_size {};
    EdgeInsets margin {};
    std::size_t column = 0;
    std::size_t row = 0;
    std::size_t column_span = 1;
    std::size_t row_span = 1;
};

struct LayoutResult {
    std::vector<Rect> rects {};
    Size used_size {};
};

[[nodiscard]] constexpr Track fixed(float value) noexcept
{
    return Track {.kind = TrackKind::fixed, .value = value};
}

[[nodiscard]] constexpr Track auto_size(float min = 0.0F, float max = 0.0F) noexcept
{
    return Track {.kind = TrackKind::auto_size, .min = min, .max = max};
}

[[nodiscard]] constexpr Track fill(float weight = 1.0F, float min = 0.0F, float max = 0.0F) noexcept
{
    return Track {.kind = TrackKind::fill, .value = weight, .min = min, .max = max};
}

[[nodiscard]] LayoutResult layout_stack(
    Axis axis,
    Rect bounds,
    std::span<const LayoutItem> items,
    float spacing = 0.0F);

[[nodiscard]] LayoutResult layout_grid(
    Rect bounds,
    std::span<const Track> columns,
    std::span<const Track> rows,
    std::span<const LayoutItem> items,
    float column_spacing = 0.0F,
    float row_spacing = 0.0F);

} // namespace vlk::gui
