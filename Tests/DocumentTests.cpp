#include "Common.h"

// Document's line indexing. Pure logic, no GPU and no files — the line index is
// what every visible-slice calculation in the renderer is derived from, so an
// off-by-one here shows up as text drawn at the wrong y or a line silently
// missing from the bottom of the viewport.

using namespace nano;
using namespace ecode;

auto tEmptyDocumentHasOneLine = test("Document/emptyDocumentStillHasOneLine") = []
{
    const auto document = Document::fromText("");

    // Somewhere has to exist for a caret to sit, even with no content.
    check(document.lineCount() == 1);
    check(document.line(0).empty());
    check(document.isEmpty());
};

auto tSingleLine = test("Document/singleLineWithoutTerminator") = []
{
    const auto document = Document::fromText("hello");

    check(document.lineCount() == 1);
    check(document.line(0) == "hello");
};

auto tSplitsOnNewlines = test("Document/splitsOnNewlines") = []
{
    const auto document = Document::fromText("one\ntwo\nthree");

    check(document.lineCount() == 3);
    check(document.line(0) == "one");
    check(document.line(1) == "two");
    check(document.line(2) == "three");
};

// The case that decides whether a file ends with a phantom blank line. A
// trailing newline terminates the last line rather than starting another.
auto tTrailingNewlineDoesNotAddALine =
    test("Document/trailingNewlineTerminatesLastLine") = []
{
    const auto document = Document::fromText("one\ntwo\n");

    check(document.lineCount() == 2);
    check(document.line(1) == "two");
};

// A blank line in the middle is real content and must be kept.
auto tKeepsInteriorBlankLines = test("Document/keepsInteriorBlankLines") = []
{
    const auto document = Document::fromText("one\n\nthree");

    check(document.lineCount() == 3);
    check(document.line(0) == "one");
    check(document.line(1).empty());
    check(document.line(2) == "three");
};

auto tConsecutiveNewlines = test("Document/handlesRunsOfNewlines") = []
{
    const auto document = Document::fromText("\n\n\n");

    check(document.lineCount() == 3);

    for (std::size_t line = 0; line < document.lineCount(); ++line)
        check(document.line(line).empty());
};

// Line terminators never reach the renderer: a stray \r would rasterize as a
// visible box on a face that has a glyph for it.
auto tStripsCarriageReturns = test("Document/stripsCrlfTerminators") = []
{
    const auto document = Document::fromText("one\r\ntwo\r\n");

    check(document.lineCount() == 2);
    check(document.line(0) == "one");
    check(document.line(1) == "two");
};

// Out of range returns empty rather than reading past the end — the renderer
// can ask for a line beyond the document while a viewport calculation settles.
auto tOutOfRangeIsEmpty = test("Document/outOfRangeLineIsEmpty") = []
{
    const auto document = Document::fromText("one\ntwo");

    check(document.line(2).empty());
    check(document.line(99).empty());
};

auto tTracksWidestLine = test("Document/tracksTheWidestLine") = []
{
    const auto document = Document::fromText("ab\nabcdef\nabc");

    check(document.widestLine() == 6);
};

auto tTextIsPreserved = test("Document/keepsTheOriginalText") = []
{
    const auto source = std::string {"alpha\nbeta\n"};
    const auto document = Document::fromText(source);

    check(document.text() == source);
    check(!document.isEmpty());
};

// A file with no trailing newline must still count its final line's width,
// which is a separate code path from the newline-terminated case.
auto tWidestHandlesFinalLine =
    test("Document/widestCountsAnUnterminatedFinalLine") = []
{
    const auto document = Document::fromText("ab\nabcdefgh");

    check(document.widestLine() == 8);
};
