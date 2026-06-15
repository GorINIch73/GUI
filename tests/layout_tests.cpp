#include <cassert>
#include <array>
#include <cmath>

#include "vlk_be_gui/layout.hpp"

namespace {

[[nodiscard]] bool near(float left, float right)
{
    return std::fabs(left - right) < 0.001F;
}

} // namespace

int main()
{
    {
        const std::array columns {
            vlk::gui::fixed(120.0F),
            vlk::gui::fill(),
            vlk::gui::fixed(80.0F),
        };
        const std::array rows {
            vlk::gui::fixed(30.0F),
            vlk::gui::fill(),
        };
        const std::array items {
            vlk::gui::LayoutItem {.column = 0, .row = 0},
            vlk::gui::LayoutItem {.column = 1, .row = 1},
            vlk::gui::LayoutItem {.column = 2, .row = 0, .row_span = 2},
        };

        const vlk::gui::LayoutResult result = vlk::gui::layout_grid(
            vlk::gui::Rect {.x = 10.0F, .y = 20.0F, .width = 500.0F, .height = 300.0F},
            columns,
            rows,
            items,
            10.0F,
            5.0F);

        assert(result.rects.size() == 3);
        assert(near(result.rects[0].x, 10.0F));
        assert(near(result.rects[0].width, 120.0F));
        assert(near(result.rects[1].x, 140.0F));
        assert(near(result.rects[1].y, 55.0F));
        assert(near(result.rects[1].width, 280.0F));
        assert(near(result.rects[1].height, 265.0F));
        assert(near(result.rects[2].x, 430.0F));
        assert(near(result.rects[2].height, 300.0F));
    }

    {
        const std::array columns {
            vlk::gui::auto_size(),
            vlk::gui::fill(2.0F),
            vlk::gui::fill(1.0F),
        };
        const std::array rows {
            vlk::gui::fixed(40.0F),
        };
        const std::array items {
            vlk::gui::LayoutItem {
                .preferred_size = {.width = 90.0F, .height = 20.0F},
                .min_size = {.width = 60.0F, .height = 20.0F},
                .margin = {.left = 5.0F, .right = 5.0F},
                .column = 0,
                .row = 0,
            },
        };

        const vlk::gui::LayoutResult result = vlk::gui::layout_grid(
            vlk::gui::Rect {.width = 410.0F, .height = 40.0F},
            columns,
            rows,
            items,
            5.0F);

        assert(result.rects.size() == 1);
        assert(near(result.rects[0].x, 5.0F));
        assert(near(result.rects[0].width, 90.0F));
        assert(near(result.used_size.width, 410.0F));
    }

    {
        const std::array items {
            vlk::gui::LayoutItem {.preferred_size = {.width = 40.0F, .height = 10.0F}},
            vlk::gui::LayoutItem {.preferred_size = {.width = 80.0F, .height = 10.0F}},
        };

        const vlk::gui::LayoutResult result = vlk::gui::layout_stack(
            vlk::gui::Axis::horizontal,
            vlk::gui::Rect {.width = 200.0F, .height = 30.0F},
            items,
            4.0F);

        assert(result.rects.size() == 2);
        assert(near(result.rects[0].width, 40.0F));
        assert(near(result.rects[1].x, 44.0F));
        assert(near(result.rects[1].height, 30.0F));
    }

    return 0;
}
