#include "Common.h"

#include <ECodeCore/LineMap.h>

#include <string>
#include <vector>

// The mapping from logical lines to the rows they occupy on screen.
//
// This is the class PLAN.md §7.3 calls the structural debt: every layer above
// it used to assume row *n* was line *n* and sat at `n * lineHeight`. Soft wrap
// is the first thing to break that assumption, and folding, inline diagnostics
// and image lines all break it the same way — so what these tests are really
// pinning is that the assumption lives in exactly one place.

using namespace nano;
using namespace ecode;

namespace
{
LineMap wrappedAt(const Document& document, std::size_t columns)
{
    auto map = LineMap {};
    map.setWrapColumns(document, columns);

    return map;
}

// Every row's text, in order — the thing that would actually be drawn.
std::vector<std::string> rowTexts(const Document& document, const LineMap& map)
{
    auto rows = std::vector<std::string> {};

    for (std::size_t index = 0; index < map.rowCount(document); ++index)
        rows.emplace_back(map.row(document, index).textIn(document));

    return rows;
}
} // namespace

// --- wrapping off -----------------------------------------------------------

// The default, and the one that has to cost nothing: a map that stores nothing
// per line and answers every question from the document's own line index.
auto tUnwrappedRowsAreLines = test("LineMap/withoutWrappingARowIsALine") = []
{
    const auto document = Document::fromText("one\ntwo\nthree");
    const auto map = LineMap {};

    check(!map.wraps());
    check(map.rowCount(document) == 3);
    check(map.lineOfRow(document, 2) == 2);
    check(map.firstRowOfLine(1) == 1);
    check(map.rowsInLine(1) == 1);

    const auto row = map.row(document, 1);

    check(row.line == 1);
    check(row.start == 0);
    check(row.end == 3);
    check(!row.isContinuation());
};

// --- wrapping on ------------------------------------------------------------

// The width matters here, and picking it badly is how the first version of this
// test passed with word breaking deleted outright: at ten columns the greedy
// character break lands on the space anyway, so wrapping by characters and
// wrapping by words give the same answer. Thirteen puts the character break
// three letters into "brown", where the two disagree.
auto tWrapsAtWordBoundaries = test("LineMap/breaksBetweenWordsNotInsideThem") = []
{
    const auto document = Document::fromText("the quick brown fox");
    const auto map = wrappedAt(document, 13);

    check(rowTexts(document, map)
          == std::vector<std::string> {"the quick ", "brown fox"});
};

// Trailing blanks stay on the row they were typed on, so a wrapped sentence
// never starts with a space hanging off the left margin. Seven columns for the
// same reason as above — six would break on the boundary by accident.
auto tBlanksStayWithTheRowBeforeTheBreak =
    test("LineMap/blanksBeforeABreakStayAbove") = []
{
    const auto document = Document::fromText("aaa   bbb");
    const auto map = wrappedAt(document, 7);

    check(rowTexts(document, map) == std::vector<std::string> {"aaa   ", "bbb"});
};

// A word with nowhere to break has to break inside itself. The alternative is a
// URL running off the right edge of a view whose entire purpose is that nothing
// does.
auto tLongWordBreaksInsideItself = test("LineMap/aWordTooWideBreaksMidWord") = []
{
    const auto document = Document::fromText("abcdefghij");
    const auto map = wrappedAt(document, 4);

    check(rowTexts(document, map)
          == std::vector<std::string> {"abcd", "efgh", "ij"});
};

// The failure this rules out is silent and permanent: half a UTF-8 sequence on
// each of two rows is not merely drawn wrong, it stops the text matching
// anything.
auto tNeverSplitsACharacter = test("LineMap/neverBreaksInsideACodepoint") = []
{
    const auto document = Document::fromText("ééééé");
    const auto map = wrappedAt(document, 2);

    check(rowTexts(document, map) == std::vector<std::string> {"éé", "éé", "é"});
};

// A tab is one byte and up to four columns, so a map that wrapped on bytes
// would run indented code well past the right edge before breaking it.
auto tTabsCountAsColumns = test("LineMap/tabsAreMeasuredInColumns") = []
{
    const auto document = Document::fromText("\t\tab");
    const auto map = wrappedAt(document, 8);

    // Two tabs fill the eight columns on their own.
    check(rowTexts(document, map) == std::vector<std::string> {"\t\t", "ab"});
};

