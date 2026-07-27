#pragma once

#include "Theme.h"
#include "Widget.h"

#include <functional>

namespace ecode
{
// A draggable divider between two panes.
//
// Holds a *position* rather than the panes themselves: it reports where the
// divider now is and the parent lays out around it. A splitter that owned its
// two children would have to own their layout as well, and every arrangement in
// this app already has a parent that knows how its own rect is split — the
// window layout takes the sidebar off the left with removeFromLeft and hands
// the rest on.
//
// The divider is drawn thin and grabbed thick. Matching them would mean either
// a 1px target nobody can hit or a 6px line in the middle of the chrome, and
// every editor makes the same split.
class Splitter final : public Widget
{
public:
    enum class Orientation
    {
        // Divides left from right, so it moves horizontally and shows the
        // left-right resize cursor.
        Vertical,

        // Divides top from bottom.
        Horizontal
    };

    Splitter(const ChromeTheme& themeToUse, Orientation orientationToUse);

    // Where the divider sits, in the parent's coordinates — the x of a vertical
    // splitter, the y of a horizontal one. The parent sets this to its starting
    // value and reads it back as the drag moves.
    void setPosition(float position);
    float position() const { return current; }

    // Clamped against on every drag. Set from the parent, which is the only
    // thing that knows how small its panes may usefully get.
    void setLimits(float minimum, float maximum);

    // Fired while dragging, with the new position already clamped. The parent
    // relays out and repaints; the splitter does neither, since it does not
    // know what it is dividing.
    std::function<void(float)> onMoved = [](float) {};

    // The shape the pointer should take right now — resize while the pointer is
    // over the grab band or a drag is in progress, and the arrow otherwise.
    //
    // Reported rather than applied, because setting a cursor needs the
    // Graphics::View that owns the GPU surface and no widget has one. The
    // application asks the widget under the pointer and applies the answer.
    eacp::Graphics::MouseCursor cursor() const override;

    bool wantsMouse() const override { return true; }

    // Not a focus stop. A splitter is a mouse control, and putting it in the Tab
    // order would mean tabbing through the chrome landed on something with no
    // keyboard behaviour and no visible focus.
    bool acceptsFocus() const override { return false; }

    bool isDragging() const { return dragging; }

    // The band that takes the mouse, which is wider than the line that is drawn.
    // In absolute coordinates, like every other widget's bounds.
    eacp::Graphics::Rect grabBounds() const;

    void paint(PaintContext& context) override;

    void mouseDown(const eacp::Graphics::MouseEvent& event) override;
    void mouseDragged(const eacp::Graphics::MouseEvent& event) override;
    void mouseUp(const eacp::Graphics::MouseEvent& event) override;
    void mouseMoved(const eacp::Graphics::MouseEvent& event) override;

    // Wider than the visible line so it can actually be grabbed. VSCode uses
    // about this.
    static constexpr auto grabThickness = 8.f;
    static constexpr auto lineThickness = 1.f;

private:
    float clamp(float value) const;

    const ChromeTheme& theme;
    Orientation orientation;

    float current = 0.f;
    float lowest = 0.f;
    float highest = 0.f;

    // Where in the band the drag started, so the divider does not jump to
    // centre itself under the pointer on the first press.
    float grabOffset = 0.f;

    bool dragging = false;
    bool hovered = false;
};
} // namespace ecode
