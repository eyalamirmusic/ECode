#include "FindBar.h"

#include "UIText.h"

#include <algorithm>

namespace ecode
{
using namespace eacp;

namespace
{
constexpr auto rowHeight = 34.f;
constexpr auto padding = 8.f;
constexpr auto gap = 6.f;
constexpr auto fieldWidth = 200.f;
constexpr auto fieldInset = 5.f;
constexpr auto borderWidth = 1.f;

constexpr auto toggleWidth = 30.f;
constexpr auto wideToggleWidth = 48.f;
constexpr auto counterWidth = 78.f;
constexpr auto buttonWidth = 26.f;

constexpr auto replaceButtonWidth = 68.f;
constexpr auto replaceAllWidth = 40.f;

// Arrows and a multiplication sign rather than an icon set, which does not
// exist. All three are in Menlo, which the × of ✕ (U+2715) is not — a missing
// glyph rasterizes as nothing at all, so a close button drawn with one would be
// an invisible control.
constexpr auto previousLabel = "↑";
constexpr auto nextLabel = "↓";
constexpr auto closeLabel = "×";

// Words rather than the initials VSCode uses. Its "Aa" and "ab" are learnable
// because a tooltip explains them once, and there are no tooltips here yet —
// PLAN.md §7.3 has them still to build.
constexpr auto caseLabel = "Aa";
constexpr auto wordLabel = "Word";
constexpr auto replaceOneLabel = "Replace";
constexpr auto replaceAllLabel = "All";

constexpr auto findPlaceholder = "Find";
constexpr auto replacePlaceholder = "Replace";
constexpr auto noResults = "No results";

// Centres a label horizontally in its hotspot.
float centredTextX(Text::GlyphAtlas& atlas,
                   const Graphics::Rect& area,
                   std::string_view label)
{
    return area.x + (area.w - UIText::width(atlas, label)) * 0.5f;
}
} // namespace

FindBar::FindBar(const ChromeTheme& themeToUse)
    : theme(themeToUse)
    , findField(themeToUse)
    , replaceField(themeToUse)
{
    setVisible(false);

    const auto colours = TextField::Colours {
        theme.findText, theme.findHintText, theme.findText, theme.paletteSelected};

    findField.setColours(colours);
    findField.setPlaceholder(findPlaceholder);

    replaceField.setColours(colours);
    replaceField.setPlaceholder(replacePlaceholder);

    findField.onTextChanged = [this](const std::string&) { queryChanged(); };

    addChild(findField);
    addChild(replaceField);
}

float FindBar::barHeight() const
{
    return replaceShown ? rowHeight * 2.f : rowHeight;
}

float FindBar::barWidth() const
{
    return padding + fieldWidth + gap + toggleWidth + wideToggleWidth + gap
           + counterWidth + gap + buttonWidth * 3.f + padding;
}

Graphics::Rect FindBar::findRowBounds() const
{
    return bounds().withHeight(rowHeight);
}

Graphics::Rect FindBar::replaceRowBounds() const
{
    auto area = bounds();

    area.removeFromTop(rowHeight);

    return area.withHeight(rowHeight);
}

void FindBar::rebuildHotspots()
{
    hotspots.clear();

    auto row = findRowBounds().inset(padding, fieldInset);

    findField.setBounds(row.removeFromLeft(fieldWidth));
    row.removeFromLeft(gap);

    hotspots.push_back({Control::caseSensitive,
                        row.removeFromLeft(toggleWidth),
                        caseLabel,
                        true,
                        currentQuery.caseSensitive});

    hotspots.push_back({Control::wholeWord,
                        row.removeFromLeft(wideToggleWidth),
                        wordLabel,
                        true,
                        currentQuery.wholeWord});

    // The counter is drawn by paint() rather than being a hotspot: it is a
    // readout, and a region that highlights under the pointer but does nothing
    // is worse than one that plainly does not.
    row.removeFromLeft(gap + counterWidth + gap);

    hotspots.push_back(
        {Control::previous, row.removeFromLeft(buttonWidth), previousLabel});
    hotspots.push_back({Control::next, row.removeFromLeft(buttonWidth), nextLabel});
    hotspots.push_back(
        {Control::close, row.removeFromLeft(buttonWidth), closeLabel});

    if (!replaceShown)
        return;

    auto second = replaceRowBounds().inset(padding, fieldInset);

    // The same width as the find field, so the two line up. Taking it from the
    // constant rather than from findField.bounds() keeps the second row's layout
    // independent of whether the first has run.
    replaceField.setBounds(second.removeFromLeft(fieldWidth));
    second.removeFromLeft(gap);

    hotspots.push_back({Control::replaceOne,
                        second.removeFromLeft(replaceButtonWidth),
                        replaceOneLabel});

    second.removeFromLeft(gap);

    hotspots.push_back({Control::replaceEvery,
                        second.removeFromLeft(replaceAllWidth),
                        replaceAllLabel});
}

void FindBar::layout()
{
    rebuildHotspots();
}

void FindBar::show(const std::string& initialQuery, bool withReplace)
{
    const auto heightChanged = replaceShown != withReplace;

    replaceShown = withReplace;
    replaceField.setVisible(withReplace);

    // An empty selection leaves the previous query in place, which is what makes
    // ⌘F twice in a row a way back to what was being looked for.
    if (!initialQuery.empty())
        findField.setText(initialQuery);

    // Selected rather than appended to: the old query is still offered and still
    // visible, and the next keystroke starts a new one without a trip to
    // Backspace.
    findField.selectAll();

    setVisible(true);

    currentQuery.text = findField.text();

    // Bounds are handed down by parents in this tree, so a widget whose height
    // has changed has to ask for them again rather than resize itself.
    if (heightChanged && parent() != nullptr)
        parent()->layout();
    else
        layout();

    onQueryChanged();
    repaint();
}

void FindBar::hide()
{
    if (!isVisible())
        return;

    setVisible(false);
    onClosed();
    repaint();
}

void FindBar::queryChanged()
{
    currentQuery.text = findField.text();

    onQueryChanged();
    repaint();
}

void FindBar::setMatchCount(int current, int total)
{
    if (currentQuery.isEmpty())
    {
        counterText.clear();
        foundNothing = false;
    }
    else if (total == 0)
    {
        counterText = noResults;
        foundNothing = true;
    }
    else
    {
        counterText = std::to_string(current) + " of " + std::to_string(total);
        foundNothing = false;
    }

    repaint();
}

FindBar::Control FindBar::controlAt(const Graphics::Point& point) const
{
    for (const auto& hotspot: hotspots)
        if (hotspot.area.contains(point))
            return hotspot.control;

    return Control::none;
}

void FindBar::activate(Control control)
{
    switch (control)
    {
        case Control::caseSensitive:
            currentQuery.caseSensitive = !currentQuery.caseSensitive;
            rebuildHotspots();
            queryChanged();
            break;

        case Control::wholeWord:
            currentQuery.wholeWord = !currentQuery.wholeWord;
            rebuildHotspots();
            queryChanged();
            break;

        case Control::previous:
            onFindPrevious();
            break;

        case Control::next:
            onFindNext();
            break;

        case Control::close:
            hide();
            break;

        case Control::replaceOne:
            onReplace();
            break;

        case Control::replaceEvery:
            onReplaceAll();
            break;

        case Control::none:
            break;
    }
}

void FindBar::mouseDown(const Graphics::MouseEvent& event)
{
    activate(controlAt(event.pos));
}

bool FindBar::keyDown(const Graphics::KeyEvent& event)
{
    // Reached only when neither field consumed the key: TextField deliberately
    // passes Return, Escape and Tab up, because what they mean is the owner's to
    // decide. Here Return is "find", Escape is "close" and Tab moves between the
    // two fields.
    switch (event.keyCode)
    {
        case Graphics::KeyCode::Escape:
            hide();
            return true;

        case Graphics::KeyCode::Return:
            // Shift+Return searches backwards, matching the shortcut pair and
            // every other editor.
            if (event.modifiers.shift)
                onFindPrevious();
            else
                onFindNext();

            return true;

        case Graphics::KeyCode::Tab:
            // Between the two fields, rather than out of the bar. With no
            // replace row there is nowhere to go, and the key is swallowed so it
            // does not fall through and indent the document behind.
            if (!replaceShown)
                return true;

            if (findField.hasFocus())
                onFocusRequested(replaceField);
            else
                onFocusRequested(findField);

            return true;

        default:
            return false;
    }
}

void FindBar::prepare(Text::GlyphAtlas& atlas, const Graphics::Rect&)
{
    for (const auto& hotspot: hotspots)
        UIText::prepare(atlas, hotspot.label);

    UIText::prepare(atlas, counterText);
}

void FindBar::paint(PaintContext& context)
{
    const auto area = bounds();

    context.sprites().fillRect(area, theme.findBackground);

    // A one-point outline in place of a shadow, which the sprite renderer cannot
    // draw. Without it the bar has no edge against the editor behind it.
    context.sprites().fillRect(area.withHeight(borderWidth), theme.findBorder);
    context.sprites().fillRect(
        {area.x, area.bottom() - borderWidth, area.w, borderWidth},
        theme.findBorder);
    context.sprites().fillRect({area.x, area.y, borderWidth, area.h},
                               theme.findBorder);
    context.sprites().fillRect(
        {area.right() - borderWidth, area.y, borderWidth, area.h}, theme.findBorder);

    // The fields get a well of their own so the caret and the placeholder read
    // as somewhere to type rather than as text lying on the bar.
    context.sprites().fillRect(
        findField.bounds().inset(-fieldInset, -fieldInset + 1.f),
        theme.findFieldBackground);

    if (replaceShown)
        context.sprites().fillRect(
            replaceField.bounds().inset(-fieldInset, -fieldInset + 1.f),
            theme.findFieldBackground);

    for (const auto& hotspot: hotspots)
    {
        if (hotspot.isToggle && hotspot.isOn)
            context.sprites().fillRect(hotspot.area.inset(1.f, 4.f),
                                       theme.findToggleOn);

        const auto baseline = UIText::centredBaseline(context.atlas(), hotspot.area);

        UIText::draw(context,
                     hotspot.label,
                     centredTextX(context.atlas(), hotspot.area, hotspot.label),
                     baseline,
                     hotspot.isToggle && hotspot.isOn ? theme.findToggleOnText
                                                      : theme.findText);
    }

    if (counterText.empty())
        return;

    // Right-aligned against the buttons, so the number does not shuffle sideways
    // as it grows from "1 of 9" to "10 of 90".
    const auto counterRight = bounds().right() - padding - buttonWidth * 3.f - gap;

    UIText::draw(context,
                 counterText,
                 counterRight - UIText::width(context.atlas(), counterText),
                 UIText::centredBaseline(context.atlas(), findRowBounds()),
                 foundNothing ? theme.findNoResults : theme.findHintText);
}
} // namespace ecode