// A continuation row starts at the left margin, so its tab stops start there
// too. Measuring them from the logical line's start would indent it by whatever
// happened to precede the break.
auto tContinuationRowsRestartTabStops =
    test("LineMap/aContinuationRowStartsItsOwnTabStops") = []
{
    const auto document = Document::fromText("abcde\tx");
    const auto map = wrappedAt(document, 5);

    const auto rows = rowTexts(document, map);

    check(rows.size() == 2);
    check(rows[1] == "\tx");

    // Column 4 on the second row, not column 8: the tab starts the row.
    const auto offset = document.offsetAt(0, 6);

    check(map.columnOfOffset(document, offset) == 4);
};

auto tEmptyLinesKeepARow = test("LineMap/anEmptyLineStillHasARow") = []
{
    const auto document = Document::fromText("a\n\nb");
    const auto map = wrappedAt(document, 4);

    check(map.rowCount(document) == 3);
    check(map.row(document, 1).length() == 0);
};

auto tRowsAccumulateAcrossLines = test("LineMap/rowsAccumulateAcrossLines") = []
{
    const auto document = Document::fromText("aaaaaa\nb\ncccccccccc");
    const auto map = wrappedAt(document, 4);

    // 2 rows, 1 row, 3 rows.
    check(map.rowCount(document) == 6);
    check(map.firstRowOfLine(0) == 0);
    check(map.firstRowOfLine(1) == 2);
    check(map.firstRowOfLine(2) == 3);
    check(map.rowsInLine(2) == 3);

    check(map.lineOfRow(document, 0) == 0);
    check(map.lineOfRow(document, 1) == 0);
    check(map.lineOfRow(document, 2) == 1);
    check(map.lineOfRow(document, 5) == 2);
};

// --- offsets and columns ----------------------------------------------------

// An offset exactly on a break belongs to the row it starts, not the one it
// ends: that is where the caret sits after moving right past the last character
// of the row above, and putting it on the row above leaves the caret drawn
// hanging off the right margin.
auto tOffsetOnABreakBelongsBelow = test("LineMap/anOffsetOnABreakStartsTheRow") = []
{
    const auto document = Document::fromText("aaaa bbbb");
    const auto map = wrappedAt(document, 5);

    check(map.rowOfOffset(document, 4) == 0);
    check(map.rowOfOffset(document, 5) == 1);
    check(map.columnOfOffset(document, 5) == 0);
};

auto tColumnsRoundTrip = test("LineMap/rowsAndColumnsRoundTrip") = []
{
    const auto document = Document::fromText("hello there\nsecond line here");
    const auto map = wrappedAt(document, 6);

    for (std::size_t offset = 0; offset <= document.length(); ++offset)
    {
        const auto row = map.rowOfOffset(document, offset);
        const auto column = map.columnOfOffset(document, offset);

        check(map.offsetAtColumn(document, row, column) == offset);
    }
};

// A column past the end of a short row lands on its end rather than walking
// into the next one, which is what makes vertical movement through a short line
// stop short without losing the column it is holding.
auto tColumnsClampToTheRow = test("LineMap/aColumnPastTheRowClampsToIt") = []
{
    const auto document = Document::fromText("aaaaaa\nbb");
    const auto map = wrappedAt(document, 4);

    check(map.offsetAtColumn(document, 2, 20) == document.length());
};

// --- the incremental update against a full rebuild --------------------------

namespace
{
// The oracle: the map that a rebuild from scratch would produce.
bool matchesRebuild(const Document& document, const LineMap& map)
{
    auto rebuilt = LineMap {};
    rebuilt.setWrapColumns(document, map.wrapColumns());

    if (rebuilt.rowCount(document) != map.rowCount(document))
        return false;

    for (std::size_t line = 0; line < document.lineCount(); ++line)
        if (rebuilt.firstRowOfLine(line) != map.firstRowOfLine(line))
            return false;

    return rowTexts(document, rebuilt) == rowTexts(document, map);
}
} // namespace

