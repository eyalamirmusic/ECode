#include "ListView.h"

#include <algorithm>
#include <cmath>

namespace ecode
{
using namespace eacp;

void ListView::setRowCount(std::size_t count)
{
    if (count == rows)
        return;

    rows = count;

    // A selection past the new end would draw a highlight on nothing and
    // report that row to a click handler.
    if (selected >= static_cast<int>(rows))
        selected = rows == 0 ? -1 : static_cast<int>(rows) - 1;

    repaint();
}

void ListView::setRowHeight(float newHeight)
{
    height = std::max(1.f, newHeight);
    repaint();
}

void ListView::setSelectedRow(int row)
{
    const auto clamped = row < 0 || row >= static_cast<int>(rows) ? -1 : row;

    if (clamped == selected)
        return;

    selected = clamped;
    repaint();
}

float ListView::preferredHeight(float) const
{
    return static_cast<float>(rows) * height;
}

Graphics::Rect ListView::boundsOfRow(std::size_t row) const
{
    const auto area = bounds();

    return {area.x, area.y + static_cast<float>(row) * height, area.w, height};
}

std::size_t ListView::firstRowIn(const Graphics::Rect& area) const
{
    if (height <= 0.f)
        return 0;

    const auto above = (area.y - bounds().y) / height;

    // Through a signed type first: a list scrolled so its top is above the
    // viewport gives a negative row, and converting that straight to size_t is
    // undefined. arm64 happens to saturate it to zero, which is the right
    // answer by luck — so this costs nothing and does not depend on the luck.
    const auto row = static_cast<std::ptrdiff_t>(std::floor(above));

    return static_cast<std::size_t>(
        std::clamp<std::ptrdiff_t>(row, 0, static_cast<std::ptrdiff_t>(rows)));
}

std::size_t ListView::lastRowIn(const Graphics::Rect& area) const
{
    if (height <= 0.f)
        return 0;

    const auto below = (area.bottom() - bounds().y) / height;
    const auto row = static_cast<std::ptrdiff_t>(std::ceil(below));

    return static_cast<std::size_t>(
        std::clamp<std::ptrdiff_t>(row, 0, static_cast<std::ptrdiff_t>(rows)));
}

int ListView::rowAt(const Graphics::Point& point) const
{
    if (height <= 0.f || !bounds().contains(point))
        return -1;

    const auto row =
        static_cast<std::ptrdiff_t>((point.y - bounds().y) / height);

    if (row < 0 || row >= static_cast<std::ptrdiff_t>(rows))
        return -1;

    return static_cast<int>(row);
}

void ListView::prepare(Text::GlyphAtlas& atlas, const Graphics::Rect& visible)
{
    const auto first = firstRowIn(visible);
    const auto last = lastRowIn(visible);

    for (auto row = first; row < last; ++row)
        prepareRow(atlas, row);
}

void ListView::paint(PaintContext& context)
{
    // The clip is the visible band: already narrowed to this widget's bounds
    // and to every ancestor's, which for a list inside a ScrollView is the
    // viewport expressed in the list's own coordinates.
    const auto visible = context.clip();

    const auto first = firstRowIn(visible);
    const auto last = lastRowIn(visible);

    for (auto row = first; row < last; ++row)
        paintRow(context,
                 row,
                 boundsOfRow(row),
                 static_cast<int>(row) == selected);
}

void ListView::mouseDown(const Graphics::MouseEvent& event)
{
    const auto row = rowAt(event.pos);

    if (row < 0)
        return;

    setSelectedRow(row);
    onRowClicked(static_cast<std::size_t>(row), event.clickCount);
}

bool ListView::keyDown(const Graphics::KeyEvent& event)
{
    if (rows == 0)
        return false;

    const auto last = static_cast<int>(rows) - 1;

    switch (event.keyCode)
    {
        case Graphics::KeyCode::UpArrow:
            setSelectedRow(std::max(0, selected - 1));
            return true;

        case Graphics::KeyCode::DownArrow:
            // From nothing selected, Down takes the first row rather than the
            // second, which is what -1 + 1 would give by luck rather than by
            // intent.
            setSelectedRow(selected < 0 ? 0 : std::min(last, selected + 1));
            return true;

        case Graphics::KeyCode::Home:
            setSelectedRow(0);
            return true;

        case Graphics::KeyCode::End:
            setSelectedRow(last);
            return true;

        case Graphics::KeyCode::Return:
            if (selected >= 0)
                onRowClicked(static_cast<std::size_t>(selected), 1);

            return true;

        default:
            return false;
    }
}
} // namespace ecode
