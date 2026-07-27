#pragma once

#include <ECodeWidgets/Widget.h>

#include <ECodeCore/OpenFile.h>
#include <ECodeRender/TextRenderer.h>

#include <functional>
#include <string_view>

namespace ecode
{
// The text view: whichever file is open, and the keyboard and mouse handling
// that turns input into edits.
//
// Points at an OpenFile rather than owning one, and switches which by being
// pointed somewhere else. Everything about a file that a view would otherwise
// have to save and restore around a tab switch — the scroll offset, the
// highlighter's tree — lives on the OpenFile, so switching is one assignment
// and there is no bookkeeping step to forget.
//
// The renderer is *not* owned either: it is rebuilt whenever the display's
// backing scale changes, and the view above owns that lifetime.
class EditorWidget final : public Widget
{
public:
    explicit EditorWidget(OpenFile& fileToEdit)
        : open(&fileToEdit)
    {
    }

    // Null until the atlas has been built, which cannot happen until the view
    // is on a display and its scale is known. Everything here tolerates that.
    void setRenderer(TextRenderer* rendererToUse);

    // Shows a different open file. Cheap — no text is copied and nothing is
    // reparsed, because the incoming file kept its own tree the whole time.
    void setFile(OpenFile& fileToEdit);

    OpenFile& openFile() { return *open; }
    TextFile& textFile() { return open->file; }
    Editor& editor() { return open->file.editor(); }
    const Editor& editor() const { return open->file.editor(); }

    Highlighter* highlighter() const { return open->highlighter.get(); }

    // Where this file is scrolled to, in points, negative down and right.
    // Exposed for the tests that check a switch does not lose it.
    ScrollOffset scrollOffset() const { return open->scroll; }

    // The document line at the top of the viewport, and putting one back there.
    //
    // A pair, because a change of font size invalidates the offset above: it is
    // in points, and the row height it was measured against has just moved, so
    // holding it fixed slides the file under whoever is reading it. The line
    // rather than the row, since the wrap width moves with the font too and a
    // row index taken before the change names different text after it.
    //
    // Line 0 before there is a renderer, which is also the right answer then.
    std::size_t topVisibleLine() const;
    void scrollToTopLine(std::size_t line);

    // The same pair across, and for the same reason: the offset is in points
    // and the column width moves with the font, so ⌘+ while scrolled across
    // would leave the view on a different part of the line.
    //
    // Counted in whole columns of the uniform grid rather than against any one
    // line's tab stops, because the left edge is a property of the viewport and
    // not of a line — nothing here knows which line is being read.
    std::size_t leftVisibleColumn() const;
    void scrollToLeftColumn(std::size_t column);

    // Inside *any* selection, half-open, matching Cursor's own range: an offset
    // at the very end of a selection is past it, which is where a click lands
    // when someone aims just beyond the last selected character.
    //
    // Any rather than the primary's, because the caller is the right-click that
    // decides whether to collapse before opening a menu — and collapsing a
    // multi-cursor selection because the click missed the primary is the same
    // mistake in a louder form.
    bool isInsideSelection(std::size_t offset) const
    {
        for (const auto& caret: editor().cursors())
            if (caret.hasSelection() && caret.covers(offset))
                return true;

        return false;
    }
    const Document& document() const { return open->file.document(); }

    // --- read-only -------------------------------------------------------

    // A view that cannot be typed into, which is what a host embedding ECode to
    // *show* a file asks for.
    //
    // Everything that only reads goes on working — moving the caret, selecting,
    // copying, scrolling, searching — because a viewer nobody can select out of
    // is not a viewer, and a caret that will not move is indistinguishable from
    // a window that has stopped responding.
    //
    // Enforced on the view rather than on Editor, and that is the whole reason
    // it is a flag here: read-only is a property of *this view of* the document,
    // not of the document. A host showing one file in two panes may want one of
    // them editable, and one that wants to change the text itself still can
    // through editor() — which is what a "revert" or a "format" button is.
    //
    // Refuses by not consuming the key rather than by swallowing it, so a
    // Return the editor will not act on still reaches whatever the host bound
    // it to.
    void setReadOnly(bool shouldBeReadOnly) { readOnly = shouldBeReadOnly; }
    bool isReadOnly() const { return readOnly; }

    // --- soft wrap -------------------------------------------------------

    // Wrapping is off by default, which is what a code editor wants: code is
    // written to a column limit and a wrapped line hides the indentation that
    // says what it belongs to.
    void setWordWrap(bool shouldWrap);
    bool isWordWrapped() const { return wordWrap; }

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
    // the end rather than looping — or when the view is read-only, since a
    // replace is an edit however it was reached.
    void replaceCurrent(std::string_view replacement);

    // Returns how many were replaced, so zero is also the read-only answer. One
    // undo step for the lot.
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
    //
    // Brings the caret into view as well, but only when it could have moved
    // since the last call. See the definition for why that condition is there.
    void wake();

    // Line and column of the caret, 1-based, for the status bar.
    std::size_t caretLine() const;
    std::size_t caretColumn() const;

    // Called after anything that may have changed the file's dirty state or the
    // caret position, so the chrome around this widget can follow.
    std::function<void()> onStateChanged = [] {};

private:
    // What the renderer draws: the document, its row mapping and its colours.
    DocumentView documentView() const;

    // The wrap width follows the viewport, so it is recomputed rather than
    // stored. Cheap because LineMap ignores a width it already has.
    void updateWrapWidth();

    void clampScroll();

    // Apart from the vertical clamp because it is the one that can cost
    // something: the horizontal range is the widest line, which Document
    // rescans whenever an edit takes the record off it. See the definition for
    // what keeps that off the keystroke path.
    void clampScrollColumn();

    void scrollToCaret();
    void scrollToCaretRow();
    void scrollToCaretColumn();

    // Whether the text or the primary caret has changed since the last wake,
    // which is what decides whether the view follows it. The document's
    // revision as well as the offset, because an edit that leaves the caret
    // where it was still moves what is under it.
    bool caretHasMoved() const;
    void rememberCaret();
    void scrollToRow(std::size_t row);
    int visibleRows() const;

    // Puts the caret on the current hit and brings it into view. The shared tail
    // of findNext, findPrevious and a replace that moves on.
    void goToCurrentMatch();

    void refreshSearch();

    // Never null: the workspace always has an active file, so there is always
    // something to point at and no caller has to handle "no document".
    OpenFile* open;

    Search finder;

    // The document revision the match list was built from, so a stale list can
    // be spotted without comparing text.
    std::uint64_t searchedVersion = 0;

    TextRenderer* renderer = nullptr;

    // Kept on the view rather than on the file: ⌥Z is a property of how the
    // text is being looked at, and the View menu presents it that way.
    bool wordWrap = false;

    // See setReadOnly. Editable by default, so the full app needs no call.
    bool readOnly = false;

    bool caretVisible = true;
    int blinkPhase = 0;

    // What the view last followed the caret to. Refreshed on a file switch as
    // well, or the first wake in an incoming tab would compare against the
    // outgoing file's numbers and scroll away the offset the switch restored.
    std::uint64_t wokeAtVersion = 0;
    std::size_t wokeAtCaret = 0;
};
} // namespace ecode