// PLAN.md §9: test an optimisation against an oracle rather than against cases.
//
// And the counter is not decoration. An incremental update that quietly fell
// back to rebuilding would agree with a rebuild on every input, so the oracle
// alone cannot tell the fast path from the slow one — the test would pass with
// the whole optimisation deleted.
auto tIncrementalMatchesRebuild =
    test("LineMap/incrementalUpdatesAgreeWithAFullRebuild") = []
{
    auto document = Document::fromText("alpha beta\ngamma\ndelta epsilon zeta\n");

    auto map = LineMap {};
    map.setWrapColumns(document, 7);

    const auto rebuildsAfterSetup = map.rebuildCount();

    const struct
    {
        std::size_t start;
        std::size_t end;
        const char* text;
    } edits[] = {
        {0, 0, "x"}, // insert at the very start
        {4, 4, "\n"}, // split a line
        {6, 12, ""}, // delete across a line boundary
        {2, 2, "one two three\n"}, // insert several lines
        {0, 5, "\n\n"}, // replace with only newlines
        {9, 9, " tail"}, // plain insertion
        {1, 14, "collapse"}, // large replacement spanning lines
        {0, 0, "\n"}, // newline at the very start
        {3, 3, "é"}, // multi-byte insertion
        {2, 2, "\t"}, // a tab, which is worth several columns
    };

    for (const auto& edit: edits)
    {
        const auto applied = document.replace(edit.start, edit.end, edit.text);

        map.applyEdit(document, applied);
        check(matchesRebuild(document, map));
    }

    // An append at the very end of the document is its own case: it starts on a
    // line that did not exist before it, so the line it grew out of is the one
    // that has to be re-measured.
    for (auto i = 0; i < 3; ++i)
    {
        const auto applied =
            document.replace(document.length(), document.length(), "\nmore words");

        map.applyEdit(document, applied);
        check(matchesRebuild(document, map));
    }

    // Deleting the lot, which is the case where every stored row goes away.
    const auto cleared = document.replace(0, document.length(), "");

    map.applyEdit(document, cleared);
    check(matchesRebuild(document, map));

    check(map.rebuildCount() == rebuildsAfterSetup);
};

// Undo replays inverted edits through the same path, so the map has to survive
// them too — and it is the path a bug here would show up on last.
auto tInvertedEditsAlsoAgree = test("LineMap/undoingAnEditAgreesToo") = []
{
    auto document = Document::fromText("one two three four five\nsix\n");

    auto map = LineMap {};
    map.setWrapColumns(document, 8);

    const auto rebuilds = map.rebuildCount();
    auto applied = std::vector<TextEdit> {};

    applied.push_back(document.replace(3, 3, "\nsplit here\n"));
    map.applyEdit(document, applied.back());

    applied.push_back(document.replace(0, 6, "x"));
    map.applyEdit(document, applied.back());

    for (auto edit = applied.rbegin(); edit != applied.rend(); ++edit)
    {
        document.apply(edit->inverted());
        map.applyEdit(document, edit->inverted());

        check(matchesRebuild(document, map));
    }

    check(document.text() == "one two three four five\nsix\n");
    check(map.rebuildCount() == rebuilds);
};

// The break positions of a line are cached, because a draw loop asks for its
// rows one after another. A cache that outlived the edit that invalidated it
// would draw the text as it was before the keystroke.
auto tCacheDoesNotSurviveAnEdit = test("LineMap/anEditInvalidatesTheRowCache") = []
{
    auto document = Document::fromText("aaaa bbbb");

    auto map = LineMap {};
    map.setWrapColumns(document, 5);

    check(map.row(document, 1).textIn(document) == "bbbb");

    const auto edit = document.replace(0, 5, "");
    map.applyEdit(document, edit);

    check(map.rowCount(document) == 1);
    check(map.row(document, 0).textIn(document) == "bbbb");
};

// Turning wrapping off has to put the map back to the free path rather than
// leave stale rows behind it.
auto tTurningWrapOffClearsIt = test("LineMap/turningWrappingOffRestoresLines") = []
{
    const auto document = Document::fromText("a long line that will wrap\nshort");

    auto map = wrappedAt(document, 8);
    check(map.rowCount(document) > 2);

    map.setWrapColumns(document, 0);

    check(!map.wraps());
    check(map.rowCount(document) == 2);
    check(map.row(document, 0).end == document.line(0).size());
};

// --- display columns --------------------------------------------------------

auto tDisplayWidthExpandsTabs = test("LineMap/displayWidthExpandsTabsToStops") = []
{
    check(displayWidth("ab", 4) == 2);
    check(displayWidth("\t", 4) == 4);
    check(displayWidth("a\t", 4) == 4);
    check(displayWidth("abcd\t", 4) == 8);
    check(displayWidth("é", 4) == 1);
};

// A column that falls inside a tab lands before it. Anything else would put an
// offset between two bytes of a character, or inside an expansion that has no
// byte to point at.
auto tOffsetInsideATabLandsOnIt = test("LineMap/aColumnInsideATabLandsBeforeIt") = []
{
    check(offsetAtDisplayColumn("\tx", 0, 4) == 0);
    check(offsetAtDisplayColumn("\tx", 2, 4) == 0);
    check(offsetAtDisplayColumn("\tx", 4, 4) == 1);
    check(offsetAtDisplayColumn("\tx", 9, 4) == 2);
};
