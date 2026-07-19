#include "TextField.h"

#include "Keymap.h"
#include "UIText.h"

#include <ECodeCore/Utf8.h>

#include <eacp/Core/App/Clipboard.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace ecode
{
using namespace eacp;

namespace
{
constexpr auto horizontalPadding = 8.f;
constexpr auto caretWidth = 2.f;

// Text the key produced, or nothing when it is not text.
//
// Two separate things have to be excluded. Return, Tab and Escape arrive with
// `characters` set to a control code, so a field that appended whatever came in
// would type them into the string. And every *function* key — the arrows
// included — arrives as a codepoint in Unicode's private use area, because that
// is where AppKit puts them: NSUpArrowFunctionKey is U+F700, which encodes to
// three perfectly ordinary-looking UTF-8 bytes and passes a control-character
// test unharmed. A field that only checked for control codes would insert a
// box-drawing character every time someone pressed Up.
bool isTypedText(const Graphics::KeyEvent& event)
{
    if (event.characters.empty() || event.modifiers.command
        || event.modifiers.control)
        return false;

    auto index = std::size_t {0};

    while (index < event.characters.size())
    {
        const auto codepoint = Utf8::next(event.characters, index);

        if (codepoint < 0x20)
            return false;

        if (codepoint >= 0xe000 && codepoint <= 0xf8ff)
            return false;
    }

    return true;
}
} // namespace

TextField::TextField(const ChromeTheme& themeToUse)
    : theme(themeToUse)
{
    colours = {theme.paletteText,
               theme.paletteHintText,
               theme.paletteText,
               theme.paletteSelected};
}

void TextField::setText(std::string newText)
{
    content = std::move(newText);

    moveCaretToEnd();
    repaint();
}

void TextField::moveCaretToEnd()
{
    head = content.size();
    anchor = head;
}

void TextField::selectAll()
{
    anchor = 0;
    head = content.size();

    repaint();
}

void TextField::moveCaret(std::size_t to, bool extend)
{
    head = std::min(to, content.size());

    if (!extend)
        anchor = head;

    repaint();
}

void TextField::replaceSelection(std::string_view insertion)
{
    const auto from = selectionStart();
    const auto to = selectionEnd();

    content.replace(from, to - from, insertion);

    head = from + insertion.size();
    anchor = head;

    onTextChanged(content);
    repaint();
}

void TextField::deleteBackwards()
{
    if (!hasSelection())
    {
        if (head == 0)
            return;

        anchor = Utf8::previousBoundary(content, head);
    }

    replaceSelection({});
}

void TextField::deleteForwards()
{
    if (!hasSelection())
    {
        if (head >= content.size())
            return;

        anchor = Utf8::nextBoundary(content, head);
    }

    replaceSelection({});
}

void TextField::prepare(Text::GlyphAtlas& atlas, const Graphics::Rect&)
{
    UIText::prepare(atlas, content.empty() ? placeholderText : content);

    // One walk, recording the pen at every boundary — the same sum
    // UIText::width takes, kept per step instead of only at the end. Measuring
    // each prefix separately would be quadratic in the query length.
    boundaries.clear();
    boundaries.push_back({0, 0.f});

    auto pen = 0.f;

    for (std::size_t index = 0; index < content.size();)
    {
        const auto codepoint = Utf8::next(content, index);

        pen += atlas.glyph(codepoint, Text::FontStyle::Regular).advance;

        boundaries.push_back({index, pen});
    }
}

float TextField::xOfOffset(std::size_t offset) const
{
    for (const auto& boundary: boundaries)
        if (boundary.offset == offset)
            return boundary.x;

    // Before the first prepare there is nothing measured, and after one every
    // caret position is a boundary — so this is only reachable in the frame
    // before the field is first drawn.
    return 0.f;
}

std::size_t TextField::offsetAtX(float x) const
{
    if (boundaries.empty())
        return 0;

    const auto relative = x - (bounds().x + horizontalPadding);

    // Nearest boundary rather than the one before, so clicking the right half of
    // a character puts the caret after it.
    auto best = boundaries[0];

    for (const auto& boundary: boundaries)
        if (std::abs(relative - boundary.x) < std::abs(relative - best.x))
            best = boundary;

    return best.offset;
}

void TextField::paint(PaintContext& context)
{
    const auto area = bounds();
    const auto textLeft = area.x + horizontalPadding;
    const auto baseline = UIText::centredBaseline(context.atlas(), area);
    const auto metrics = context.atlas().metrics();

    if (hasSelection())
    {
        const auto from = textLeft + xOfOffset(selectionStart());
        const auto to = textLeft + xOfOffset(selectionEnd());

        context.sprites().fillRect({from,
                                    baseline - metrics.ascent,
                                    to - from,
                                    metrics.ascent + metrics.descent},
                                   colours.selection);
    }

    if (content.empty())
        UIText::draw(
            context, placeholderText, textLeft, baseline, colours.placeholder);
    else
        UIText::draw(context, content, textLeft, baseline, colours.text);

    // Solid rather than blinking. The blink timer belongs to the editor's caret;
    // a find field is on screen for a few seconds at a time, and a second caret
    // pulsing out of phase with the document's reads as a glitch.
    if (focused)
        context.sprites().fillRect({textLeft + xOfOffset(head),
                                    baseline - metrics.ascent,
                                    caretWidth,
                                    metrics.ascent + metrics.descent},
                                   colours.caret);
}

void TextField::focusGained()
{
    focused = true;
    repaint();
}

void TextField::focusLost()
{
    focused = false;
    repaint();
}

void TextField::mouseDown(const Graphics::MouseEvent& event)
{
    moveCaret(offsetAtX(event.pos.x), event.modifiers.shift);
}

void TextField::mouseDragged(const Graphics::MouseEvent& event)
{
    // Always an extension: the anchor was set on mouse-down.
    moveCaret(offsetAtX(event.pos.x), true);
}

std::string TextField::selectedText() const
{
    return content.substr(selectionStart(), selectionEnd() - selectionStart());
}

bool TextField::keyDown(const Graphics::KeyEvent& event)
{
    const auto extend = event.modifiers.shift;

    // The four chords that mean *this box* rather than the document behind it.
    // Everything else with ⌘ held is the application's — see Widget::isTextInput
    // for who decides and why this short list is where the line falls.
    if (event.modifiers.command)
    {
        const auto chord = Chord::fromEvent(event).key;

        if (chord == "a")
        {
            selectAll();
            return true;
        }

        if (chord == "c" || chord == "x")
        {
            if (!hasSelection())
                return true;

            Clipboard::copyText(selectedText());

            if (chord == "x")
                replaceSelection({});

            return true;
        }

        if (chord == "v")
        {
            if (!Clipboard::hasText())
                return true;

            auto pasted = Clipboard::getText();

            // A single-line box. Pasting a multi-line clipboard would put a
            // newline somewhere it can never otherwise appear — Return is not
            // typed into a field — and the query would then match nothing while
            // looking perfectly ordinary.
            std::erase(pasted, '\n');
            std::erase(pasted, '\r');

            replaceSelection(pasted);

            return true;
        }

        return false;
    }

    switch (event.keyCode)
    {
        case Graphics::KeyCode::Return:
        case Graphics::KeyCode::Escape:
        case Graphics::KeyCode::Tab:
            return false;

        case Graphics::KeyCode::LeftArrow:
            // Collapsing a selection leftwards goes to its start rather than
            // stepping back from the head, which is what every editor does.
            if (!extend && hasSelection())
                moveCaret(selectionStart(), false);
            else
                moveCaret(Utf8::previousBoundary(content, head), extend);

            return true;

        case Graphics::KeyCode::RightArrow:
            if (!extend && hasSelection())
                moveCaret(selectionEnd(), false);
            else
                moveCaret(Utf8::nextBoundary(content, head), extend);

            return true;

        case Graphics::KeyCode::Home:
            moveCaret(0, extend);
            return true;

        case Graphics::KeyCode::End:
            moveCaret(content.size(), extend);
            return true;

        case Graphics::KeyCode::Delete:
            deleteBackwards();
            return true;

        case Graphics::KeyCode::ForwardDelete:
            deleteForwards();
            return true;

        default:
            break;
    }

    if (!isTypedText(event))
        return false;

    replaceSelection(event.characters);

    return true;
}
} // namespace ecode
