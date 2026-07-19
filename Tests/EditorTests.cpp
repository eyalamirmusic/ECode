#include "Common.h"

#include <ECodeCore/Editor.h>

// The editing model: cursor, selection, and the operations a keystroke maps to.
//
// Written as "drive it and read the text back", because that is the only way to
// catch the interactions. Most editing bugs are not in one operation but in a
// pair of them — typing over a selection, arrow-keying out of one, undoing
// after a caret move.

using namespace nano;
using namespace ecode;

namespace
{
Editor editorWith(std::string text)
{
    return Editor {Document::fromText(std::move(text))};
}
} // namespace

// --- typing -----------------------------------------------------------------

auto tInsertsAtCaret = test("Editor/insertsAtTheCaret") = []
{
    auto editor = editorWith("hello");

    editor.moveToDocumentEnd();
    editor.insert(" world");

    check(editor.document().text() == "hello world");
    check(editor.cursor().head == 11);
    check(!editor.cursor().hasSelection());
};

// Typing over a selection replaces it. The case most easily forgotten, because
// it works fine until someone selects something.
auto tTypingReplacesSelection = test("Editor/typingReplacesTheSelection") = []
{
    auto editor = editorWith("hello world");

    editor.moveToDocumentStart();
    editor.moveWordRight(true); // select "hello"

    editor.insert("goodbye");

    check(editor.document().text() == "goodbye world");
    check(!editor.cursor().hasSelection());
};

auto tBackspaceDeletesBehind = test("Editor/backspaceDeletesBehindTheCaret") = []
{
    auto editor = editorWith("abc");

    editor.moveToDocumentEnd();
    editor.backspace();

    check(editor.document().text() == "ab");
    check(editor.cursor().head == 2);
};

auto tBackspaceAtStartIsSafe = test("Editor/backspaceAtTheStartDoesNothing") = []
{
    auto editor = editorWith("abc");

    editor.backspace();

    check(editor.document().text() == "abc");
    check(editor.cursor().head == 0);
};

auto tBackspaceDeletesSelection = test("Editor/backspaceDeletesTheSelection") = []
{
    auto editor = editorWith("hello world");

    editor.moveToDocumentStart();
    editor.moveWordRight(true);
    editor.backspace();

    check(editor.document().text() == " world");
};

auto tDeleteForward = test("Editor/deleteForwardRemovesAhead") = []
{
    auto editor = editorWith("abc");

    editor.deleteForward();

    check(editor.document().text() == "bc");
    check(editor.cursor().head == 0);
};

// A multi-byte character must delete whole. Deleting one byte would leave a
// broken UTF-8 tail that the renderer then tries to decode.
auto tBackspaceRemovesWholeCodepoint =
    test("Editor/backspaceRemovesAWholeCodepoint") = []
{
    auto editor = editorWith("aé");

    editor.moveToDocumentEnd();
    editor.backspace();

    check(editor.document().text() == "a");
};

auto tDeleteWordBefore = test("Editor/deleteWordBeforeRemovesAWord") = []
{
    auto editor = editorWith("one two three");

    editor.moveToDocumentEnd();
    editor.deleteWordBefore();

    check(editor.document().text() == "one two ");
};

// --- movement ---------------------------------------------------------------

auto tArrowsMoveByCodepoint = test("Editor/arrowsMoveByCodepoint") = []
{
    auto editor = editorWith("aé b");

    editor.moveRight();
    check(editor.cursor().head == 1);

    editor.moveRight(); // over the two-byte é
    check(editor.cursor().head == 3);

    editor.moveLeft();
    check(editor.cursor().head == 1);
};

