#include "ContextMenu.h"

#include "UIText.h"

#include <algorithm>

namespace ecode
{
using namespace eacp;

namespace
{
constexpr auto rowHeight = 24.f;
constexpr auto separatorHeight = 9.f;
constexpr auto padding = 10.f;
constexpr auto borderWidth = 1.f;

// Between the title and the shortcut column, so the two never touch on a row
// whose title is long.
constexpr auto shortcutGap = 28.f;

constexpr auto minWidth = 160.f;
constexpr auto maxWidth = 420.f;

// Kept off the window edge when the box has to be pushed back on screen, so it
// never looks welded to the side.
constexpr auto screenMargin = 4.f;
} // namespace

ContextMenu::ContextMenu(const ChromeTheme& themeToUse,
                         const CommandRegistry& registryToUse,
                         const Keymap& keymapToUse)
    : theme(themeToUse)
    , registry(registryToUse)
    , keymap(keymapToUse)
{
    setVisible(false);
}

void ContextMenu::show(const Graphics::Point& at,
                       const Vector<std::string>& commandIds)
{
    items.clear();

    for (const auto& id: commandIds)
    {
        if (id.empty())
        {
            // A separator with nothing above it would draw a rule against the
            // box's top edge, and two in a row would draw two rules with
            // nothing between them.
            if (!items.empty() && !items.back().isSeparator)
            {
                auto rule = Row {};
                rule.isSeparator = true;

                items.add(std::move(rule));
            }

            continue;
        }

        const auto* command = registry.find(id);

        if (command == nullptr)
            continue;

        auto row = Row {};
        row.id = id;
        row.title = command->title;
        row.shortcut = keymap.chordFor(id).display();

        items.add(std::move(row));
    }

    // A trailing separator has the same problem as a leading one.
    while (!items.empty() && items.back().isSeparator)
        items.erase(items.end() - 1);

    if (items.empty())
        return;

    anchor = at;
    highlighted = -1;

    // Stale until the next prepare(), and nothing reads it before then. Seeded
    // rather than left at the previous menu's width so a first paint that
    // somehow beat prepare() would be wrong by a sensible amount rather than by
    // the width of a different menu.
    measuredWidth = minWidth;

    setVisible(true);
}

void ContextMenu::hide()
{
    if (!isVisible())
        return;

    setVisible(false);
    items.clear();
    highlighted = -1;

    onClosed();
}

bool ContextMenu::isRowEnabled(int row) const
{
    if (row < 0 || row >= items.size() || items[row].isSeparator)
        return false;

    const auto* command = registry.find(items[row].id);

    return command != nullptr && command->isEnabled();
}

float ContextMenu::rowOffset(int row) const
{
    auto offset = padding;

    for (auto index = 0; index < row && index < items.size(); ++index)
        offset += items[index].isSeparator ? separatorHeight : rowHeight;

    return offset;
}

float ContextMenu::contentHeight() const
{
    return rowOffset(items.size()) + padding;
}

Graphics::Rect ContextMenu::boxBounds() const
{
    const auto height = contentHeight();

    auto x = anchor.x;
    auto y = anchor.y;

    const auto area = bounds();

    // Flipped rather than clamped, which is what a native menu does: a menu
    // opened near the right edge should grow left from the pointer, not sit
    // with the pointer in its middle. Clamping only after flipping, for the
    // case where it does not fit either way.
    if (x + measuredWidth > area.right())
        x = anchor.x - measuredWidth;

    if (y + height > area.bottom())
        y = anchor.y - height;

    x = std::clamp(x,
                   area.x + screenMargin,
                   std::max(area.x + screenMargin,
                            area.right() - measuredWidth - screenMargin));

    y = std::clamp(
        y,
        area.y + screenMargin,
        std::max(area.y + screenMargin, area.bottom() - height - screenMargin));

    return {x, y, measuredWidth, height};
}

Graphics::Rect ContextMenu::rowBounds(int row) const
{
    const auto box = boxBounds();
    const auto height = items[row].isSeparator ? separatorHeight : rowHeight;

    return {box.x, box.y + rowOffset(row), box.w, height};
}

int ContextMenu::rowAt(const Graphics::Point& point) const
{
    if (!boxBounds().contains(point))
        return -1;

    for (auto row = 0; row < items.size(); ++row)
        if (!items[row].isSeparator && rowBounds(row).contains(point))
            return row;

    return -1;
}

void ContextMenu::prepare(Text::GlyphAtlas& atlas, const Graphics::Rect&)
{
    auto widest = minWidth - padding * 2.f;

    for (const auto& row: items)
    {
        if (row.isSeparator)
            continue;

        UIText::prepare(atlas, row.title);

        auto width = UIText::width(atlas, row.title);

        if (!row.shortcut.empty())
        {
            UIText::prepare(atlas, row.shortcut);
            width += shortcutGap + UIText::width(atlas, row.shortcut);
        }

        widest = std::max(widest, width);
    }

    measuredWidth = std::clamp(widest + padding * 2.f, minWidth, maxWidth);
}

void ContextMenu::paint(PaintContext& context)
{
    const auto box = boxBounds();

    context.sprites().fillRect(box, theme.menuBackground);

    // A one-point outline standing in for the drop shadow the sprite renderer
    // cannot draw, the same way the palette's box does it. Without an edge, a
    // dark popup over dark text has none.
    context.sprites().fillRect(box.withHeight(borderWidth), theme.menuBorder);
    context.sprites().fillRect(
        {box.x, box.bottom() - borderWidth, box.w, borderWidth}, theme.menuBorder);
    context.sprites().fillRect({box.x, box.y, borderWidth, box.h}, theme.menuBorder);
    context.sprites().fillRect(
        {box.right() - borderWidth, box.y, borderWidth, box.h}, theme.menuBorder);

    for (auto index = 0; index < items.size(); ++index)
    {
        const auto& row = items[index];
        const auto area = rowBounds(index);

        if (row.isSeparator)
        {
            const auto rule = Graphics::Rect {area.x + padding,
                                              area.y + separatorHeight * 0.5f,
                                              area.w - padding * 2.f,
                                              borderWidth};

            context.sprites().fillRect(rule, theme.menuSeparator);
            continue;
        }

        const auto enabled = isRowEnabled(index);
        const auto isHighlighted = index == highlighted;

        // Inset past the box's own border, which the bar would otherwise paint
        // over — leaving the outline broken along exactly the row the eye is
        // on. Only visible by looking at one.
        if (isHighlighted)
            context.sprites().fillRect(
                {area.x + borderWidth, area.y, area.w - borderWidth * 2.f, area.h},
                theme.menuHighlight);

        const auto baseline = UIText::centredBaseline(context.atlas(), area);

        // The highlight is a filled bar, so the text on it needs its own colour
        // rather than the one that reads against the background.
        const auto titleColor = !enabled        ? theme.menuDisabledText
                                : isHighlighted ? theme.menuHighlightText
                                                : theme.menuText;

        UIText::draw(context, row.title, area.x + padding, baseline, titleColor);

        if (row.shortcut.empty())
            continue;

        const auto width = UIText::width(context.atlas(), row.shortcut);

        // A disabled row greys its shortcut too. The shortcut colour is
        // *lighter* than the disabled one, so leaving it would print ⌘X more
        // brightly than the Cut it belongs to.
        UIText::draw(context,
                     row.shortcut,
                     area.right() - padding - width,
                     baseline,
                     !enabled        ? theme.menuDisabledText
                     : isHighlighted ? theme.menuHighlightText
                                     : theme.menuShortcutText);
    }
}

void ContextMenu::mouseDown(const Graphics::MouseEvent& event)
{
    // Only clicks outside the box reach here as a dismissal; inside, the press
    // arms a choice that the release commits.
    if (!boxBounds().contains(event.pos))
    {
        hide();
        return;
    }

    highlighted = rowAt(event.pos);
}

void ContextMenu::mouseUp(const Graphics::MouseEvent& event)
{
    if (!isOpen())
        return;

    // A release outside the box after pressing inside it cancels rather than
    // choosing, which is what every menu does — it is how someone backs out of
    // one they opened by mistake.
    if (!boxBounds().contains(event.pos))
        return;

    chooseAt(event.pos);
}

void ContextMenu::mouseMoved(const Graphics::MouseEvent& event)
{
    const auto row = rowAt(event.pos);

    if (row == highlighted)
        return;

    highlighted = row;
    repaint();
}

void ContextMenu::chooseAt(const Graphics::Point& point)
{
    const auto row = rowAt(point);

    if (row < 0)
        return;

    highlighted = row;
    acceptHighlighted();
}

void ContextMenu::acceptHighlighted()
{
    if (!isRowEnabled(highlighted))
        return;

    // Copied out before hide(), which clears the row list — and the menu closes
    // before the command runs, so a command that opens another menu or moves
    // focus is not undone by the close that would otherwise follow it. Same
    // ordering the palette settled on, and for the same reason.
    const auto id = items[highlighted].id;

    hide();
    onCommandChosen(id);
}

void ContextMenu::moveHighlight(int direction)
{
    const auto count = items.size();

    if (count == 0)
        return;

    // From nothing, Down lands on the first runnable row and Up on the last,
    // rather than both starting from row 0.
    auto index = highlighted;

    if (index < 0)
        index = direction > 0 ? -1 : count;

    for (auto step = 0; step < count; ++step)
    {
        index += direction;

        // Stops at the ends rather than cycling. A context menu is short enough
        // to see whole, so wrapping past the last item reads as the highlight
        // jumping rather than as reaching the end.
        if (index < 0 || index >= count)
            return;

        if (isRowEnabled(index))
        {
            highlighted = index;
            repaint();
            return;
        }
    }
}

bool ContextMenu::keyDown(const Graphics::KeyEvent& event)
{
    switch (event.keyCode)
    {
        case Graphics::KeyCode::Escape:
            hide();
            return true;

        case Graphics::KeyCode::Return:
            acceptHighlighted();
            return true;

        case Graphics::KeyCode::UpArrow:
            moveHighlight(-1);
            return true;

        case Graphics::KeyCode::DownArrow:
            moveHighlight(1);
            return true;

        default:
            break;
    }

    // Everything else is swallowed: the menu is modal while it is up, and a
    // keystroke falling through to the document behind a menu would edit a file
    // the person cannot currently see the caret in.
    return true;
}
} // namespace ecode
