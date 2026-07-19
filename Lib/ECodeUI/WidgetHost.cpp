#include "WidgetHost.h"

namespace ecode
{
using namespace eacp;

namespace
{
// Nearest widget at or above `widget` that will take focus. Clicking a row
// inside a focusable list should focus the list, not nothing.
Widget* focusableFrom(Widget* widget)
{
    for (auto* candidate = widget; candidate != nullptr;
         candidate = candidate->parent())
        if (candidate->acceptsFocus() && candidate->isVisible())
            return candidate;

    return nullptr;
}

void collectFocusable(Widget& widget, Vector<Widget*>& out)
{
    if (!widget.isVisible())
        return;

    if (widget.acceptsFocus())
        out.push_back(&widget);

    for (auto* child: widget.children())
        collectFocusable(*child, out);
}
} // namespace

void WidgetHost::setRoot(Widget& newRoot)
{
    rootWidget = &newRoot;

    // The old tree's widgets are about to stop existing as far as this host is
    // concerned, and both of these are raw pointers into it.
    capturedWidget = nullptr;
    focusedWidget = nullptr;
}

void WidgetHost::setBounds(const Graphics::Rect& bounds)
{
    if (rootWidget != nullptr)
        rootWidget->setBounds(bounds);
}

void WidgetHost::prepare(Text::GlyphAtlas& atlas)
{
    if (rootWidget != nullptr)
        rootWidget->prepareTree(atlas);
}

void WidgetHost::paint(PaintContext& context)
{
    if (rootWidget != nullptr)
        rootWidget->paintTree(context);
}

void WidgetHost::mouseDown(const Graphics::MouseEvent& event)
{
    if (rootWidget == nullptr)
        return;

    capturedWidget = rootWidget->widgetAt(event.pos);

    if (auto* target = focusableFrom(capturedWidget))
        setFocus(target);

    if (capturedWidget != nullptr)
        capturedWidget->mouseDown(event);
}

void WidgetHost::mouseDragged(const Graphics::MouseEvent& event)
{
    if (capturedWidget != nullptr)
        capturedWidget->mouseDragged(event);
}

void WidgetHost::mouseUp(const Graphics::MouseEvent& event)
{
    if (capturedWidget == nullptr)
        return;

    // Cleared before the call, so a handler that tears its own widget down
    // cannot leave a dangling capture behind.
    auto* target = capturedWidget;
    capturedWidget = nullptr;

    target->mouseUp(event);
}

void WidgetHost::mouseMoved(const Graphics::MouseEvent& event)
{
    if (rootWidget == nullptr)
        return;

    if (auto* target = rootWidget->widgetAt(event.pos))
        target->mouseMoved(event);
}

bool WidgetHost::mouseWheel(const Graphics::MouseEvent& event)
{
    if (rootWidget == nullptr)
        return false;

    for (auto* widget = rootWidget->widgetAt(event.pos); widget != nullptr;
         widget = widget->parent())
        if (widget->mouseWheel(event))
            return true;

    return false;
}

bool WidgetHost::keyDown(const Graphics::KeyEvent& event)
{
    for (auto* widget = focusedWidget; widget != nullptr;
         widget = widget->parent())
        if (widget->keyDown(event))
            return true;

    return false;
}

void WidgetHost::setFocus(Widget* widget)
{
    if (focusedWidget == widget)
        return;

    auto* previous = focusedWidget;

    // Moved before either callback so that a handler asking who has focus gets
    // the answer it will still have once the swap finishes.
    focusedWidget = widget;

    if (previous != nullptr)
        previous->focusLost();

    if (focusedWidget != nullptr)
        focusedWidget->focusGained();
}

Vector<Widget*> WidgetHost::focusableWidgets() const
{
    auto found = Vector<Widget*> {};

    if (rootWidget != nullptr)
        collectFocusable(*rootWidget, found);

    return found;
}

void WidgetHost::moveFocus(int direction)
{
    const auto candidates = focusableWidgets();

    if (candidates.empty())
        return;

    const auto count = static_cast<int>(candidates.size());
    auto next = direction > 0 ? 0 : count - 1;

    for (auto index = 0; index < count; ++index)
    {
        if (candidates[static_cast<std::size_t>(index)] != focusedWidget)
            continue;

        // Cyclic, and written to stay non-negative before the modulo: a
        // Shift+Tab off the first widget has to land on the last.
        next = (index + direction + count) % count;
        break;
    }

    setFocus(candidates[static_cast<std::size_t>(next)]);
}

void WidgetHost::focusNext()
{
    moveFocus(1);
}

void WidgetHost::focusPrevious()
{
    moveFocus(-1);
}
} // namespace ecode
