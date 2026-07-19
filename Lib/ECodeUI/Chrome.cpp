#include "Chrome.h"

#include "UIText.h"

namespace ecode
{
using namespace eacp;

namespace
{
constexpr auto tabWidth = 180.f;
constexpr auto tabPadding = 12.f;
constexpr auto dotSize = 7.f;
constexpr auto accentHeight = 2.f;

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
    if (active >= static_cast<int>(tabs.size()))
        active = tabs.empty() ? 0 : static_cast<int>(tabs.size()) - 1;

    repaint();
}

void TabBar::setActiveTab(int index)
{
    if (index == active)
        return;

    active = index;
    repaint();
}

Graphics::Rect TabBar::boundsOfTab(int index) const
{
    const auto area = bounds();

    return {area.x + static_cast<float>(index) * tabWidth, area.y, tabWidth, area.h};
}

int TabBar::tabAt(const Graphics::Point& point) const
{
    for (auto index = 0; index < static_cast<int>(tabs.size()); ++index)
        if (boundsOfTab(index).contains(point))
            return index;

    return -1;
}

void TabBar::prepare(Text::GlyphAtlas& atlas)
{
    for (const auto& tab: tabs)
        UIText::prepare(atlas, tab.title);
}

void TabBar::paint(PaintContext& context)
{
    context.sprites().fillRect(bounds(), theme.tabBar);

    for (auto index = 0; index < static_cast<int>(tabs.size()); ++index)
    {
        const auto& tab = tabs[static_cast<std::size_t>(index)];
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
            context.sprites().fillRect(area.withHeight(accentHeight),
                                       theme.activeTabAccent);
        }

        auto text = area.inset(tabPadding, 0.f);

        // The status dot takes the left of the label rather than overlaying it,
        // so a name long enough to fill the tab still cannot hide it.
        if (tab.modified || tab.conflicted)
        {
            const auto slot = text.removeFromLeft(dotSize + tabPadding * 0.5f);
            const auto colour = tab.conflicted ? theme.conflict : theme.unsaved;

            context.sprites().fillRect({slot.x,
                                        slot.y + (slot.h - dotSize) * 0.5f,
                                        dotSize,
                                        dotSize},
                                       colour);
        }

        UIText::draw(context,
                     tab.title,
                     text.x,
                     UIText::centredBaseline(context.atlas(), text),
                     isActive ? theme.activeTabText : theme.inactiveTabText);
    }
}

void TabBar::mouseDown(const Graphics::MouseEvent& event)
{
    const auto index = tabAt(event.pos);

    if (index < 0)
        return;

    setActiveTab(index);
    onTabSelected(index);
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

void StatusBar::prepare(Text::GlyphAtlas& atlas)
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

    UIText::draw(context,
                 rightText,
                 inner.right() - rightWidth,
                 baseline,
                 theme.statusText);
}
} // namespace ecode
