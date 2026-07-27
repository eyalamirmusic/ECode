#include "Splitter.h"

#include <algorithm>

namespace ecode
{
using namespace eacp;

Splitter::Splitter(const ChromeTheme& themeToUse, Orientation orientationToUse)
    : theme(themeToUse)
    , orientation(orientationToUse)
{
}

bool isVertical(Splitter::Orientation orientation)
{
    return orientation == Splitter::Orientation::Vertical;
}

float Splitter::clamp(float value) const
{
    // Guards the case where the window is too small for the limits to make
    // sense — std::clamp is undefined when the bounds cross, and a window
    // dragged narrower than the sidebar's minimum gets there easily.
    if (highest < lowest)
        return lowest;

    return std::clamp(value, lowest, highest);
}

void Splitter::setPosition(float position)
{
    current = clamp(position);
}

void Splitter::setLimits(float minimum, float maximum)
{
    lowest = minimum;
    highest = maximum;

    // Re-clamped rather than left alone, so a window resize that tightens the
    // limits pulls the divider back in with it instead of leaving it stranded
    // outside them until the next drag.
    current = clamp(current);
}

Graphics::Rect Splitter::grabBounds() const
{
    const auto area = bounds();

    // Centred on the line rather than starting at it, so the band is as easy to
    // hit from the pane on either side.
    if (isVertical(orientation))
        return {
            area.x + (area.w - grabThickness) * 0.5f, area.y, grabThickness, area.h};

    return {area.x, area.y + (area.h - grabThickness) * 0.5f, area.w, grabThickness};
}

Graphics::MouseCursor Splitter::cursor() const
{
    // While dragging the shape stays regardless of where the pointer has got
    // to; the host asks the captured widget, so this is reached with the
    // pointer well outside the band.
    if (!dragging && !hovered)
        return Graphics::MouseCursor::Default;

    return isVertical(orientation) ? Graphics::MouseCursor::ResizeLeftRight
                                   : Graphics::MouseCursor::ResizeUpDown;
}

void Splitter::paint(PaintContext& context)
{
    const auto area = bounds();

    // The line only, not the grab band. A divider drawn as thick as it is
    // grabbable is a bar through the middle of the chrome.
    const auto line = isVertical(orientation)
                          ? Graphics::Rect {area.x + (area.w - lineThickness) * 0.5f,
                                            area.y,
                                            lineThickness,
                                            area.h}
                          : Graphics::Rect {area.x,
                                            area.y + (area.h - lineThickness) * 0.5f,
                                            area.w,
                                            lineThickness};

    const auto lit = dragging || hovered;

    context.sprites().fillRect(line, lit ? theme.splitterActive : theme.splitter);
}

void Splitter::mouseDown(const Graphics::MouseEvent& event)
{
    dragging = true;

    // The divider keeps its distance from the pointer rather than snapping its
    // centre to it. Grabbing a band 8 points wide and having the line jump up
    // to 4 points sideways is a visible twitch on every single drag.
    grabOffset = (isVertical(orientation) ? event.pos.x : event.pos.y) - current;

    repaint();
}

void Splitter::mouseDragged(const Graphics::MouseEvent& event)
{
    if (!dragging)
        return;

    const auto along = isVertical(orientation) ? event.pos.x : event.pos.y;
    const auto moved = clamp(along - grabOffset);

    if (moved == current)
        return;

    current = moved;
    onMoved(current);
}

void Splitter::mouseUp(const Graphics::MouseEvent& event)
{
    dragging = false;

    // The pointer may have been released outside the band, in which case the
    // splitter is no longer under it and should stop looking active.
    hovered = grabBounds().contains(event.pos);

    repaint();
}

void Splitter::mouseMoved(const Graphics::MouseEvent& event)
{
    const auto inside = grabBounds().contains(event.pos);

    if (inside == hovered)
        return;

    hovered = inside;
    repaint();
}
} // namespace ecode
