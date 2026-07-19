#include "CommandPalette.h"

#include "UIText.h"

#include <algorithm>

namespace ecode
{
using namespace eacp;

namespace
{
constexpr auto maxWidth = 620.f;
constexpr auto sideMargin = 24.f;
constexpr auto topOffset = 72.f;

constexpr auto inputHeight = 42.f;
constexpr auto rowHeight = 26.f;
constexpr auto padding = 12.f;
constexpr auto borderWidth = 1.f;
constexpr auto caretWidth = 2.f;

// Past this the results scroll rather than the box growing, so the palette
// never covers the file it is being used on.
constexpr auto maxVisibleRows = std::size_t {12};

constexpr auto placeholder = "Type a command";
constexpr auto emptyMessage = "No matching commands";

// Back over one UTF-8 sequence: the continuation bytes first, then the lead.
// Backspace deletes a character, and a query with a ⌘ in it deleted a byte at a
// time would leave half a sequence behind and stop matching anything.
std::size_t withoutLastCharacter(std::string_view text)
{
    auto index = text.size();

    while (index > 0)
    {
        --index;

        if ((static_cast<unsigned char>(text[index]) & 0xc0) != 0x80)
            break;
    }

    return index;
}

// Text the key produced, or nothing when it is not text. Return, Tab and
// Escape all arrive with `characters` set to a control code, so a palette that
// simply appended whatever came in would type them into the query.
bool isTypedText(const Graphics::KeyEvent& event)
{
    if (event.characters.empty() || event.modifiers.command
        || event.modifiers.control)
        return false;

    for (auto c: event.characters)
        if (static_cast<unsigned char>(c) < 0x20)
            return false;

    return true;
}
} // namespace

CommandPalette::CommandPalette(const ChromeTheme& themeToUse,
                               const CommandRegistry& registryToUse,
                               const Keymap& keymapToUse)
    : theme(themeToUse)
    , registry(registryToUse)
    , keymap(keymapToUse)
    , results(themeToUse)
{
    setVisible(false);

    list.setRowHeight(rowHeight);

    // The palette owns the keyboard: the query is what is being typed into, and
    // the list follows it. A focusable list would steal focus on a row click.
    list.setFocusable(false);

    list.paintRow = [this](PaintContext& context,
                           std::size_t index,
                           const Graphics::Rect& area,
                           bool selected) { paintRow(context, index, area, selected); };

    list.prepareRow = [this](Text::GlyphAtlas& atlas, std::size_t index)
    {
        const auto& entry = matches[static_cast<int>(index)];

        UIText::prepare(atlas, registry.commands()[entry.command].title);
        UIText::prepare(atlas, entry.shortcut);
    };

    // A click runs the command under the pointer rather than only selecting it,
    // which is what every palette does and what a single click is for.
    list.onRowClicked = [this](std::size_t, int) { acceptSelection(); };

    list.onSelectionChanged = [this](int row)
    {
        if (row >= 0)
            results.scrollToShow(static_cast<float>(row) * rowHeight, rowHeight);
    };

    results.setContent(list);

    addChild(results);
}

void CommandPalette::show()
{
    setVisible(true);
    setQuery({});
    repaint();
}

void CommandPalette::hide()
{
    if (!isVisible())
        return;

    setVisible(false);
    onClosed();
    repaint();
}

void CommandPalette::setQuery(std::string text)
{
    queryText = std::move(text);

    refilter();
    layout();
    repaint();
}

void CommandPalette::refilter()
{
    matches.clear();

    for (auto index = 0; index < registry.commands().size(); ++index)
    {
        const auto& command = registry.commands()[index];

        auto match = fuzzyMatch(queryText, command.title);

        if (!match)
            continue;

        auto entry = Entry {};
        entry.command = index;
        entry.match = std::move(*match);
        entry.shortcut = keymap.chordFor(command.id).display();

        matches.push_back(std::move(entry));
    }

    // Stable, so commands that score the same stay in registration order —
    // which is the order they were meant to be offered in, and the order an
    // empty query shows, so the list does not reshuffle under a first keystroke
    // that separates nothing.
    std::stable_sort(matches.begin(),
                     matches.end(),
                     [](const Entry& a, const Entry& b)
                     { return a.match.score > b.match.score; });

    list.setRowCount(static_cast<std::size_t>(matches.size()));

    // Always start on the first result: the point of typing is that the thing
    // wanted is at the top, and Enter should take it without an arrow key.
    list.setSelectedRow(matches.empty() ? -1 : 0);

    results.setScrollPosition(0.f);
}

float CommandPalette::resultsHeight() const
{
    // An empty result set still gets a row, which is what "No matching
    // commands" is drawn in. A box that collapsed to the query field would look
    // like the palette had closed.
    const auto rows =
        std::clamp(static_cast<std::size_t>(matches.size()), std::size_t {1}, maxVisibleRows);

    return static_cast<float>(rows) * rowHeight;
}

Graphics::Rect CommandPalette::boxBounds() const
{
    const auto area = bounds();
    const auto width = std::min(maxWidth, std::max(0.f, area.w - sideMargin * 2.f));

    return {area.x + (area.w - width) * 0.5f,
            area.y + topOffset,
            width,
            inputHeight + resultsHeight()};
}

Graphics::Rect CommandPalette::inputBounds() const
{
    auto box = boxBounds();

    return box.removeFromTop(inputHeight);
}

Graphics::Rect CommandPalette::resultsBounds() const
{
    auto box = boxBounds();

    box.removeFromTop(inputHeight);

    return box;
}

void CommandPalette::layout()
{
    results.setBounds(resultsBounds());

    // Nothing matched, so the message is painted by this widget and the scroll
    // view would only draw an empty background over it.
    results.setVisible(!matches.empty());
}

void CommandPalette::prepare(Text::GlyphAtlas& atlas, const Graphics::Rect&)
{
    UIText::prepare(atlas, queryText.empty() ? placeholder : queryText);

    if (matches.empty())
        UIText::prepare(atlas, emptyMessage);
}

void CommandPalette::paint(PaintContext& context)
{
    // Dims the window behind it. The editor is still legible through it, which
    // is the point — the palette is a thing on top of the work, not a screen
    // that replaces it.
    context.sprites().fillRect(bounds(), theme.paletteBackdrop);

    const auto box = boxBounds();

    context.sprites().fillRect(box, theme.paletteBackground);

    // A one-point outline in place of a shadow, which the sprite renderer has
    // no way to draw. Without either, a dark box on a dimmed dark background
    // has no edge at all.
    context.sprites().fillRect(box.withHeight(borderWidth), theme.paletteBorder);
    context.sprites().fillRect({box.x, box.bottom() - borderWidth, box.w, borderWidth},
                               theme.paletteBorder);
    context.sprites().fillRect({box.x, box.y, borderWidth, box.h}, theme.paletteBorder);
    context.sprites().fillRect({box.right() - borderWidth, box.y, borderWidth, box.h},
                               theme.paletteBorder);

    const auto input = inputBounds();
    const auto text = input.inset(padding, 0.f);
    const auto baseline = UIText::centredBaseline(context.atlas(), input);

    const auto pen =
        queryText.empty()
            ? UIText::draw(context, placeholder, text.x, baseline, theme.paletteHintText)
            : UIText::draw(context, queryText, text.x, baseline, theme.paletteText);

    // Solid rather than blinking: the blink timer belongs to the editor's
    // caret, and a palette is open for a few seconds at a time.
    const auto metrics = context.atlas().metrics();

    context.sprites().fillRect({queryText.empty() ? text.x : pen,
                                baseline - metrics.ascent,
                                caretWidth,
                                metrics.ascent + metrics.descent},
                               theme.paletteText);

    context.sprites().fillRect({input.x, input.bottom() - borderWidth, input.w, borderWidth},
                               theme.paletteBorder);

    if (matches.empty())
        UIText::draw(context,
                     emptyMessage,
                     text.x,
                     UIText::centredBaseline(context.atlas(), resultsBounds()),
                     theme.paletteHintText);
}

void CommandPalette::drawTitle(PaintContext& context,
                               const Entry& entry,
                               float x,
                               float baseline,
                               const Graphics::Color& base) const
{
    const auto title = std::string_view {registry.commands()[entry.command].title};
    const auto& positions = entry.match.positions;

    auto pen = x;
    std::size_t index = 0;
    auto nextMatch = 0;

    const auto matchesAt = [&](std::size_t at)
    { return nextMatch < positions.size() && positions[nextMatch] == at; };

    while (index < title.size())
    {
        const auto highlighted = matchesAt(index);
        auto end = index;

        // Extends the run for as long as its highlighting does not change, so
        // "sa" in "File: Save" is two draws rather than one per character.
        while (end < title.size() && matchesAt(end) == highlighted)
        {
            if (highlighted)
                ++nextMatch;

            ++end;
        }

        pen = UIText::draw(context,
                           title.substr(index, end - index),
                           pen,
                           baseline,
                           highlighted ? theme.paletteMatchText : base);

        index = end;
    }
}

void CommandPalette::paintRow(PaintContext& context,
                              std::size_t index,
                              const Graphics::Rect& area,
                              bool selected)
{
    const auto& entry = matches[static_cast<int>(index)];
    const auto& command = registry.commands()[entry.command];
    const auto enabled = command.isEnabled();

    if (selected)
        context.sprites().fillRect(area, theme.paletteSelected);

    const auto inner = area.inset(padding, 0.f);
    const auto baseline = UIText::centredBaseline(context.atlas(), area);

    drawTitle(context,
              entry,
              inner.x,
              baseline,
              enabled ? theme.paletteText : theme.paletteDisabledText);

    // Right-aligned against the box edge, the way a menu prints its shortcut,
    // so the column lines up whatever the titles are.
    const auto width = UIText::width(context.atlas(), entry.shortcut);

    UIText::draw(context,
                 entry.shortcut,
                 inner.right() - width,
                 baseline,
                 theme.paletteHintText);
}

void CommandPalette::acceptSelection()
{
    const auto selected = list.selectedRow();

    if (selected < 0 || selected >= matches.size())
        return;

    const auto& command = registry.commands()[matches[selected].command];

    if (!command.isEnabled())
        return;

    // The palette closes *before* the command runs, so a command that opens the
    // palette again — or moves focus — is not undone by the close that would
    // otherwise follow it. The action is copied out first because hide() fires
    // onClosed, and nothing here should depend on what that leaves the registry
    // holding.
    const auto action = command.run;

    hide();
    action();
}

void CommandPalette::mouseDown(const Graphics::MouseEvent& event)
{
    // Only the backdrop reaches here — a click on a row goes to the list, which
    // is a deeper widget. So this is a click outside the box, which dismisses,
    // except when it landed on the query field.
    if (!boxBounds().contains(event.pos))
        hide();
}

bool CommandPalette::keyDown(const Graphics::KeyEvent& event)
{
    switch (event.keyCode)
    {
        case Graphics::KeyCode::Escape:
            hide();
            return true;

        case Graphics::KeyCode::Return:
            acceptSelection();
            return true;

        case Graphics::KeyCode::Delete:
            setQuery(queryText.substr(0, withoutLastCharacter(queryText)));
            return true;

        case Graphics::KeyCode::UpArrow:
        case Graphics::KeyCode::DownArrow:
        case Graphics::KeyCode::Home:
        case Graphics::KeyCode::End:
            return list.keyDown(event);

        default:
            break;
    }

    if (!isTypedText(event))
    {
        // Everything else is swallowed rather than bubbling to the editor
        // underneath, which is still in the tree and would otherwise be typed
        // into while the palette is open.
        return true;
    }

    setQuery(queryText + event.characters);
    return true;
}
} // namespace ecode