// Collapsing a selection with an arrow goes to its edge, rather than stepping
// one character from the head. Getting this wrong loses a character of context
// every time someone dismisses a selection.
auto tArrowCollapsesToSelectionEdge =
    test("Editor/arrowsCollapseToTheSelectionEdge") = []
{
    auto editor = editorWith("hello world");

    editor.moveToDocumentStart();
    editor.moveWordRight(true); // "hello" selected, head at 5

    editor.moveLeft();
    check(editor.cursor().head == 0);
    check(!editor.cursor().hasSelection());

    editor.moveWordRight(true);
    editor.moveRight();
    check(editor.cursor().head == 5);
};

auto tWordMovement = test("Editor/wordMovementSkipsSeparators") = []
{
    auto editor = editorWith("one  two,three");

    editor.moveWordRight();
    check(editor.cursor().head == 3); // end of "one"

    editor.moveWordRight();
    check(editor.cursor().head == 8); // end of "two"

    editor.moveWordLeft();
    check(editor.cursor().head == 5); // start of "two"
};

// Home toggles between the first non-blank and the true line start, which is
// what makes it useful on indented code.
auto tHomeTogglesWithIndentation =
    test("Editor/lineStartTogglesWithIndentation") = []
{
    auto editor = editorWith("    indented");

    editor.moveToDocumentEnd();

    editor.moveToLineStart();
    check(editor.cursor().head == 4); // first non-blank

    editor.moveToLineStart();
    check(editor.cursor().head == 0); // then the true start
};

auto tLineEnd = test("Editor/lineEndGoesToTheEndOfTheLine") = []
{
    auto editor = editorWith("one\ntwo");

    editor.moveToLineEnd();
    check(editor.cursor().head == 3);

    editor.moveDown();
    editor.moveToLineEnd();
    check(editor.cursor().head == 7);
};

// The property vertical movement exists for: passing through a short line must
// not narrow the column it returns to.
auto tVerticalMovementHoldsColumn =
    test("Editor/verticalMovementRemembersTheColumn") = []
{
    auto editor = editorWith("aaaaaaaaaa\nbb\ncccccccccc");

    editor.placeCaret(8); // column 8 of the first line
    check(editor.document().columnAt(editor.cursor().head) == 8);

    editor.moveDown(); // short line clamps to its end
    check(editor.document().columnAt(editor.cursor().head) == 2);

    editor.moveDown(); // and the original column comes back
    check(editor.document().columnAt(editor.cursor().head) == 8);
};

// Any horizontal move abandons the held column, so a later vertical move starts
// from where the caret actually is.
auto tHorizontalMovementClearsColumn =
    test("Editor/horizontalMovementForgetsTheColumn") = []
{
    auto editor = editorWith("aaaaaaaaaa\nbb\ncccccccccc");

    editor.placeCaret(8);
    editor.moveDown(); // on the short line, holding column 8
    editor.moveToLineStart(); // clears the hold
    editor.moveDown();

    check(editor.document().columnAt(editor.cursor().head) == 0);
};

auto tVerticalMovementClampsAtEnds =
    test("Editor/verticalMovementStopsAtTheEnds") = []
{
    auto editor = editorWith("one\ntwo");

    editor.moveUp();
    check(editor.document().lineAt(editor.cursor().head) == 0);

    editor.moveDown(false, 50);
    check(editor.document().lineAt(editor.cursor().head) == 1);
};

// --- selection --------------------------------------------------------------

auto tShiftExtendsSelection = test("Editor/extendGrowsTheSelection") = []
{
    auto editor = editorWith("hello world");

    editor.moveRight(true);
    editor.moveRight(true);

    check(editor.cursor().hasSelection());
    check(editor.selectedText() == "he");
};

auto tSelectAll = test("Editor/selectAllCoversTheDocument") = []
{
    auto editor = editorWith("one\ntwo");

    editor.selectAll();

    check(editor.selectedText() == "one\ntwo");
};

// A click anywhere inside a word selects the whole word, not just the part
// after the click.
auto tSelectWordFromTheMiddle = test("Editor/doubleClickSelectsTheWholeWord") = []
{
    auto editor = editorWith("alpha beta gamma");

    editor.selectWordAt(8); // inside "beta"

    check(editor.selectedText() == "beta");
};

