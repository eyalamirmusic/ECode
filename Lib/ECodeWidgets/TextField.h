#pragma once

#include "Theme.h"
#include "Widget.h"

#include <functional>
#include <string>

namespace ecode
{
// A single-line text input: a string, a caret, a selection, and the keys that
// change them.
//
// Deliberately not an Editor. A find query and a palette query are short strings
// typed and thrown away, and giving them the document machinery would mean a
// Document, an undo history and a line index per input field. What they do need
// is a caret that moves — the palette's hand-rolled query had none, so a typo
// three characters back could only be fixed by erasing everything after it.
//
// **It never consumes Return, Escape or Tab.** Those keys mean different things
// in different fields — Return is "run this command" in the palette and "find
// the next one" in the find bar — so the field returns false for them and the
// widget that owns it decides. Keeping that decision out of here is what lets
// one field serve both.
class TextField final : public Widget
{
public:
    explicit TextField(const ChromeTheme& themeToUse);

    // Replaces the contents and puts the caret at the end, with nothing
    // selected. Does not fire onTextChanged: the caller setting the text already
    // knows what it set, and a callback here turns "seed the field from the
    // selection" into a search the person did not ask for.
    void setText(std::string newText);
    const std::string& text() const { return content; }
    bool isEmpty() const { return content.empty(); }

    // Drawn in the hint colour when the field is empty.
    void setPlaceholder(std::string text) { placeholderText = std::move(text); }

    // Colours are set rather than taken from the theme by name, because the same
    // field sits on the palette's background in one place and the find bar's in
    // another, and a widget that picked one of them would only ever suit that
    // one.
    struct Colours
    {
        eacp::Graphics::Color text;
        eacp::Graphics::Color placeholder;
        eacp::Graphics::Color caret;
        eacp::Graphics::Color selection;
    };

    void setColours(const Colours& newColours) { colours = newColours; }

    // Inset from the field's own left edge to the first character. Settable
    // because the two users disagree: the find bar's fields sit in a well drawn
    // tight around them, and the palette's query is a full-width line inside a
    // box that already carries its own margin. Without this the palette would
    // have to hand the field a rect offset by the difference, which puts a
    // layout constant in one place and its correction in another.
    void setHorizontalPadding(float padding) { horizontalPadding = padding; }

    // Byte offsets into text(), always on character boundaries.
    std::size_t caret() const { return head; }
    std::size_t selectionStart() const { return head < anchor ? head : anchor; }
    std::size_t selectionEnd() const { return head < anchor ? anchor : head; }
    bool hasSelection() const { return head != anchor; }

    // Selects everything, so the next keystroke replaces it. What opening the
    // find bar over an existing query does: the old one is still visible and
    // still usable, and typing starts a new one without a trip to Backspace.
    void selectAll();

    void moveCaretToEnd();

    // The caret is drawn only while the field has focus. Two fields side by side
    // — find and replace — would otherwise both show one, and two carets mean
    // the person cannot tell where their typing is going.
    bool hasFocus() const { return focused; }

    void focusGained() override;
    void focusLost() override;

    std::function<void(const std::string&)> onTextChanged =
        [](const std::string&) {};

    bool wantsMouse() const override { return true; }
    bool acceptsFocus() const override { return true; }
    bool isTextInput() const override { return true; }

    // The four editing commands that mean *this box* rather than the document
    // behind it. Claimed whether or not there is anything to do, so a ⌘C with
    // no selection is swallowed here rather than falling through and copying
    // from the file. Everything else is the application's.
    bool runCommand(std::string_view id) override;

    // Selected text, for ⌘C and ⌘X.
    std::string selectedText() const;

    void prepare(eacp::Text::GlyphAtlas& atlas,
                 const eacp::Graphics::Rect& visible) override;
    void paint(PaintContext& context) override;

    void mouseDown(const eacp::Graphics::MouseEvent& event) override;
    void mouseDragged(const eacp::Graphics::MouseEvent& event) override;
    bool keyDown(const eacp::Graphics::KeyEvent& event) override;

    // Byte offset nearest a point, for click-to-place.
    //
    // Answered from the measurements the last prepare() took, because measuring
    // needs the atlas and the atlas is only in hand during the prepare/paint
    // walk. A frame stale in principle and never in practice: nothing moves the
    // text between the frame that drew it and the click on it, and a field that
    // has never been drawn has nothing to click on.
    std::size_t offsetAtX(float x) const;

private:
    void replaceSelection(std::string_view insertion);
    void deleteBackwards();
    void deleteForwards();
    void moveCaret(std::size_t to, bool extend);

    const ChromeTheme& theme;

    std::string content;
    std::string placeholderText;

    Colours colours;

    std::size_t head = 0;
    std::size_t anchor = 0;

    float horizontalPadding = 8.f;

    bool focused = false;

    // Where every character boundary in `content` sits: its byte offset, which
    // is what a caret is, and its x relative to the text's left edge, which is
    // what drawing and clicking need. Rebuilt by each prepare().
    //
    // The two are stored together rather than the x alone so that neither
    // direction needs a conversion walk — a click reads an offset straight off
    // the nearest entry, and the caret finds its x by the offset it already has.
    // There is one more entry than there are characters: the last is the end of
    // the string, which is where the caret sits when typing at the end.
    struct Boundary
    {
        std::size_t offset = 0;
        float x = 0.f;
    };

    eacp::Vector<Boundary> boundaries;

    float xOfOffset(std::size_t offset) const;
};
} // namespace ecode
