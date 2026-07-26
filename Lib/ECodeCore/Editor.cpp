#include "Editor.h"

#include <algorithm>

namespace ecode
{
namespace
{
// Makes a multi-cursor edit one thing to undo, and leaves a single-cursor one
// exactly as it was.
//
// Deliberately not unconditional. beginGroup ends whatever step is open, so
// grouping every keystroke would break the merge rule that makes a typed word
// undo as a word rather than a letter at a time. With N cursors there is no
// such run to preserve: one keystroke is already N edits that have to land on
// the stack as one step.
class GroupWhen
{
public:
    GroupWhen(EditHistory& historyToGroup, bool shouldGroup)
        : history(shouldGroup ? &historyToGroup : nullptr)
    {
        if (history != nullptr)
            history->beginGroup();
    }

    ~GroupWhen()
    {
        if (history != nullptr)
            history->endGroup();
    }

    GroupWhen(const GroupWhen&) = delete;
    GroupWhen& operator=(const GroupWhen&) = delete;

private:
    EditHistory* history;
};

std::size_t shifted(std::size_t offset, std::ptrdiff_t delta)
{
    const auto moved = static_cast<std::ptrdiff_t>(offset) + delta;

    return static_cast<std::size_t>(std::max(moved, std::ptrdiff_t {0}));
}
} // namespace

void Editor::setDocument(Document documentToUse)
{
    doc = std::move(documentToUse);
    carets.reset({});
    history.clear();
    rows.rebuild(doc);
    ++revision;

    onDocumentReplaced();
}

std::size_t
    Editor::applyEdit(std::size_t start, std::size_t end, std::string_view text)
{
    const auto edit = doc.replace(start, end, text);

    rows.applyEdit(doc, edit);
    history.record(edit);
    ++revision;

    onEdit(edit);

    return edit.insertedEnd();
}

void Editor::insert(std::string_view text)
{
    const auto grouped = GroupWhen {history, carets.hasMultiple()};

    // How far the edits already made have moved the text below them. Cursors
    // are visited in document order, so each one's recorded offsets are stale
    // by exactly this much and by nothing else.
    //
    // PLAN.md §7.2 originally called for the highest offset first, which needs
    // no running total for the cursors not yet reached — but then needs every
    // cursor already done shifted by each later edit, which is the same
    // arithmetic with an extra loop to get wrong.
    auto shift = std::ptrdiff_t {0};

    carets.transform(
        [&](Cursor& caret)
        {
            caret.head = shifted(caret.head, shift);
            caret.anchor = shifted(caret.anchor, shift);

            const auto before = static_cast<std::ptrdiff_t>(doc.length());

            // Typing over a selection replaces it. Handled here rather than at
            // each call site, because it is the case that gets forgotten.
            const auto at = applyEdit(caret.start(), caret.end(), text);

            shift += static_cast<std::ptrdiff_t>(doc.length()) - before;

            caret.moveTo(at);
        });
}

void Editor::deleteWith(std::size_t (*motion)(const Document&, std::size_t),
                        bool backwards)
{
    const auto grouped = GroupWhen {history, carets.hasMultiple()};

    auto shift = std::ptrdiff_t {0};

    carets.transform(
        [&](Cursor& caret)
        {
            // Moved with the text below it before anything else, and whether or
            // not this cursor turns out to have something of its own to delete.
            // A forward delete at the end of the file deletes nothing, and a
            // cursor left at its unshifted offset there would be pointing past
            // the end of a document the cursors below it had just shortened.
            caret.head = shifted(caret.head, shift);
            caret.anchor = shifted(caret.anchor, shift);

            auto from = caret.start();
            auto to = caret.end();

            if (!caret.hasSelection())
            {
                // Asked of the document as it is *now*, so a motion never reads
                // a length or a character boundary that has already moved.
                const auto target = motion(doc, caret.head);

                // Also the bounds check at both ends of the document: there is
                // nothing left of offset zero and nothing right of the last
                // byte, so both motions answer with the offset they were given.
                if (target == caret.head)
                    return;

                from = backwards ? target : caret.head;
                to = backwards ? caret.head : target;
            }

            const auto before = static_cast<std::ptrdiff_t>(doc.length());

            caret.moveTo(applyEdit(from, to, ""));

            shift += static_cast<std::ptrdiff_t>(doc.length()) - before;
        });
}

void Editor::backspace()
{
    deleteWith(Motion::left, true);
}

void Editor::deleteForward()
{
    deleteWith(Motion::right, false);
}

void Editor::deleteWordBefore()
{
    deleteWith(Motion::wordLeft, true);
}

void Editor::deleteWordAfter()
{
    deleteWith(Motion::wordRight, false);
}

void Editor::undo()
{
    const auto edits = history.undo();

    if (edits.empty())
        return;

    for (const auto& edit: edits)
    {
        doc.apply(edit);
        rows.applyEdit(doc, edit);
        onEdit(edit);
    }

    // Land where the change was, so undoing scrolls back to what it changed
    // rather than leaving the caret somewhere unrelated.
    //
    // One cursor, whatever there were before. Restoring the set an edit was
    // made with means recording it on the step, which is a change to
    // EditHistory — and the obvious cheap substitute, a cursor per edit in the
    // step, is wrong for the other kind of grouped edit: undoing a replace-all
    // would leave a cursor on every occurrence in the file.
    auto landed = Cursor {};
    landed.moveTo(std::min(edits.back().insertedEnd(), doc.length()));

    carets.reset(landed);

    ++revision;
}

void Editor::redo()
{
    const auto edits = history.redo();

    if (edits.empty())
        return;

    for (const auto& edit: edits)
    {
        doc.apply(edit);
        rows.applyEdit(doc, edit);
        onEdit(edit);
    }

    auto landed = Cursor {};
    landed.moveTo(std::min(edits.back().insertedEnd(), doc.length()));

    carets.reset(landed);

    ++revision;
}

std::string Editor::selectedText() const
{
    auto joined = std::string {};

    for (const auto& caret: carets)
    {
        if (!caret.hasSelection())
            continue;

        // A newline between them, which is what makes the copy round-trip:
        // pasted back into one cursor it reads as the lines that were picked
        // out, and there is no other separator a text editor could mean.
        if (!joined.empty())
            joined += '\n';

        joined += doc.text().substr(caret.start(), caret.length());
    }

    return joined;
}

void Editor::selectAll()
{
    auto whole = Cursor {};
    whole.anchor = 0;
    whole.head = doc.length();

    carets.reset(whole);
}

void Editor::selectWordAt(std::size_t offset)
{
    offset = std::min(offset, doc.length());

    auto word = Cursor {};

    // Right then left, so a click anywhere inside a word selects the whole of
    // it rather than only the half after the click.
    word.anchor = Motion::wordLeft(doc, Motion::wordRight(doc, offset));
    word.head = Motion::wordRight(doc, offset);

    carets.reset(word);
}

void Editor::selectLineAt(std::size_t offset)
{
    const auto line = doc.lineAt(std::min(offset, doc.length()));

    auto whole = Cursor {};
    whole.anchor = doc.offsetAt(line, 0);
    whole.head = doc.offsetAt(line, doc.line(line).size());

    carets.reset(whole);
}

bool Editor::toggleCursorAt(std::size_t offset)
{
    offset = std::min(offset, doc.length());

    if (carets.removeCovering(offset))
    {
        history.breakStep();
        return true;
    }

    auto added = Cursor {};
    added.moveTo(offset);

    const auto grew = carets.add(added);

    history.breakStep();

    return grew;
}

bool Editor::addCursorAbove()
{
    const auto& topmost = carets[0];
    const auto row = rows.rowOfOffset(doc, topmost.head);

    if (row == 0)
        return false;

    // The held column if there is one, so a column of cursors dragged down
    // through a short line and back up again returns to where it started
    // rather than to the short line's end.
    const auto column = topmost.holdsColumn ? topmost.desiredColumn
                                            : rows.columnOfOffset(doc, topmost.head);

    auto added = Cursor {};
    added.moveTo(rows.offsetAtColumn(doc, row - 1, column));
    added.desiredColumn = column;
    added.holdsColumn = true;

    history.breakStep();

    return carets.add(added);
}

bool Editor::addCursorBelow()
{
    const auto& bottommost = carets[carets.count() - 1];
    const auto row = rows.rowOfOffset(doc, bottommost.head);

    if (row + 1 >= rows.rowCount(doc))
        return false;

    const auto column = bottommost.holdsColumn
                            ? bottommost.desiredColumn
                            : rows.columnOfOffset(doc, bottommost.head);

    auto added = Cursor {};
    added.moveTo(rows.offsetAtColumn(doc, row + 1, column));
    added.desiredColumn = column;
    added.holdsColumn = true;

    history.breakStep();

    return carets.add(added);
}

bool Editor::collapseCursors()
{
    if (!carets.hasMultiple())
        return false;

    carets.collapseToPrimary();
    history.breakStep();

    return true;
}

Cursor Editor::selectionOver(const SearchMatch& match)
{
    auto selection = Cursor {};
    selection.anchor = match.start;
    selection.head = match.end;

    return selection;
}

SearchQuery Editor::occurrenceQuery() const
{
    const auto& primary = carets.primary();

    if (!primary.hasSelection())
        return {};

    auto query = SearchQuery {};
    query.text = doc.text().substr(primary.start(), primary.length());

    // Case-sensitive, because ⌘D is how a rename is done and renaming `Value`
    // has no business also catching `value`.
    query.caseSensitive = true;

    // Whole-word only when the selection is exactly a word — which is what the
    // *first* ⌘D produces, since it expands the caret to one. Asked of the two
    // motions rather than of a flag set when that happened: a flag would have
    // to be cleared by every other thing that changes a selection, and the one
    // that got missed would silently change what the next ⌘D matched.
    query.wholeWord = Motion::wordRight(doc, primary.start()) == primary.end()
                      && Motion::wordLeft(doc, primary.end()) == primary.start();

    return query;
}

bool Editor::selectNextOccurrence()
{
    // Nothing selected yet: select the word under the primary, which is what
    // the next press then looks for. Deliberately a separate press rather than
    // expanding and jumping in one, so ⌘D on a word can also just mean "select
    // this word".
    if (!carets.primary().hasSelection())
    {
        const auto head = carets.primary().head;

        auto word = Cursor {};
        word.anchor = Motion::wordLeft(doc, Motion::wordRight(doc, head));
        word.head = Motion::wordRight(doc, head);

        if (!word.hasSelection())
            return false;

        carets.reset(word);
        history.breakStep();

        return true;
    }

    const auto query = occurrenceQuery();
    const auto matches = findMatches(doc, query);

    if (matches.empty())
        return false;

    // From past the last cursor, wrapping — so repeated presses walk down the
    // file and then round to the top, rather than stopping at the end.
    const auto from = carets[carets.count() - 1].end();

    auto next =
        std::find_if(matches.begin(),
                     matches.end(),
                     [&](const SearchMatch& match) { return match.start >= from; });

    if (next == matches.end())
        next = matches.begin();

    history.breakStep();

    return carets.add(selectionOver(*next));
}

bool Editor::selectAllOccurrences()
{
    // With nothing selected, the word under the caret is what to look for — and
    // unlike ⌘D this does not stop there. ⇧⌘L is one gesture and expanding to
    // the word is part of it rather than a press of its own.
    if (!carets.primary().hasSelection() && !selectNextOccurrence())
        return false;

    const auto query = occurrenceQuery();
    const auto matches = findMatches(doc, query);

    if (matches.empty())
        return false;

    // Where the person was, kept so the view does not jump to the last
    // occurrence in the file the moment every occurrence is selected.
    const auto wasAt = carets.primary().start();

    carets.reset(selectionOver(matches[0]));

    for (auto index = 1; index < matches.size(); ++index)
        carets.add(selectionOver(matches[index]));

    if (const auto index = carets.indexCovering(wasAt); index >= 0)
        carets.makePrimary(index);

    history.breakStep();

    return true;
}

void Editor::moveEach(const Destination& destination, bool extend)
{
    carets.transform(
        [&](Cursor& caret)
        {
            const auto offset = destination(caret);

            if (extend)
                caret.extendTo(offset);
            else
                caret.moveTo(offset);
        });

    // A caret move ends the undo step: typing, moving away, and typing again
    // are two separate things to undo.
    history.breakStep();
}

void Editor::moveLeft(bool extend)
{
    moveEach(
        [&](const Cursor& caret)
        {
            // Collapsing a selection leftwards goes to its start rather than
            // stepping back from the head, which is what every editor does.
            if (!extend && caret.hasSelection())
                return caret.start();

            return Motion::left(doc, caret.head);
        },
        extend);
}

void Editor::moveRight(bool extend)
{
    moveEach(
        [&](const Cursor& caret)
        {
            if (!extend && caret.hasSelection())
                return caret.end();

            return Motion::right(doc, caret.head);
        },
        extend);
}

void Editor::moveWordLeft(bool extend)
{
    moveEach([&](const Cursor& caret) { return Motion::wordLeft(doc, caret.head); },
             extend);
}

void Editor::moveWordRight(bool extend)
{
    moveEach([&](const Cursor& caret) { return Motion::wordRight(doc, caret.head); },
             extend);
}

void Editor::moveVertically(int rowsToMove, bool extend)
{
    carets.transform(
        [&](Cursor& caret)
        {
            // Vertical movement keeps the held column, so it does not go
            // through moveEach, which clears it.
            const auto offset = Motion::vertical(doc, rows, caret, rowsToMove);

            caret.head = offset;

            if (!extend)
                caret.anchor = offset;
        });

    history.breakStep();
}

void Editor::moveUp(bool extend, int lines)
{
    moveVertically(-lines, extend);
}

void Editor::moveDown(bool extend, int lines)
{
    moveVertically(lines, extend);
}

void Editor::moveToLineStart(bool extend)
{
    moveEach([&](const Cursor& caret)
             { return Motion::lineStart(doc, rows, caret.head); },
             extend);
}

void Editor::moveToLineEnd(bool extend)
{
    moveEach([&](const Cursor& caret)
             { return Motion::lineEnd(doc, rows, caret.head); },
             extend);
}

void Editor::moveToDocumentStart(bool extend)
{
    moveEach([&](const Cursor&) { return Motion::documentStart(doc); }, extend);
}

void Editor::moveToDocumentEnd(bool extend)
{
    moveEach([&](const Cursor&) { return Motion::documentEnd(doc); }, extend);
}

void Editor::placeCaret(std::size_t offset, bool extend)
{
    // A click puts the caret somewhere; it does not put N of them there. An
    // extension is the drag or the Shift+click that follows one, so it keeps
    // the set it grew from and moves only the primary's head.
    if (!extend)
        carets.reset({});

    offset = std::min(offset, doc.length());

    carets.transformPrimary(
        [&](Cursor& caret)
        {
            if (extend)
                caret.extendTo(offset);
            else
                caret.moveTo(offset);
        });

    history.breakStep();
}
} // namespace ecode
