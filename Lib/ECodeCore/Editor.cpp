#include "Editor.h"

#include <algorithm>

namespace ecode
{
void Editor::setDocument(Document documentToUse)
{
    doc = std::move(documentToUse);
    caret = {};
    history.clear();
    ++revision;

    onDocumentReplaced();
}

std::size_t
    Editor::applyEdit(std::size_t start, std::size_t end, std::string_view text)
{
    const auto edit = doc.replace(start, end, text);

    history.record(edit);
    ++revision;

    onEdit(edit);

    return edit.insertedEnd();
}

void Editor::insert(std::string_view text)
{
    // Typing over a selection replaces it. Handled here rather than at each
    // call site, because it is the case that gets forgotten.
    const auto at = applyEdit(caret.start(), caret.end(), text);

    caret.moveTo(at);
}

void Editor::backspace()
{
    if (caret.hasSelection())
    {
        caret.moveTo(applyEdit(caret.start(), caret.end(), ""));
        return;
    }

    if (caret.head == 0)
        return;

    // A whole codepoint, so backspacing a multi-byte character removes it
    // rather than leaving a broken tail behind.
    const auto from = Motion::left(doc, caret.head);

    caret.moveTo(applyEdit(from, caret.head, ""));
}

void Editor::deleteForward()
{
    if (caret.hasSelection())
    {
        caret.moveTo(applyEdit(caret.start(), caret.end(), ""));
        return;
    }

    if (caret.head >= doc.length())
        return;

    const auto to = Motion::right(doc, caret.head);

    caret.moveTo(applyEdit(caret.head, to, ""));
}

void Editor::deleteWordBefore()
{
    if (caret.hasSelection())
    {
        caret.moveTo(applyEdit(caret.start(), caret.end(), ""));
        return;
    }

    const auto from = Motion::wordLeft(doc, caret.head);

    if (from == caret.head)
        return;

    caret.moveTo(applyEdit(from, caret.head, ""));
}

void Editor::deleteWordAfter()
{
    if (caret.hasSelection())
    {
        caret.moveTo(applyEdit(caret.start(), caret.end(), ""));
        return;
    }

    const auto to = Motion::wordRight(doc, caret.head);

    if (to == caret.head)
        return;

    caret.moveTo(applyEdit(caret.head, to, ""));
}

void Editor::undo()
{
    const auto edits = history.undo();

    if (edits.empty())
        return;

    for (const auto& edit: edits)
    {
        doc.apply(edit);
        onEdit(edit);
    }

    // Land where the change was, so undoing scrolls back to what it changed
    // rather than leaving the caret somewhere unrelated.
    caret.moveTo(std::min(edits.back().insertedEnd(), doc.length()));
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
        onEdit(edit);
    }

    caret.moveTo(std::min(edits.back().insertedEnd(), doc.length()));
    ++revision;
}

std::string Editor::selectedText() const
{
    if (!caret.hasSelection())
        return {};

    return doc.text().substr(caret.start(), caret.length());
}

void Editor::selectAll()
{
    caret.anchor = 0;
    caret.head = doc.length();
    caret.holdsColumn = false;
}

void Editor::selectWordAt(std::size_t offset)
{
    offset = std::min(offset, doc.length());

    // Right then left, so a click anywhere inside a word selects the whole of
    // it rather than only the half after the click.
    caret.anchor = Motion::wordLeft(doc, Motion::wordRight(doc, offset));
    caret.head = Motion::wordRight(doc, offset);
    caret.holdsColumn = false;
}

void Editor::selectLineAt(std::size_t offset)
{
    const auto line = doc.lineAt(std::min(offset, doc.length()));

    caret.anchor = doc.offsetAt(line, 0);
    caret.head = doc.offsetAt(line, doc.line(line).size());
    caret.holdsColumn = false;
}

void Editor::applyMotion(std::size_t offset, bool extend)
{
    if (extend)
        caret.extendTo(offset);
    else
        caret.moveTo(offset);

    // A caret move ends the undo step: typing, moving away, and typing again
    // are two separate things to undo.
    history.breakStep();
}

void Editor::moveLeft(bool extend)
{
    // Collapsing a selection leftwards goes to its start rather than stepping
    // back from the head, which is what every editor does.
    if (!extend && caret.hasSelection())
    {
        applyMotion(caret.start(), false);
        return;
    }

    applyMotion(Motion::left(doc, caret.head), extend);
}

void Editor::moveRight(bool extend)
{
    if (!extend && caret.hasSelection())
    {
        applyMotion(caret.end(), false);
        return;
    }

    applyMotion(Motion::right(doc, caret.head), extend);
}

void Editor::moveWordLeft(bool extend)
{
    applyMotion(Motion::wordLeft(doc, caret.head), extend);
}

void Editor::moveWordRight(bool extend)
{
    applyMotion(Motion::wordRight(doc, caret.head), extend);
}

void Editor::moveUp(bool extend, int lines)
{
    // Vertical movement keeps the held column, so this does not go through
    // applyMotion, which clears it.
    const auto offset = Motion::vertical(doc, caret, -lines);

    if (extend)
        caret.head = offset;
    else
    {
        caret.head = offset;
        caret.anchor = offset;
    }

    history.breakStep();
}

void Editor::moveDown(bool extend, int lines)
{
    const auto offset = Motion::vertical(doc, caret, lines);

    caret.head = offset;

    if (!extend)
        caret.anchor = offset;

    history.breakStep();
}

void Editor::moveToLineStart(bool extend)
{
    applyMotion(Motion::lineStart(doc, caret.head), extend);
}

void Editor::moveToLineEnd(bool extend)
{
    applyMotion(Motion::lineEnd(doc, caret.head), extend);
}

void Editor::moveToDocumentStart(bool extend)
{
    applyMotion(Motion::documentStart(doc), extend);
}

void Editor::moveToDocumentEnd(bool extend)
{
    applyMotion(Motion::documentEnd(doc), extend);
}

void Editor::placeCaret(std::size_t offset, bool extend)
{
    applyMotion(std::min(offset, doc.length()), extend);
}
} // namespace ecode
