#pragma once

#include <cstdint>

struct ViewPoint
{
    float x = 0.0f;
    float y = 0.0f;
};

enum class ViewMouseButton : uint32_t
{
    None = 0,
    Left = 1u << 0,
    Middle = 1u << 1,
    Right = 1u << 2,
};

using ViewMouseButtons = uint32_t;

struct ViewPointerEvent
{
    ViewPoint position;
    ViewMouseButton button = ViewMouseButton::None;
    ViewMouseButtons buttons = 0;
};

struct ViewScrollEvent
{
    float delta = 0.0f;
};

constexpr ViewMouseButtons toViewMouseButtons(ViewMouseButton button)
{
    return static_cast<ViewMouseButtons>(button);
}

constexpr bool hasViewMouseButton(ViewMouseButtons buttons, ViewMouseButton button)
{
    return (buttons & toViewMouseButtons(button)) != 0;
}
