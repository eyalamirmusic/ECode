#pragma once

#include "Widget.h"

#include <ECodeCore/TextFile.h>
#include <ECodeRender/TextRenderer.h>

#include <functional>

namespace ecode
{
// The text view: a file, a scroll offset, and the keyboard and mouse handling
// that turns input into edits.
//
// Holds the file rather than a bare Editor because a tab owns a file, and the
// scroll offset belongs to the view of it rather than to the document. The
// renderer is *not* owned: it is rebuilt whenever the display's backing scale
// changes, and the view above owns that lifetime.
class EditorWidget final : public Widget
{
public:
    explicit EditorWidget(TextFile& fileToEdit)
        : file(fileToEdit)
    {
    }

    // Null until the atlas has been built, which cannot happen until the view
    // is on a display and its scale is known. Everything here tolerates that.
    void setRenderer(TextRenderer* rendererToUse);
    void setHighlighter(Highlighter* highlighterToUse) { highlighter = highlighterToUse; }

    TextFile& textFile() { return file; }
    Editor& editor() { return file.editor(); }
    const Editor& editor() const { return file.editor(); }
    const Document& document() const { return file.document(); }

    bool wantsMouse() const override { return true; }
    bool acceptsFocus() const override { return true; }

    void layout() override;

    void prepare(eacp::Text::GlyphAtlas& atlas,
                 const eacp::Graphics::Rect& visible) override;
    void paint(PaintContext& context) override;

    void mouseDown(const eacp::Graphics::MouseEvent& event) override;
    void mouseDragged(const eacp::Graphics::MouseEvent& event) override;
    bool mouseWheel(const eacp::Graphics::MouseEvent& event) override;
    bool keyDown(const eacp::Graphics::KeyEvent& event) override;

    void focusGained() override;
    void focusLost() override;

    // Drives the caret's blink from the view's timer. Returns true when the
    // screen needs to change, so an unfocused editor costs no frames.
    bool tickCaretBlink();

    // Any interaction restarts the blink, so the caret is solid while working
    // and only pulses when idle — one that blinks out mid-keystroke reads as
    // dropped input.
    void wake();

    // Line and column of the caret, 1-based, for the status bar.
    std::size_t caretLine() const;
    std::size_t caretColumn() const;

    // Called after anything that may have changed the file's dirty state or the
    // caret position, so the chrome around this widget can follow.
    std::function<void()> onStateChanged = [] {};

private:
    void clampScroll();
    void scrollToCaret();
    int visibleLines() const;

    TextFile& file;

    TextRenderer* renderer = nullptr;
    Highlighter* highlighter = nullptr;

    float scrollY = 0.f;

    bool caretVisible = true;
    int blinkPhase = 0;
};
} // namespace ecode
