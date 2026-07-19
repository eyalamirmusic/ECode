#pragma once

#include "Widget.h"

namespace ecode
{
// Owns the root of a widget tree and routes input into it.
//
// Separate from the GPUView on purpose: everything here is plain logic over
// rectangles, so routing, capture and focus traversal can be tested without a
// device or an event loop. The view forwards its events and does nothing else.
class WidgetHost
{
public:
    void setRoot(Widget& newRoot);
    Widget* root() const { return rootWidget; }

    // Resizes the root, which lays out the tree beneath it.
    void setBounds(const eacp::Graphics::Rect& bounds);

    void prepare(eacp::Text::GlyphAtlas& atlas);
    void paint(PaintContext& context);

    // --- input -----------------------------------------------------------
    //
    // Down latches the widget under the pointer, and every Drag and Up goes to
    // it until the button is released — so a selection drag that leaves the
    // editor keeps extending the selection rather than being handed to whatever
    // it passed over. This mirrors what eacp already does one level up.

    void mouseDown(const eacp::Graphics::MouseEvent& event);
    void mouseDragged(const eacp::Graphics::MouseEvent& event);
    void mouseUp(const eacp::Graphics::MouseEvent& event);
    void mouseMoved(const eacp::Graphics::MouseEvent& event);

    // Wheel deliberately ignores the capture and goes to the widget under the
    // pointer, which is what the platform does and what feels right when a
    // drag is in progress somewhere else. Bubbles to ancestors until consumed.
    bool mouseWheel(const eacp::Graphics::MouseEvent& event);

    // To the focused widget, then up its ancestors. False when nothing took it,
    // which lets the caller fall back to application shortcuts.
    bool keyDown(const eacp::Graphics::KeyEvent& event);

    // True when a focused text box claimed the command for itself, so the
    // application should not also run it.
    //
    // The same precedence keyDown applies to ⌘C and ⌘V, asked about a command
    // id instead of a keystroke — because a menu shortcut never becomes a key
    // event. macOS matches key equivalents against the menu bar before the
    // window sees anything, so with Paste in the Edit menu a ⌘V that used to
    // reach a focused find field arrives as "edit.paste" instead. Without this,
    // it would paste into the document with the caret visibly in the find box.
    bool runCommandOnFocus(std::string_view id);

    // The shape the pointer should take at this point.
    //
    // A widget holding the mouse answers wherever the pointer has got to, which
    // is what keeps the resize cursor up while a splitter is dragged past its
    // own band — the drag is still happening, and a pointer that reverts to an
    // arrow mid-drag reads as the drag having been dropped.
    eacp::Graphics::MouseCursor cursorAt(const eacp::Graphics::Point& point) const;

    // --- focus -----------------------------------------------------------

    void setFocus(Widget* widget);
    Widget* focused() const { return focusedWidget; }

    // Tab traversal, in paint order and cyclic. Skips hidden widgets and any
    // that do not accept focus.
    void focusNext();
    void focusPrevious();

    // Every focusable widget in the tree, in traversal order. Public because it
    // is the honest way to test the ordering.
    eacp::Vector<Widget*> focusableWidgets() const;

private:
    void moveFocus(int direction);

    Widget* rootWidget = nullptr;
    Widget* capturedWidget = nullptr;
    Widget* focusedWidget = nullptr;
};
} // namespace ecode
