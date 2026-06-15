#include "vlk_be_gui/layout.hpp"

#include <algorithm>
#include <numeric>

namespace vlk::gui {
namespace {

[[nodiscard]] float horizontal_margin(const EdgeInsets& margin) noexcept
{
    return margin.left + margin.right;
}

[[nodiscard]] float vertical_margin(const EdgeInsets& margin) noexcept
{
    return margin.top + margin.bottom;
}

[[nodiscard]] float clamp_track(float value, const Track& track) noexcept
{
    value = std::max(value, track.min);
    if (track.max > 0.0F) {
        value = std::min(value, track.max);
    }
    return value;
}

[[nodiscard]] std::vector<float> resolve_tracks(
    std::span<const Track> tracks,
    std::span<const LayoutItem> items,
    bool horizontal,
    float available_size,
    float spacing)
{
    std::vector<float> sizes(tracks.size(), 0.0F);
    if (tracks.empty()) {
        return sizes;
    }

    float fill_weight = 0.0F;

    for (std::size_t i = 0; i < tracks.size(); ++i) {
        const Track& track = tracks[i];
        switch (track.kind) {
        case TrackKind::fixed:
            sizes[i] = clamp_track(track.value, track);
            break;
        case TrackKind::auto_size:
            sizes[i] = clamp_track(0.0F, track);
            break;
        case TrackKind::fill:
            fill_weight += std::max(track.value, 0.0F);
            sizes[i] = clamp_track(0.0F, track);
            break;
        }
    }

    for (const LayoutItem& item : items) {
        const std::size_t track_index = horizontal ? item.column : item.row;
        const std::size_t span = std::max<std::size_t>(horizontal ? item.column_span : item.row_span, 1);
        if (track_index >= tracks.size() || span != 1) {
            continue;
        }

        const Track& track = tracks[track_index];
        if (track.kind != TrackKind::auto_size) {
            continue;
        }

        const float preferred = horizontal
            ? item.preferred_size.width + horizontal_margin(item.margin)
            : item.preferred_size.height + vertical_margin(item.margin);
        const float minimum = horizontal
            ? item.min_size.width + horizontal_margin(item.margin)
            : item.min_size.height + vertical_margin(item.margin);
        sizes[track_index] = clamp_track(std::max({sizes[track_index], preferred, minimum}), track);
    }

    const float spacing_total = spacing * static_cast<float>(tracks.size() - 1);
    const float used_without_fill = std::accumulate(sizes.begin(), sizes.end(), 0.0F);
    const float remaining = std::max(0.0F, available_size - spacing_total - used_without_fill);

    if (fill_weight > 0.0F && remaining > 0.0F) {
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            const Track& track = tracks[i];
            if (track.kind != TrackKind::fill) {
                continue;
            }

            const float weight = std::max(track.value, 0.0F);
            sizes[i] = clamp_track(sizes[i] + remaining * (weight / fill_weight), track);
        }
    }

    return sizes;
}

[[nodiscard]] float track_offset(std::span<const float> sizes, std::size_t index, float spacing) noexcept
{
    float offset = 0.0F;
    for (std::size_t i = 0; i < index && i < sizes.size(); ++i) {
        offset += sizes[i] + spacing;
    }
    return offset;
}

[[nodiscard]] float span_size(std::span<const float> sizes, std::size_t index, std::size_t span, float spacing) noexcept
{
    if (index >= sizes.size()) {
        return 0.0F;
    }

    const std::size_t end = std::min(sizes.size(), index + std::max<std::size_t>(span, 1));
    float size = 0.0F;
    for (std::size_t i = index; i < end; ++i) {
        size += sizes[i];
    }
    size += spacing * static_cast<float>(end - index - 1);
    return size;
}

} // namespace

LayoutResult layout_stack(Axis axis, Rect bounds, std::span<const LayoutItem> items, float spacing)
{
    std::vector<Track> tracks;
    tracks.reserve(items.size());

    for (const LayoutItem& item : items) {
        const float preferred = axis == Axis::horizontal
            ? item.preferred_size.width + horizontal_margin(item.margin)
            : item.preferred_size.height + vertical_margin(item.margin);
        const float minimum = axis == Axis::horizontal
            ? item.min_size.width + horizontal_margin(item.margin)
            : item.min_size.height + vertical_margin(item.margin);
        tracks.push_back(auto_size(minimum));
        tracks.back().value = preferred;
    }

    std::vector<LayoutItem> indexed_items(items.begin(), items.end());
    for (std::size_t i = 0; i < indexed_items.size(); ++i) {
        if (axis == Axis::horizontal) {
            indexed_items[i].column = i;
            indexed_items[i].row = 0;
        } else {
            indexed_items[i].column = 0;
            indexed_items[i].row = i;
        }
    }

    const std::array single_track {fill()};
    return axis == Axis::horizontal
        ? layout_grid(bounds, tracks, single_track, indexed_items, spacing, 0.0F)
        : layout_grid(bounds, single_track, tracks, indexed_items, 0.0F, spacing);
}

LayoutResult layout_grid(
    Rect bounds,
    std::span<const Track> columns,
    std::span<const Track> rows,
    std::span<const LayoutItem> items,
    float column_spacing,
    float row_spacing)
{
    LayoutResult result;
    result.rects.reserve(items.size());

    const std::vector<float> column_sizes = resolve_tracks(columns, items, true, bounds.width, column_spacing);
    const std::vector<float> row_sizes = resolve_tracks(rows, items, false, bounds.height, row_spacing);

    for (const LayoutItem& item : items) {
        const float x = bounds.x + track_offset(column_sizes, item.column, column_spacing);
        const float y = bounds.y + track_offset(row_sizes, item.row, row_spacing);
        const float width = span_size(column_sizes, item.column, item.column_span, column_spacing);
        const float height = span_size(row_sizes, item.row, item.row_span, row_spacing);

        result.rects.push_back(Rect {
            .x = x + item.margin.left,
            .y = y + item.margin.top,
            .width = std::max(0.0F, width - horizontal_margin(item.margin)),
            .height = std::max(0.0F, height - vertical_margin(item.margin)),
        });
    }

    const float columns_total = std::accumulate(column_sizes.begin(), column_sizes.end(), 0.0F)
        + column_spacing * static_cast<float>(column_sizes.empty() ? 0 : column_sizes.size() - 1);
    const float rows_total = std::accumulate(row_sizes.begin(), row_sizes.end(), 0.0F)
        + row_spacing * static_cast<float>(row_sizes.empty() ? 0 : row_sizes.size() - 1);
    result.used_size = Size {.width = columns_total, .height = rows_total};

    return result;
}

} // namespace vlk::gui
