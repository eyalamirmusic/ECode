#include "Common.h"

// Document mutation and position mapping.
//
// The line index has to survive every edit, because everything the renderer and
// the highlighter do is derived from it. An edit that leaves the index stale
// shows up much later as text drawn at the wrong y, or a highlight span landing
// on the wrong row, with nothing pointing back here.

using namespace nano;
using namespace ecode;

auto tInsertsText = test("DocumentEdit/insertsText") = []
{
    auto document = Document::fromText("hello world");

    document.replace(5, 5, ",");

    check(document.text() == "hello, world");
    check(document.lineCount() == 1);
};

auto tDeletesText = test("DocumentEdit/deletesText") = []
{
    auto document = Document::fromText("hello, world");

    document.replace(5, 6, "");

    check(document.text() == "hello world");
};

auto tReplacesText = test("DocumentEdit/replacesARange") = []
{
    auto document = Document::fromText("hello world");

    document.replace(6, 11, "there");

    check(document.text() == "hello there");
};

// The returned edit carries what was actually removed, which is what makes it
// invertible. Without this, undo would have nothing to restore.
auto tReturnsAnInvertibleEdit = test("DocumentEdit/returnsWhatItRemoved") = []
{
    auto document = Document::fromText("hello world");

    const auto edit = document.replace(6, 11, "there");

    check(edit.start == 6);
    check(edit.removed == "world");
    check(edit.inserted == "there");

    document.apply(edit.inverted());
    check(document.text() == "hello world");
};

// Adding a line must be visible to lineCount immediately, not on the next
// reload.
auto tReindexesOnInsertingNewlines =
    test("DocumentEdit/reindexesWhenLinesAreAdded") = []
{
    auto document = Document::fromText("one\ntwo");

    check(document.lineCount() == 2);

    document.replace(3, 3, "\nmiddle");

    check(document.lineCount() == 3);
    check(document.line(0) == "one");
    check(document.line(1) == "middle");
    check(document.line(2) == "two");
};

auto tReindexesOnRemovingNewlines =
    test("DocumentEdit/reindexesWhenLinesAreRemoved") = []
{
    auto document = Document::fromText("one\ntwo\nthree");

    document.replace(3, 8, ""); // joins line 0 and line 2

    check(document.lineCount() == 1);
    check(document.line(0) == "onethree");
};

// A stale cursor after a reload should land somewhere sensible rather than
// corrupt the buffer, so out-of-range offsets clamp.
auto tClampsOutOfRangeEdits = test("DocumentEdit/clampsOutOfRangeOffsets") = []
{
    auto document = Document::fromText("abc");

    document.replace(100, 200, "!");
    check(document.text() == "abc!");

    document.replace(2, 1, "?"); // end before start
    check(document.text() == "ab?c!");
};

auto tEditingEmptyDocument = test("DocumentEdit/editsAnEmptyDocument") = []
{
    auto document = Document::fromText("");

    document.replace(0, 0, "first");

    check(document.text() == "first");
    check(document.lineCount() == 1);
    check(document.line(0) == "first");
};

auto tOffsetAtMapsPositions = test("DocumentEdit/offsetAtMapsLineAndColumn") = []
{
    const auto document = Document::fromText("one\ntwo\nthree");

    check(document.offsetAt(0, 0) == 0);
    check(document.offsetAt(0, 3) == 3);
    check(document.offsetAt(1, 0) == 4);
    check(document.offsetAt(2, 0) == 8);
    check(document.offsetAt(2, 5) == 13);
};

// A column carried over from a longer line — what happens when the caret moves
// vertically — must not walk into the following line.
auto tOffsetClampsColumnToLine =
    test("DocumentEdit/offsetClampsColumnToItsLine") = []
{
    const auto document = Document::fromText("longer line\nab\nnext");

    check(document.offsetAt(1, 99) == document.offsetAt(1, 2));
    check(document.line(document.lineAt(document.offsetAt(1, 99))) == "ab");
};

auto tLineAtMapsBack = test("DocumentEdit/lineAtMapsOffsetsBackToLines") = []
{
    const auto document = Document::fromText("one\ntwo\nthree");

    check(document.lineAt(0) == 0);
    check(document.lineAt(3) == 0);
    check(document.lineAt(4) == 1);
    check(document.lineAt(7) == 1);
    check(document.lineAt(8) == 2);
    check(document.lineAt(12) == 2);
};

auto tColumnAtMapsBack = test("DocumentEdit/columnAtMapsOffsetsBackToColumns") = []
{
    const auto document = Document::fromText("one\ntwo");

    check(document.columnAt(0) == 0);
    check(document.columnAt(2) == 2);
    check(document.columnAt(4) == 0);
    check(document.columnAt(6) == 2);
};

// The round trip is what cursor movement relies on: converting a position to an
// offset and back must land where it started.
auto tPositionRoundTrips = test("DocumentEdit/positionsRoundTrip") = []
{
    const auto document = Document::fromText("alpha\nbeta\n\ngamma");

    for (std::size_t line = 0; line < document.lineCount(); ++line)
    {
        for (std::size_t column = 0; column <= document.line(line).size(); ++column)
        {
            const auto offset = document.offsetAt(line, column);

            check(document.lineAt(offset) == line);
            check(document.columnAt(offset) == column);
        }
    }
};

// Undo through the document itself: apply, invert, and the text and the line
// index both come back.
auto tApplyRestoresIndexToo = test("DocumentEdit/undoRestoresTheLineIndex") = []
{
    auto document = Document::fromText("one\ntwo");

    const auto edit = document.replace(3, 3, "\nextra");
    check(document.lineCount() == 3);

    document.apply(edit.inverted());

    check(document.text() == "one\ntwo");
    check(document.lineCount() == 2);
    check(document.line(1) == "two");
};

auto tLengthTracksEdits = test("DocumentEdit/lengthTracksEdits") = []
{
    auto document = Document::fromText("abc");

    check(document.length() == 3);

    document.replace(3, 3, "def");
    check(document.length() == 6);

    document.replace(0, 3, "");
    check(document.length() == 3);
};