auto tSelectLine = test("Editor/tripleClickSelectsTheLine") = []
{
    auto editor = editorWith("one\ntwo\nthree");

    editor.selectLineAt(5);

    check(editor.selectedText() == "two");
};

// --- undo -------------------------------------------------------------------

auto tUndoRestoresTypedText = test("Editor/undoRemovesTypedText") = []
{
    auto editor = editorWith("");

    editor.insert("h");
    editor.insert("i");

    check(editor.document().text() == "hi");

    editor.undo();
    check(editor.document().text().empty());

    editor.redo();
    check(editor.document().text() == "hi");
};

// Moving the caret splits the undo step, so typing in two places undoes as two
// separate actions.
auto tCaretMoveSplitsUndo = test("Editor/movingTheCaretSplitsUndo") = []
{
    auto editor = editorWith("....");

    editor.moveToDocumentStart();
    editor.insert("a");

    editor.moveToDocumentEnd();
    editor.insert("b");

    editor.undo();
    check(editor.document().text() == "a....");

    editor.undo();
    check(editor.document().text() == "....");
};

auto tUndoOfSelectionReplacement = test("Editor/undoRestoresAReplacedSelection") = []
{
    auto editor = editorWith("hello world");

    editor.moveToDocumentStart();
    editor.moveWordRight(true);
    editor.insert("goodbye");

    check(editor.document().text() == "goodbye world");

    editor.undo();
    check(editor.document().text() == "hello world");
};

auto tUndoWithNothingToDo = test("Editor/undoOnAFreshEditorIsSafe") = []
{
    auto editor = editorWith("abc");

    check(!editor.canUndo());
    editor.undo();

    check(editor.document().text() == "abc");
};

// --- consistency ------------------------------------------------------------

// The caret must never end up outside the document or mid-character, whatever
// sequence of operations ran.
auto tCaretStaysInBounds = test("Editor/caretStaysWithinTheDocument") = []
{
    auto editor = editorWith("one\ntwo\nthree");

    editor.moveToDocumentEnd();

    for (auto i = 0; i < 30; ++i)
    {
        editor.moveRight();
        editor.moveDown();
        editor.moveWordRight();
    }

    check(editor.cursor().head <= editor.document().length());

    for (auto i = 0; i < 30; ++i)
    {
        editor.moveLeft();
        editor.moveUp();
        editor.moveWordLeft();
    }

    check(editor.cursor().head == 0);
};

// Deleting everything then typing must leave a coherent document, rather than
// a stale line index or a caret pointing past the end.
auto tEmptyingAndRefilling = test("Editor/emptyingThenTypingStaysConsistent") = []
{
    auto editor = editorWith("one\ntwo\nthree");

    editor.selectAll();
    editor.backspace();

    check(editor.document().text().empty());
    check(editor.document().lineCount() == 1);
    check(editor.cursor().head == 0);

    editor.insert("fresh");

    check(editor.document().text() == "fresh");
    check(editor.document().lineCount() == 1);
};

auto tVersionTracksChanges = test("Editor/versionAdvancesOnEveryChange") = []
{
    auto editor = editorWith("abc");

    const auto before = editor.version();

    editor.moveRight(); // movement is not a change
    check(editor.version() == before);

    editor.insert("d");
    check(editor.version() > before);
};

// Typing a newline has to update the line index, since everything the renderer
// and highlighter do is derived from it.
auto tNewlinesUpdateTheLineIndex = test("Editor/typingANewlineAddsALine") = []
{
    auto editor = editorWith("oneTwo");

    editor.placeCaret(3);
    editor.insert("\n");

    check(editor.document().lineCount() == 2);
    check(editor.document().line(0) == "one");
    check(editor.document().line(1) == "Two");
    check(editor.document().lineAt(editor.cursor().head) == 1);
};
