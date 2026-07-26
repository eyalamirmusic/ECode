#pragma once

#include "Cursor.h"
#include "Document.h"
#include "LineMap.h"
#include "Search.h"
#include "TextEdit.h"

#include <functional>
#include <string>
#include <string_view>

namespace ecode
{
// A document, a cursor and an undo history, kept consistent with each other.
//
// This is where the invariants live that are easy to break when editing is
// spread across a view class: that every mutation goes through the history,
// that the cursor lands somewhere sensible afterwards, and that typing over a
// selection replaces it. None of it touches the GPU or the platform, so it can
// be tested by driving it and reading back the text.
class Editor
{
public:
    Editor() = default;

    explicit Editor(Document documentToUse)
        : doc(std::move(documentToUse))
    {
    }

    const Document& document() const { return doc; }

    // The primary cursor. Kept as the name it has always had because most
    // callers genuinely want that one — the status bar's readout, the offset a
    // search resumes from, the caret the view scrolls to. Anything that has to
    // see all of them asks for cursors() instead.
    const Cursor& cursor() const { return carets.primary(); }
    const CursorSet& cursors() const { return carets; }

    // How the document's lines map onto rows on screen.
    //
    // Owned here, though a wrap width is a property of a view, because vertical
    // movement is the map's first caller and the cursor is the editor's. A map
    // kept outside would mean either the view reaching into the cursor — which
    // PLAN.md §7.2 is explicit about not doing — or every mutation needing a
    // subscriber that a caller can forget to attach.
    //
    // The price is that two views of one file would share a wrap width. That is
    // the point at which this becomes a view model; until editor groups exist
    // there is exactly one view per file.
    const LineMap& lineMap() const { return rows; }

    // Zero turns wrapping off. The view supplies it, since only the view knows
    // how wide the text area is.
    void setWrapColumns(std::size_t columns) { rows.setWrapColumns(doc, columns); }
    std::size_t wrapColumns() const { return rows.wrapColumns(); }

    void setDocument(Document documentToUse);

    // --- editing ---------------------------------------------------------
    //
    // Every one of these applies at every cursor, as one thing to undo.

    // Replaces the selection if there is one, otherwise inserts at the caret.
    void insert(std::string_view text);

    // Backspace: deletes the selection, or one character before the caret.
    void backspace();

    // Forward delete: the selection, or one character after the caret.
    void deleteForward();

    // Whole-word deletion, for Alt+Backspace and Alt+Delete.
    void deleteWordBefore();
    void deleteWordAfter();

    void undo();
    void redo();

    bool canUndo() const { return history.canUndo(); }
    bool canRedo() const { return history.canRedo(); }

    // --- selection -------------------------------------------------------

    // Every selection, in document order, joined by newlines. One string
    // rather than a list because the only caller is the clipboard, and the
    // alternative — the primary's selection alone — silently drops the rest of
    // what a person selected before copying it.
    std::string selectedText() const;

    void selectAll();
    void selectWordAt(std::size_t offset);
    void selectLineAt(std::size_t offset);

    // --- more than one cursor --------------------------------------------

    // ⌥-click: a caret at `offset`, or the removal of the one already there.
    // Toggling rather than only adding, because a click that lands one pixel
    // into the wrong character otherwise has no way back except Escape and
    // starting over.
    bool toggleCursorAt(std::size_t offset);

    // ⌥⌘↑ / ⌥⌘↓: a caret one visual row above the topmost or below the
    // bottommost, at the column that one holds.
    //
    // Relative to the outermost cursor rather than to each of them, which is
    // what makes holding the chord grow a column downwards. Adding one under
    // every cursor would double the set on each press.
    bool addCursorAbove();
    bool addCursorBelow();

    // Escape. False when there was only one cursor, so the key can fall
    // through to whatever else wants it.
    bool collapseCursors();

    // ⌘D. With nothing selected this selects the word the primary is in — the
    // first press is what gives the later ones something to look for. With a
    // selection it adds a cursor on the next occurrence of that text, wrapping
    // at the end of the file.
    bool selectNextOccurrence();

    // ⇧⌘L: a cursor on every occurrence, with the one already under the primary
    // staying primary so the view does not jump to the last hit in the file.
    bool selectAllOccurrences();

