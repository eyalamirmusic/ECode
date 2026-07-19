#pragma once

#include "Widget.h"

#include <ECodeCore/TextFile.h>
#include <ECodeRender/TextRenderer.h>

#include <functional>
#include <string_view>

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
    void setHighlighter(Highlighter* highlighterToUse)
    {
        highlighter = highlighterToUse;
    }

    TextFile& textFile() { return file; }
    Editor& editor() { return file.editor(); }
    const Editor& editor() const { return file.editor(); }

    // Half-open, matching Cursor's own range: an offset at the very end of a
    // selection is past it, which is where a click lands when someone aims just
    // beyond the last selected character.
    bool isInsideSelection(std::size_t offset) const
    {
        const auto& caret = editor().cursor();

        return caret.hasSelection() && offset >= caret.start()
               && offset < caret.end();
    }
    const Document& document() const { return file.document(); }

    // --- find and replace ------------------------------------------------
    //
    // The search lives here rather than in the find bar because everything it
    // needs is here: the document to search, the scroll offset to bring a match
    // into view, and the renderer that draws the hits. The bar is the query and
    // the buttons, and pushes both at this.

    const Search& search() const { return finder; }

    // Recomputes the matches and moves to the one at or after `from`, so typing
    // in the find field carries on from where the work is rather than jumping to
    // the top of the file on every keystroke. Does not move the caret: a search
    // that is still being typed should not take the insertion point with it.
    void setSearchQuery(const SearchQuery& query, std::size_t from);

    // Clears the query, so nothing is highlighted and the count reads zero.
    void clearSearch();

    // Move to the next or previous hit and select it, wrapping at both ends.
    // Selecting rather than only scrolling is what makes ⌘F then Escape leave
    // the caret on what was being looked for.
    void findNext();
    void findPrevious();

    // Replaces the current hit and moves to the one after it. Does nothing when
    // there is no current hit, which is what makes holding the button stop at
    // the end rather than looping.
    void replaceCurrent(std::string_view replacement);

    // Returns how many were replaced. One undo step for the lot.
    int replaceAllMatches(std::string_view replacement);

    bool wantsMouse() const override { return true; }
    bool acceptsFocus() const override { return true; }

    void layout() override;

    void prepare(eacp::Text::GlyphAtlas& atlas,
                 const eacp::Graphics::Rect& visible) override;
    void paint(PaintContext& context) override;

    // A right-click, after the caret has been moved to it. The widget does not
    // own the menu — the commands it offers belong to the application, and a
    // widget that popped its own would need the registry to build one.
    std::function<void(const eacp::Graphics::Point&)> onContextMenuRequested =
        [](const eacp::Graphics::Point&) {};

    // An I-beam over the text, which is the shape that says "this is
    // selectable" before anyone tries selecting it.
    eacp::Graphics::MouseCursor cursor() const override
    {
        return eacp::Graphics::MouseCursor::IBeam;
    }

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
    void scrollToLine(std::size_t line);
    int visibleLines() const;

    // Puts the caret on the current hit and brings it into view. The shared tail
    // of findNext, findPrevious and a replace that moves on.
    void goToCurrentMatch();

    void refreshSearch();

    TextFile& file;

    Search finder;

    // The document revision the match list was built from, so a stale list can
    // be spotted without comparing text.
    std::uint64_t searchedVersion = 0;

    TextRenderer* renderer = nullptr;
    Highlighter* highlighter = nullptr;

    float scrollY = 0.f;

    bool caretVisible = true;
    int blinkPhase = 0;
};
} // namespace ecode
