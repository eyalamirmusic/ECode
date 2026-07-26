#include "Common.h"

#include <ECodeCore/Editor.h>

// Cursor movement once a logical line is several rows.
//
// The Editor owns the LineMap, so this is where the two meet: every one of
// these passes trivially with wrapping off — the map degenerates to the line
// index and the old behaviour is exactly what falls out — and every one of them
// is about the case where they differ.

using namespace nano;
using namespace ecode;

namespace
{
// Wrapped at five columns, so "aaaa bbbb cccc" is three rows of one line:
// "aaaa ", "bbbb ", "cccc".
Editor wrappedEditor(std::string text, std::size_t columns = 5)
{
    auto editor = Editor {Document::fromText(std::move(text))};
    editor.setWrapColumns(columns);

    return editor;
}
} // namespace

// The property vertical movement exists for, restated for wrapping: Down moves
// to what is directly below on screen. Moving by logical lines here would step
// over the whole paragraph on the first press.
auto tDownMovesByRow = test("WrapMotion/downMovesOneRowNotOneLine") = []
{
    auto editor = wrappedEditor("aaaa bbbb cccc");

    check(editor.lineMap().rowCount(editor.document()) == 3);

    editor.moveDown();
    check(editor.cursor().head == 5);

    editor.moveDown();
    check(editor.cursor().head == 10);

    // And stops at the last row rather than running off the document.
    editor.moveDown();
    check(editor.cursor().head == 10);
};

auto tUpMovesByRow = test("WrapMotion/upMovesOneRowNotOneLine") = []
{
    auto editor = wrappedEditor("aaaa bbbb cccc");

    editor.placeCaret(11);
    editor.moveUp();

    check(editor.cursor().head == 6);
};

// The held column has to survive rows as well as lines, and it is a *display*
// column now — the number that says where the caret looked like it was.
auto tHeldColumnCrossesRows = test("WrapMotion/theHeldColumnSurvivesAShortRow") = []
{
    auto editor = wrappedEditor("aaaa\nb\ncccc", 5);

    editor.placeCaret(3); // column 3 of the first line
    editor.moveDown(); // the short line clamps
    check(editor.cursor().head == 6);

    editor.moveDown(); // and column 3 comes back
    check(editor.document().columnAt(editor.cursor().head) == 3);
};

// A tab is one byte and four columns. Holding a byte column would put the caret
// somewhere it never looked like it was, which is what the old vertical motion
// did on every indented line.
auto tHeldColumnIsVisual = test("WrapMotion/theHeldColumnCountsTabsAsColumns") = []
{
    auto editor = Editor {Document::fromText("\tx\nabcdefgh")};

    editor.placeCaret(2); // after the tab and the x: display column 5
    editor.moveDown();

    check(editor.document().columnAt(editor.cursor().head) == 5);
};

// Home stops at the row's own left edge first, so a wrapped paragraph is
// navigable without counting rows back to the line's start.
auto tHomeStopsAtTheRow = test("WrapMotion/homeGoesToTheRowThenTheLine") = []
{
    auto editor = wrappedEditor("aaaa bbbb cccc");

    editor.placeCaret(7);

    editor.moveToLineStart();
    check(editor.cursor().head == 5);

    editor.moveToLineStart();
    check(editor.cursor().head == 0);
};

// End is the same toggle, with one wrinkle: a row's end is also the next row's
// start, so landing on it would draw the caret a row below and read as End
// having done nothing. It backs over the blank the wrap left behind.
auto tEndStopsAtTheRow = test("WrapMotion/endGoesToTheRowThenTheLine") = []
{
    auto editor = wrappedEditor("aaaa bbbb cccc");

    editor.placeCaret(6);

    editor.moveToLineEnd();
    check(editor.cursor().head == 9); // "bbbb", before the space at 9

    editor.moveToLineEnd();
    check(editor.cursor().head == 14);
};

// The same ambiguity, reached from the other side, and the arrangement it takes
// is narrow enough to be worth spelling out.
//
// A caret can only land on a full continuation row's far edge by *holding* a
// column across an intervening row — a caret already on such a row can never be
// at its end, because that offset belongs to the row below. So: start at the
// end of the last row, which is five columns wide, step up through a four-wide
// row that clamps without help, and only then arrive at a five-wide row that
// continues. Without the correction the second Up computes the offset at the
// end of row 0, which is the start of row 1 — and the caret does not appear to
// move at all.
//
// The first version of this test moved up once and passed with the correction
// deleted, which is the whole reason the arrangement is written out here.
auto tVerticalMovementStaysOnItsRow =
    test("WrapMotion/aHeldColumnDoesNotFallThroughToTheRowBelow") = []
{
    auto editor = wrappedEditor("aaaa bbbb\nccccc"); // rows "aaaa ", "bbbb", "ccccc"

    const auto& map = editor.lineMap();

    editor.placeCaret(editor.document().length());
    check(map.rowOfOffset(editor.document(), editor.cursor().head) == 2);

    editor.moveUp();
    check(map.rowOfOffset(editor.document(), editor.cursor().head) == 1);

    editor.moveUp();
    check(map.rowOfOffset(editor.document(), editor.cursor().head) == 0);
};

// --- the map staying in step ------------------------------------------------

// Editing goes through Editor, which is what keeps the map current without a
// subscriber anyone can forget to attach. A stale map draws the text as it was
// before the keystroke.
auto tEditingUpdatesTheMap = test("WrapMotion/typingRewrapsTheLineItLandsOn") = []
{
    auto editor = wrappedEditor("aaaa");

    check(editor.lineMap().rowCount(editor.document()) == 1);

    editor.placeCaret(4);
    editor.insert(" bbbb");

    check(editor.lineMap().rowCount(editor.document()) == 2);

    editor.insert("\ncc");
    check(editor.lineMap().rowCount(editor.document()) == 3);
};

auto tUndoUpdatesTheMap = test("WrapMotion/undoRewrapsToo") = []
{
    auto editor = wrappedEditor("aaaa");

    editor.placeCaret(4);
    editor.insert(" bbbb cccc");

    check(editor.lineMap().rowCount(editor.document()) == 3);

    editor.undo();

    check(editor.document().text() == "aaaa");
    check(editor.lineMap().rowCount(editor.document()) == 1);

    editor.redo();
    check(editor.lineMap().rowCount(editor.document()) == 3);
};

// Replacing the document wholesale — a reload from disk — has no edit to repair
// against, so it rebuilds. The wrap width survives it: it belongs to the view,
// not to the text.
auto tReplacingTheDocumentKeepsTheWidth =
    test("WrapMotion/replacingTheDocumentKeepsTheWrapWidth") = []
{
    auto editor = wrappedEditor("aaaa");

    editor.setDocument(Document::fromText("bbbb cccc dddd"));

    check(editor.wrapColumns() == 5);
    check(editor.lineMap().rowCount(editor.document()) == 3);
};

// Turning wrapping on must not move the caret. It changes where the text is
// drawn, not what is being edited.
auto tTogglingWrapKeepsTheCaret = test("WrapMotion/togglingWrapLeavesTheCaret") = []
{
    auto editor = Editor {Document::fromText("aaaa bbbb cccc")};

    editor.placeCaret(12);
    editor.setWrapColumns(5);

    check(editor.cursor().head == 12);

    editor.setWrapColumns(0);
    check(editor.cursor().head == 12);
};
