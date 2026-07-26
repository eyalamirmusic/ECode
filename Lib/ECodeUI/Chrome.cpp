#include "Chrome.h"

#include "UIText.h"

#include <algorithm>
#include <cmath>

namespace ecode
{
using namespace eacp;

namespace
{
constexpr auto preferredTabWidth = 180.f;

// Below this a filename stops being readable and a tab stops being worth
// clicking, so the strip overflows and scrolls instead of shrinking further.
// Chosen against the padding and the close button either side of the title:
// what is left is about nine characters, which is enough to tell two files in a
// directory apart once the rest is elided.
constexpr auto minTabWidth = 116.f;

constexpr auto tabPadding = 12.f;
constexpr auto dotSize = 7.f;
constexpr auto accentHeight = 2.f;
constexpr auto separatorWidth = 1.f;

// The square the × sits in, which is also its click target — bigger than the
// glyph, because a close button that has to be aimed at gets hit by accident on
// the way to selecting the tab.
constexpr auto closeSize = 18.f;

// U+00D7, drawn as a glyph rather than assembled from quads: the sprite
// renderer fills rectangles and nothing else, so a hand-built × would be two
// stacks of little squares with no antialiasing, and the atlas is right there.
constexpr auto closeGlyph = "×";

constexpr auto statusPadding = 10.f;
} // namespace

void Panel::paint(PaintContext& context)
{
    context.sprites().fillRect(bounds(), colour);
}

// --- TabBar -----------------------------------------------------------------

void TabBar::setTabs(Vector<TabItem> newTabs)
{
    tabs = std::move(newTabs);

    // An index left over from a longer list would draw an accent on nothing.
    if (active >= tabs.size())
        active = std::max(0, tabs.size() - 1);

    // The pointer has not moved, but what is under it has: a tab closed by a
    // click on its × leaves the pointer over whatever slid left into its place,
    // and a stale index would light the wrong tab or an index past the end.
    hovered = -1;
    hoveredClose = false;

    clampOffset();
    scrollToActive();

    repaint();
}

void TabBar::setActiveTab(int index)
{
    if (index == active)
        return;

    active = index;

    scrollToActive();
    repaint();
}

void TabBar::setGroupActive(bool isActive)
{
    if (groupActive == isActive)
        return;

    groupActive = isActive;

    repaint();
}

void TabBar::layout()
{
    // A narrower strip both changes every tab's width and can leave the offset
    // pointing past the end of a strip that now fits.
    clampOffset();
    scrollToActive();
}

float TabBar::tabWidth() const
{
    if (tabs.size() <= 0)
        return preferredTabWidth;

    const auto even = bounds().w / static_cast<float>(tabs.size());

    return std::clamp(even, minTabWidth, preferredTabWidth);
}

void TabBar::clampOffset()
{
    const auto overflow = static_cast<float>(tabs.size()) * tabWidth() - bounds().w;

    offsetX = std::clamp(offsetX, 0.f, std::max(0.f, overflow));
}

void TabBar::scrollToActive()
{
    if (active < 0 || active >= tabs.size())
        return;

    const auto width = tabWidth();
    const auto left = static_cast<float>(active) * width;

    // Right edge first and left edge second, so that a tab wider than the strip
    // shows its left end — which is where the filename is.
    offsetX = std::max(offsetX, left + width - bounds().w);
    offsetX = std::min(offsetX, left);

    clampOffset();
}

Graphics::Rect TabBar::boundsOfTab(int index) const
{
    const auto area = bounds();
    const auto width = tabWidth();

    return {
        area.x + static_cast<float>(index) * width - offsetX, area.y, width, area.h};
}

Graphics::Rect TabBar::closeBoundsOfTab(int index) const
{
    auto area = boundsOfTab(index).inset(tabPadding * 0.5f, 0.f);
    const auto slot = area.removeFromRight(closeSize);

    return {slot.x, slot.y + (slot.h - closeSize) * 0.5f, closeSize, closeSize};
}

int TabBar::tabAt(const Graphics::Point& point) const
{
    // Only inside the strip: boundsOfTab is unclipped, so a tab scrolled off
    // the left still reports a rectangle, and it is one that reaches under the
    // sidebar.
    if (!bounds().contains(point))
        return -1;

    for (auto index = 0; index < tabs.size(); ++index)
        if (boundsOfTab(index).contains(point))
            return index;

    return -1;
}

void TabBar::prepare(Text::GlyphAtlas& atlas, const Graphics::Rect&)
{
    for (const auto& tab: tabs)
        UIText::prepareElided(atlas, tab.title);

    UIText::prepare(atlas, closeGlyph);
}

void TabBar::paint(PaintContext& context)
{
    context.sprites().fillRect(bounds(), theme.tabBar);

    for (auto index = 0; index < tabs.size(); ++index)
    {
        const auto& tab = tabs[index];
        const auto area = boundsOfTab(index);
        const auto isActive = index == active;

        // Everything for this tab is clipped to it, so a long filename stops at
        // the tab's edge rather than running into the next one.
        const auto clip = ClipScope {context, area};

        if (clip.isEmpty())
            continue;

        if (isActive)
        {
            context.sprites().fillRect(area, theme.activeTab);

            // The fill stays: the tab is still the file this pane is showing.
            // Only the accent is muted, so an inactive group reads as "this is
            // the file over here" rather than as having no selection at all.
            context.sprites().fillRect(area.withHeight(accentHeight),
                                       groupActive ? theme.activeTabAccent
                                                   : theme.inactiveGroupAccent);
        }
        else if (index == hovered)
        {
            context.sprites().fillRect(area, theme.hoverTab);
        }

        // Between this tab and the next, not around it, so two rules do not
        // stack into a two-point line down the middle of the strip.
        if (index + 1 < tabs.size())
            context.sprites().fillRect(
                {area.right() - separatorWidth, area.y, separatorWidth, area.h},
                theme.tabSeparator);

        auto text = area.inset(tabPadding, 0.f);

        // The close slot is taken whether or not a × is drawn in it. Reserving
        // it only while hovered would reflow the title under the pointer, which
        // reads as the tab twitching.
        text.removeFromRight(closeSize);

        // The status dot takes the left of the label rather than overlaying it,
        // so a name long enough to fill the tab still cannot hide it.
        if (tab.modified || tab.conflicted)
        {
            const auto slot = text.removeFromLeft(dotSize + tabPadding * 0.5f);
            const auto colour = tab.conflicted ? theme.conflict : theme.unsaved;

            context.sprites().fillRect(
                {slot.x, slot.y + (slot.h - dotSize) * 0.5f, dotSize, dotSize},
                colour);
        }

        // Shortened to its own box rather than clipped to the tab: a clip cuts
        // the last character in half and gives no sign the name went on, so at
        // the narrow end "Workspace.cpp" and "Workspace.h" both come out
        // "Workspace." and read as two views of one file.
        UIText::draw(context,
                     UIText::elide(context.atlas(), tab.title, text.w),
                     text.x,
                     UIText::centredBaseline(context.atlas(), text),
                     isActive ? theme.activeTabText : theme.inactiveTabText);

        // Shown on the tab being worked in and on the one under the pointer,
        // and nowhere else — a × on every tab turns a row of filenames into a
        // row of buttons.
        if (!isActive && index != hovered)
            continue;

        const auto closeArea = closeBoundsOfTab(index);
        const auto onClose = index == hovered && hoveredClose;

        if (onClose)
            context.sprites().fillRect(closeArea, theme.tabCloseHover);

        const auto glyphWidth = UIText::width(context.atlas(), closeGlyph);

        UIText::draw(context,
                     closeGlyph,
                     closeArea.x + (closeArea.w - glyphWidth) * 0.5f,
                     UIText::centredBaseline(context.atlas(), closeArea),
                     onClose ? theme.tabCloseIconHover : theme.tabCloseIcon);
    }
}

void TabBar::setHover(int tab, bool onClose)
{
    if (tab == hovered && onClose == hoveredClose)
        return;

    hovered = tab;
    hoveredClose = onClose;

    repaint();
}

void TabBar::mouseMoved(const Graphics::MouseEvent& event)
{
    const auto index = tabAt(event.pos);

    setHover(index, index >= 0 && closeBoundsOfTab(index).contains(event.pos));
}

void TabBar::mouseExited()
{
    setHover(-1, false);
}

void TabBar::mouseDown(const Graphics::MouseEvent& event)
{
    armedClose = -1;
    armedByMiddleButton = false;

    const auto index = tabAt(event.pos);

    if (index < 0)
        return;

    // Middle-click closes wherever on the tab it lands, which is the gesture
    // every browser has trained people to expect and the only way to close a
    // tab without first aiming at an 18-point square.
    if (event.button == Graphics::MouseButton::Middle)
    {
        armedClose = index;
        armedByMiddleButton = true;

        return;
    }

    if (event.button != Graphics::MouseButton::Left)
        return;

    if (closeBoundsOfTab(index).contains(event.pos))
    {
        armedClose = index;
        return;
    }

    // Selection follows the press rather than the release, unlike closing: a
    // tab is switched to by pressing it, and waiting for the release would make
    // the strip feel a frame behind the hand.
    setActiveTab(index);
    onTabSelected(index);
}

void TabBar::mouseUp(const Graphics::MouseEvent& event)
{
    const auto armed = armedClose;
    const auto middle = armedByMiddleButton;

    // Cleared before the callback, which is going to close a tab and call back
    // into setTabs.
    armedClose = -1;
    armedByMiddleButton = false;

    if (armed < 0 || armed >= tabs.size())
        return;

    // Released somewhere else: the press is cancelled rather than honoured.
    // A middle click is answered anywhere on the tab it began on, since it was
    // never aimed at the button in the first place.
    const auto over = middle ? boundsOfTab(armed).contains(event.pos)
                             : closeBoundsOfTab(armed).contains(event.pos);

    if (over)
        onTabClosed(armed);
}

bool TabBar::mouseWheel(const Graphics::MouseEvent& event)
{
    const auto overflow = static_cast<float>(tabs.size()) * tabWidth() - bounds().w;

    // Nothing to scroll: hand the wheel back rather than swallowing it, so a
    // strip that fits does not silently eat a gesture aimed past it.
    if (overflow <= 0.f)
        return false;

    // A vertical wheel scrolls the strip too. A plain mouse has no horizontal
    // axis at all, and a row of tabs is the one place where "scroll" can only
    // mean one direction.
    const auto delta = std::abs(event.delta.x) > std::abs(event.delta.y)
                           ? event.delta.x
                           : event.delta.y;

    offsetX -= delta;
    clampOffset();

    repaint();

    return true;
}

// --- StatusBar --------------------------------------------------------------

void StatusBar::setText(std::string left, std::string right)
{
    if (left == leftText && right == rightText)
        return;

    leftText = std::move(left);
    rightText = std::move(right);

    repaint();
}

void StatusBar::prepare(Text::GlyphAtlas& atlas, const Graphics::Rect&)
{
    UIText::prepare(atlas, leftText);
    UIText::prepare(atlas, rightText);
}

void StatusBar::paint(PaintContext& context)
{
    const auto area = bounds();

    context.sprites().fillRect(area, theme.statusBar);

    const auto baseline = UIText::centredBaseline(context.atlas(), area);
    const auto inner = area.inset(statusPadding, 0.f);

    UIText::draw(context, leftText, inner.x, baseline, theme.statusText);

    // Right-aligned against the far edge, so it stays put as the left text
    // changes width with the caret's column.
    const auto rightWidth = UIText::width(context.atlas(), rightText);

    UIText::draw(
        context, rightText, inner.right() - rightWidth, baseline, theme.statusText);
}
} // namespace ecode