    // --- movement --------------------------------------------------------
    //
    // `extend` is the shift key: keep the anchor and grow the selection rather
    // than collapsing it.

    void moveLeft(bool extend = false);
    void moveRight(bool extend = false);
    void moveWordLeft(bool extend = false);
    void moveWordRight(bool extend = false);
    void moveUp(bool extend = false, int lines = 1);
    void moveDown(bool extend = false, int lines = 1);
    void moveToLineStart(bool extend = false);
    void moveToLineEnd(bool extend = false);
    void moveToDocumentStart(bool extend = false);
    void moveToDocumentEnd(bool extend = false);

    // Places the caret at an offset, e.g. from a mouse click.
    void placeCaret(std::size_t offset, bool extend = false);

    // Ends the current undo step. Anything that should not merge with
    // surrounding typing calls this — a caret move, a save, a paste.
    void breakUndoStep() { history.breakStep(); }

    // Prefer the UndoGroup below to calling these directly.
    void beginUndoGroup() { history.beginGroup(); }
    void endUndoGroup() { history.endGroup(); }

    // Bumped on every change, so a view can tell whether it needs to re-run a
    // highlighter or re-measure without comparing the whole text.
    std::uint64_t version() const { return revision; }

    // Which text this is, as opposed to how many times it has changed.
    // Undoing back to an earlier state returns the id that state had, so a
    // caller holding one can ask "is this still the text I saved?". See
    // EditHistory::stateId.
    std::uint64_t stateId() const { return history.stateId(); }

    // Called with every edit as it is applied, including the inverses undo and
    // redo apply. A syntax engine subscribes to reparse incrementally rather
    // than rebuilding from the whole text. Non-null by default so the editor
    // can invoke it without a check.
    std::function<void(const TextEdit&)> onEdit = [](const TextEdit&) {};

    // Called when the document is replaced wholesale, where an incremental
    // update makes no sense.
    std::function<void()> onDocumentReplaced = [] {};

private:
    // Applies an edit through the history so it is undoable, and returns where
    // the caret should land after it.
    std::size_t applyEdit(std::size_t start, std::size_t end, std::string_view text);

    // Where one cursor is going, asked once per cursor.
    using Destination = std::function<std::size_t(const Cursor&)>;

    // Moves every cursor to wherever `destination` sends it. Two that walk into
    // each other stop being two, which CursorSet::transform does on the way out.
    void moveEach(const Destination& destination, bool extend);

    // Up or down by whole visual rows, which cannot go through moveEach: it
    // clears the held column, and holding that column is the entire point of
    // vertical movement.
    void moveVertically(int rowsToMove, bool extend);

    // The shape every deletion has: the selection where there is one, and
    // otherwise the span between the caret and wherever `motion` says to go.
    // The four callers differ only in the motion and the direction, and the
    // check that the motion actually moved covers both ends of the document.
    void deleteWith(std::size_t (*motion)(const Document&, std::size_t),
                    bool backwards);

    // What ⌘D and ⇧⌘L look for: the primary's selection, matched
    // case-sensitively, and as a whole word when the selection *is* one.
    //
    // Decided from the text rather than remembered from the press that
    // produced it, so there is no flag to fall out of step with the selection.
    // ⌘D on `i` finding every `i` inside every identifier is the case this
    // exists for.
    SearchQuery occurrenceQuery() const;

    // A selection over a match, pointing forwards.
    static Cursor selectionOver(const SearchMatch& match);

    Document doc;
    CursorSet carets;
    EditHistory history;
    LineMap rows;

    std::uint64_t revision = 0;
};

// Makes everything done inside it a single thing to undo.
//
// For an operation that is one action made of several edits: replace-all, and
// one edit per cursor. See EditHistory::beginGroup for why the ordinary merge
// rule cannot cover those.
//
// The editor's own multi-cursor edits do not use this, because they must not
// group when there is only one cursor — see GroupWhen in Editor.cpp.
class UndoGroup
{
public:
    explicit UndoGroup(Editor& editorToGroup)
        : editor(editorToGroup)
    {
        editor.beginUndoGroup();
    }

    ~UndoGroup() { editor.endUndoGroup(); }

    UndoGroup(const UndoGroup&) = delete;
    UndoGroup& operator=(const UndoGroup&) = delete;

private:
    Editor& editor;
};
} // namespace ecode
