#pragma once

#include "Keymap.h"
#include "Theme.h"
#include "Widget.h"

#include <ECodeCore/Commands.h>

#include <functional>
#include <string>
#include <string_view>

namespace ecode
{
// A popup menu inside the window, opened at a point.
//
// `Graphics::Menu` is the native menu bar and nothing else — there is no
// `popup(at:)` on any platform eacp supports — so this is drawn by us like the
// rest of the widget tree. It takes the same shape the palette established: a
// child of the root laid out over the whole window rather than a box at the
// click, because `PaintContext` has no notion of a layer escaping its parent's
// clip, and covering the window is also what catches the click that dismisses.
//
// Like the menu bar, it names commands by id and holds no strings of its own —
// title from the registry, shortcut from the keymap, availability from the
// command. The one thing it does not share with `MenuBuilder` is the platform:
// that builds an `NSMenu`, this draws quads.
class ContextMenu final : public Widget
{
public:
    ContextMenu(const ChromeTheme& themeToUse,
                const CommandRegistry& registryToUse,
                const Keymap& keymapToUse);

    // Opens at `at` with these command ids, an empty one being a separator.
    //
    // Ids the registry does not know are dropped rather than shown dead, and a
    // menu that ends up with nothing to offer does not open at all — a popup
    // appearing empty under the pointer reads as a glitch rather than as an
    // answer.
    void show(const eacp::Graphics::Point& at,
              const eacp::Vector<std::string>& commandIds);

    void hide();
    bool isOpen() const { return isVisible(); }

    // The chosen command's id. Routed out rather than run here for the same
    // reason the menu bar's items are: a focused text box may claim it, and
    // this widget has no business knowing that. See WidgetHost::runCommandOnFocus.
    std::function<void(std::string_view)> onCommandChosen = [](std::string_view) {};

    std::function<void()> onClosed = [] {};

    struct Row
    {
        // Empty for a separator, which is also what isSeparator says — kept as
        // a field because the row list is walked far more often than it is
        // built.
        std::string id;
        std::string title;
        std::string shortcut;
        bool isSeparator = false;
    };

    // What the menu is currently offering. Public because it is the honest way
    // to test what a right-click produced without a device.
    const eacp::Vector<Row>& rows() const { return items; }

    // -1 when nothing is highlighted, which is the state a menu opens in: the
    // pointer is at the corner of the box, not on a row, and pre-selecting the
    // first item would mean a stray Return ran something nobody pointed at.
    int highlightedRow() const { return highlighted; }

    bool isRowEnabled(int row) const;

    // Runs the highlighted row, if it is one that can run.
    void acceptHighlighted();

    // It owns the keyboard while it is up, so unlike the palette — whose text
    // field is the focus target — the menu itself is the focus stop.
    bool acceptsFocus() const override { return true; }
    bool wantsMouse() const override { return true; }

    void prepare(eacp::Text::GlyphAtlas& atlas,
                 const eacp::Graphics::Rect& visible) override;
    void paint(PaintContext& context) override;

    void mouseDown(const eacp::Graphics::MouseEvent& event) override;
    void mouseUp(const eacp::Graphics::MouseEvent& event) override;
    void mouseMoved(const eacp::Graphics::MouseEvent& event) override;
    bool keyDown(const eacp::Graphics::KeyEvent& event) override;

    // The box itself, as opposed to this widget's bounds, which are the whole
    // window. Public for the tests and for hit-testing from outside.
    eacp::Graphics::Rect boxBounds() const;

    // Row at a point, or -1. Separators and points outside the box answer -1.
    int rowAt(const eacp::Graphics::Point& point) const;

private:
    // Down and up are both taken so a press-drag-release across the menu picks
    // the row released on, which is how a menu behaves when it is opened by
    // holding the button down.
    void chooseAt(const eacp::Graphics::Point& point);

    // Skips separators and disabled rows, the way a native menu does, and stops
    // at the ends rather than cycling.
    void moveHighlight(int direction);

    eacp::Graphics::Rect rowBounds(int row) const;
    float rowOffset(int row) const;
    float contentHeight() const;

    const ChromeTheme& theme;
    const CommandRegistry& registry;
    const Keymap& keymap;

    eacp::Vector<Row> items;

    // Where show() was asked to put the box. The box itself is placed from this
    // and then pushed back on screen, so reopening at the same point is stable
    // rather than drifting.
    eacp::Graphics::Point anchor;

    // Measured in prepare(), where the atlas is available, and read by paint()
    // and by hit-testing. Safe in that order because the host prepares the whole
    // tree before it paints any of it, and a click cannot land before the first
    // paint.
    float measuredWidth = 0.f;

    int highlighted = -1;
};
} // namespace ecode
