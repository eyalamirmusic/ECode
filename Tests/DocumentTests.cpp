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

// The text of that line, not merely a line of that length: the renderer measures
// it to size a horizontal scroll, and a tab is one byte and several columns, so
// handing back the wrong line of the right length would be an answer that looks
// correct and reaches the wrong distance.
//
// The document is built so that no other line could stand in — "abcdef" is the
// only one of its width, and the two either side of it differ from it in their
// characters as well as in their length.
auto tWidestLineTextIsThatLine =
    test("Document/theWidestLineIsHandedBackAsText") = []
{
    const auto document = Document::fromText("ab\nabcdef\nabc");

    check(document.widestLineText() == "abcdef");
    check(document.widestLineText().size() == document.widestLine());
};

// It follows the record rather than being computed once, including across the
// rescan a shortened record forces.
auto tWidestLineTextFollowsEdits =
    test("Document/theWidestLineTextFollowsTheRecord") = []
{
    auto document = Document::fromText("ab\nabcdef\nabcd");

    check(document.widestLineText() == "abcdef");

    // A new record on another line, carried without a rescan.
    document.replace(0, 2, "zzzzzzzz");
    check(document.widestLineText() == "zzzzzzzz");

    // And the record's own line cut down, which is the case that rescans.
    document.replace(0, 8, "z");
    check(document.widestLineText() == "abcdef");
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

// --- the revision ----------------------------------------------------------
//
// What anything caching work derived from the text compares against. Two
// properties, and the second is the one that is easy to miss.

auto tRevisionFollowsTheText = test("Document/theRevisionFollowsTheText") = []
{
    auto document = Document::fromText("alpha\nbeta\n");

    const auto before = document.revision();

    // Reading does not count as changing.
    (void) document.line(0);
    (void) document.lineCount();

    check(document.revision() == before);

    document.replace(0, 0, "x");

    const auto afterInsert = document.revision();

    check(afterInsert != before);

    // Undoing through apply() is a change like any other: the text is back
    // where it started but nothing derived from the intermediate state is.
    document.apply(TextEdit {0, "x", ""});

    check(document.revision() != afterInsert);
};

// The property that matters to a cache and that a per-document counter would
// get wrong: opening a file must not look like the file it replaced. Both would
// otherwise be at revision one with entirely different text.
auto tRevisionsAreUnique = test("Document/revisionsAreUniqueAcrossDocuments") = []
{
    const auto first = Document::fromText("alpha\n");
    const auto second = Document::fromText("alpha\n");

    check(first.text() == second.text());
    check(first.revision() != second.revision());
};

// A document nobody has put text into still has to answer, because an untitled
// buffer is one — and every one of these was reached, in the app, by drawing the
// status bar over a brand new tab. columnAt() indexed an empty vector and the
// window went down.
//
// The invariant was always documented ("a genuinely empty document still has a
// single line to put the caret on"); it was the *default* constructor that did
// not establish it, and nothing asked until untitled buffers were reachable.
auto tADefaultDocumentIsIndexed =
    test("Document/aDefaultConstructedDocumentIsIndexed") = []
{
    const auto doc = Document {};

    check(doc.lineCount() == 1);
    check(doc.length() == 0);
    check(doc.lineAt(0) == 0);
    check(doc.columnAt(0) == 0);
    check(doc.line(0).empty());

    // And an offset past the end clamps rather than reading off the index.
    check(doc.lineAt(50) == 0);
    check(doc.columnAt(50) == 50);
};

// The same document as fromText(""), because there is only one empty document.
auto tDefaultMatchesEmptyText =
    test("Document/aDefaultDocumentMatchesFromTextOfNothing") = []
{
    const auto byDefault = Document {};
    const auto fromNothing = Document::fromText("");

    check(byDefault.lineCount() == fromNothing.lineCount());
    check(byDefault.text() == fromNothing.text());
    check(byDefault.widestLine() == fromNothing.widestLine());
};
