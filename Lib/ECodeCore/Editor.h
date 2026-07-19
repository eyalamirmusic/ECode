#pragma once

#include "Cursor.h"
#include "Document.h"
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
    const Cursor& cursor() const { return caret; }

    void setDocument(Document documentToUse);

    // --- editing ---------------------------------------------------------

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

    std::string selectedText() const;

    void selectAll();
    void selectWordAt(std::size_t offset);
    void selectLineAt(std::size_t offset);

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

    // Movement shares this: collapse or extend, then clear the held column
    // unless the caller is moving vertically.
    void applyMotion(std::size_t offset, bool extend);

    Document doc;
    Cursor caret;
    EditHistory history;

    std::uint64_t revision = 0;
};
} // namespace ecode
