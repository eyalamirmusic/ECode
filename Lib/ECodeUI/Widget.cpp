#include "Widget.h"

namespace ecode
{
using namespace eacp;

void Widget::addChild(Widget& child)
{
    child.parentWidget = this;
    childList.push_back(&child);
}

Widget& Widget::addChild(OwningPointer<Widget> child)
{
    auto& reference = *child;

    reference.parentWidget = this;
    childList.push_back(&reference);

    // add rather than push_back: only add has an rvalue overload, and an
    // OwningPointer copy needs a clone() the widgets deliberately do not have.
    ownedChildren.add(std::move(child));

    return reference;
}

void Widget::removeAllChildren()
{
    childList.clear();
    ownedChildren.clear();
}

void Widget::setBounds(const Graphics::Rect& newBounds)
{
    area = newBounds;
    layout();
}

void Widget::setVisible(bool shouldBeVisible)
{
    if (visible == shouldBeVisible)
        return;

    visible = shouldBeVisible;
    repaint();
}

float Widget::preferredHeight(float) const
{
    return area.h;
}

void Widget::prepareTree(Text::GlyphAtlas& atlas, const Graphics::Rect& clip)
{
    if (!visible || area.isEmpty())
        return;

    // The same narrowing paintTree does, so the two walks agree on what is
    // visible. A widget scrolled entirely out of its parent rasterizes
    // nothing, which is what keeps a long list's prepass proportional to the
    // viewport rather than to the number of rows.
    const auto narrowed = clip.intersection(area);

    if (narrowed.isEmpty())
        return;

    prepare(atlas, narrowed);

    for (auto* child: childList)
        child->prepareTree(atlas, narrowed);
}

void Widget::paintTree(PaintContext& context)
{
    if (!visible || area.isEmpty())
        return;

    const auto scope = ClipScope {context, area};

    // Entirely outside its parent, so neither it nor anything inside it can
    // put a pixel on screen.
    if (scope.isEmpty())
        return;

    paint(context);

    for (auto* child: childList)
        child->paintTree(context);
}

Widget* Widget::widgetAt(const Graphics::Point& point)
{
    if (!visible || !area.contains(point))
        return nullptr;

    // Back to front: the last child painted is the one on top, so it is the
    // one the click belongs to.
    for (auto index = childList.size(); index > 0; --index)
        if (auto* hit = childList[index - 1]->widgetAt(point))
            return hit;

    return wantsMouse() ? this : nullptr;
}

void Widget::repaint()
{
    auto* root = this;

    while (root->parentWidget != nullptr)
        root = root->parentWidget;

    root->onRepaintNeeded();
}
} // namespace ecode
