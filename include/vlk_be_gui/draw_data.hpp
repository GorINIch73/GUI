#pragma once

#include <cstdint>
#include <span>

namespace vlk::gui {

struct Rect {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

struct TextureHandle {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return value != 0;
    }
};

struct DrawVertex {
    float x = 0.0F;
    float y = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    std::uint32_t rgba = 0xFFFFFFFFU;
};

struct DrawCommand {
    std::uint32_t index_offset = 0;
    std::uint32_t index_count = 0;
    Rect clip_rect {};
    TextureHandle texture {};
};

struct DrawList {
    std::span<const DrawVertex> vertices {};
    std::span<const std::uint32_t> indices {};
    std::span<const DrawCommand> commands {};
};

[[nodiscard]] constexpr std::uint32_t pack_rgba(
    std::uint8_t r,
    std::uint8_t g,
    std::uint8_t b,
    std::uint8_t a = 255) noexcept
{
    return static_cast<std::uint32_t>(r)
        | (static_cast<std::uint32_t>(g) << 8U)
        | (static_cast<std::uint32_t>(b) << 16U)
        | (static_cast<std::uint32_t>(a) << 24U);
}

} // namespace vlk::gui
