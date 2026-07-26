#include "Common.h"

#include <ECodeSyntax/SyntaxHighlighter.h>

#include <algorithm>

// tree-sitter highlighting, asserted on the spans rather than on pixels.
//
// These deliberately do not check exact byte offsets for whole constructs —
// that would be a test of the grammar's node boundaries, which move between
// grammar releases. What they check is the contract the renderer depends on:
// that the right *kind* covers the right *text*, that spans stay inside their
// line, and that only the requested range is populated.

using namespace nano;
using namespace ecode;

namespace
{
// The kind covering a line's first occurrence of `needle`, or Text if none.
TokenKind kindOf(SyntaxHighlighter& highlighter,
                 const Document& document,
                 std::size_t line,
                 std::string_view needle)
{
    const auto text = document.line(line);
    const auto at = text.find(needle);

    if (at == std::string_view::npos)
        return TokenKind::Text;

    auto cursor = std::size_t {0};
    const auto* span = spanAt(highlighter.lineStyle(line), at, cursor);

    return span != nullptr ? span->kind : TokenKind::Text;
}

// Long enough that the band queried around a window is a small part of it,
// which is what the range tests below have to be measured against: the query is
// widened past what was asked for so that scrolling a line at a time does not
// re-run it, so "outside the range" has to mean well outside.
Document longSample()
{
    auto text = std::string {};

    for (auto line = 0; line < 500; ++line)
        text += "int value" + std::to_string(line) + " = 1; // comment\n";

    return Document::fromText(std::move(text));
}

Document sample()
{
    return Document::fromText("#include <string>\n" // 0
                              "\n" // 1
                              "// a comment line\n" // 2
                              "int counter = 42;\n" // 3
                              "const char* name = \"hi\";\n" // 4
                              "\n" // 5
                              "void doWork()\n" // 6
                              "{\n" // 7
                              "    return;\n" // 8
                              "}\n"); // 9
}
} // namespace

auto tLoadsGrammarAndQuery = test("Syntax/loadsTheGrammarAndQuery") = []
{
    const auto highlighter = SyntaxHighlighter {};

    // A failure here means the grammar ABI or the generated query is broken,
    // and every test below would silently pass by reporting plain text.
    check(highlighter.isValid());
};

auto tHighlightsKeywords = test("Syntax/highlightsKeywords") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto document = sample();
    highlighter.update(document, 0, document.lineCount());

    check(kindOf(highlighter, document, 8, "return") == TokenKind::Keyword);
    check(kindOf(highlighter, document, 4, "const") == TokenKind::Keyword);
};

// Primitive types are Type, not Keyword. Worth its own test because the
// obvious expectation is wrong: `int` reads like a keyword, but the grammar
// calls it a primitive_type and the query captures it as @type — which is also
// what every editor colours it as.
auto tPrimitiveTypesAreTypes = test("Syntax/primitiveTypesAreTypesNotKeywords") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto document = sample();
    highlighter.update(document, 0, document.lineCount());

    check(kindOf(highlighter, document, 3, "int") == TokenKind::Type);
    check(kindOf(highlighter, document, 6, "void") == TokenKind::Type);
    check(kindOf(highlighter, document, 4, "char") == TokenKind::Type);
};

auto tHighlightsFunctionNames = test("Syntax/highlightsFunctionNames") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto document = sample();
    highlighter.update(document, 0, document.lineCount());

    check(kindOf(highlighter, document, 6, "doWork") == TokenKind::Function);
};

auto tHighlightsComments = test("Syntax/highlightsComments") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto document = sample();
    highlighter.update(document, 0, document.lineCount());

    check(kindOf(highlighter, document, 2, "//") == TokenKind::Comment);
    check(kindOf(highlighter, document, 2, "comment") == TokenKind::Comment);
};

auto tHighlightsStringsAndNumbers = test("Syntax/highlightsStringsAndNumbers") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto document = sample();
    highlighter.update(document, 0, document.lineCount());

    check(kindOf(highlighter, document, 3, "42") == TokenKind::Number);
    check(kindOf(highlighter, document, 4, "\"hi\"") == TokenKind::String);
};

// The catch-all `(identifier) @variable` must not colour ordinary identifiers,
// or every name in the file competes with the captures that mean something.
auto tPlainIdentifiersStayUnstyled = test("Syntax/ordinaryIdentifiersStayPlain") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto document = sample();
    highlighter.update(document, 0, document.lineCount());

    check(kindOf(highlighter, document, 3, "counter") == TokenKind::Text);
};

// Spans must never reach past the end of their own line. A multi-line comment
// overlaps the query range on every line it covers, and the byte offsets come
// back relative to the file, so getting this wrong paints off the end of a row.
auto tSpansStayWithinTheirLine = test("Syntax/spansNeverExceedTheirLine") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto document = Document::fromText("int a = 1;\n"
                                             "/* a comment\n"
                                             "   spanning lines */\n"
                                             "int b = 2;\n");

    highlighter.update(document, 0, document.lineCount());

    for (std::size_t line = 0; line < document.lineCount(); ++line)
    {
        const auto length = document.line(line).size();

        for (const auto& span: highlighter.lineStyle(line))
        {
            check(span.start <= length);
            check(span.end() <= length);
        }
    }
};

// Both interior lines of a block comment are styled, which only happens if a
// capture overlapping the range is split across the lines it covers.
auto tMultiLineCommentCoversEveryLine =
    test("Syntax/blockCommentsCoverEveryLine") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto document = Document::fromText("/* first\n"
                                             "   second\n"
                                             "   third */\n"
                                             "int x = 0;\n");

    highlighter.update(document, 0, document.lineCount());

    check(kindOf(highlighter, document, 0, "first") == TokenKind::Comment);
    check(kindOf(highlighter, document, 1, "second") == TokenKind::Comment);
    check(kindOf(highlighter, document, 2, "third") == TokenKind::Comment);
    check(kindOf(highlighter, document, 3, "int") == TokenKind::Type);
};

// The property the whole design rests on: the work is bounded by what is on
// screen, so scrolling a large file costs what is in the window rather than what
// is in the document.
//
// Bounded, not exact — the band queried is the window plus a fixed margin either
// side, so that scrolling by a line does not re-run the query. What must still
// hold is that a line far from the window is not computed at all.
auto tOnlyTheRequestedRangeIsPopulated =
    test("Syntax/populatesOnlyTheRequestedRange") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto document = longSample();

    highlighter.update(document, 200, 205);

    check(!highlighter.lineStyle(200).empty());
    check(highlighter.lineStyle(0).empty()); // far before the range
    check(highlighter.lineStyle(499).empty()); // far after it
};

// Moving the range must recompute rather than accumulate: a cursor has to be
// re-exec'd per range, and forgetting that leaves the previous range's spans
// behind or yields nothing at all.
auto tRangeCanMove = test("Syntax/movingTheRangeRecomputes") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto document = longSample();

    highlighter.update(document, 0, 3);
    check(!highlighter.lineStyle(2).empty());
    check(highlighter.lineStyle(400).empty());

    highlighter.update(document, 400, 403);
    check(!highlighter.lineStyle(400).empty());
    check(highlighter.lineStyle(2).empty()); // the old range is gone
};

// reset() discards the tree, for a document replaced wholesale.
auto tResetReparsesFromScratch = test("Syntax/resetReparsesANewDocument") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto first = Document::fromText("int a = 1;\n");
    highlighter.update(first, 0, 1);
    check(kindOf(highlighter, first, 0, "int") == TokenKind::Type);

    const auto second = Document::fromText("// now a comment\n");
    highlighter.reset();
    highlighter.update(second, 0, 1);
    check(kindOf(highlighter, second, 0, "//") == TokenKind::Comment);
};

// The safety net: a caller that swaps the document without saying so still gets
// correct highlighting, as long as the length changed. Reporting edits is the
// contract, but silently stale colours are a worse failure than a slow reparse.
auto tDetectsAnUnreportedSwap = test("Syntax/detectsAnUnreportedDocumentSwap") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto first = Document::fromText("int a = 1;\n");
    highlighter.update(first, 0, 1);

    const auto second = Document::fromText("// a comment of a different length\n");
    highlighter.update(second, 0, 1); // no reset, no applyEdit

    check(kindOf(highlighter, second, 0, "//") == TokenKind::Comment);
};

// Incremental reparse must agree with a fresh one. The oracle check: the whole
// point of ts_tree_edit is reusing untouched subtrees, and the way that fails is
// by producing *almost* the right tree.
auto tIncrementalMatchesFullParse =
    test("Syntax/incrementalReparseMatchesAFullParse") = []
{
    auto incremental = SyntaxHighlighter {};

    if (!incremental.isValid())
        return;

    auto document = Document::fromText("int value = 1;\n"
                                       "// a comment\n"
                                       "void run() { return; }\n");

    incremental.update(document, 0, document.lineCount());

    // A varied sequence: inside a token, at a boundary, adding and removing a
    // line, and turning code into a comment.
    const struct
    {
        std::size_t start;
        std::size_t end;
        const char* text;
    } edits[] = {
        {4, 9, "counter"}, // rename an identifier
        {0, 3, "double"}, // change the type
        {0, 0, "// lead\n"}, // insert a line at the top
        {0, 8, ""}, // and take it away again
        {0, 0, "/*"}, // open a block comment: a big tree change
        {2, 2, "*/"}, // and close it
    };

    for (const auto& edit: edits)
    {
        const auto applied = document.replace(edit.start, edit.end, edit.text);
        incremental.applyEdit(document, applied);
        incremental.update(document, 0, document.lineCount());

        // A highlighter that has never seen an edit, parsing the same text.
        auto fresh = SyntaxHighlighter {};
        fresh.update(document, 0, document.lineCount());

        for (std::size_t line = 0; line < document.lineCount(); ++line)
        {
            const auto& incrementalSpans = incremental.lineStyle(line);
            const auto& freshSpans = fresh.lineStyle(line);

            check(incrementalSpans.size() == freshSpans.size());

            for (std::size_t span = 0;
                 span < incrementalSpans.size() && span < freshSpans.size();
                 ++span)
            {
                check(incrementalSpans[span].start == freshSpans[span].start);
                check(incrementalSpans[span].length == freshSpans[span].length);
                check(incrementalSpans[span].kind == freshSpans[span].kind);
            }
        }
    }
};

auto tEmptyDocumentIsSafe = test("Syntax/emptyDocumentProducesNoSpans") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto document = Document::fromText("");
    highlighter.update(document, 0, document.lineCount());

    check(highlighter.lineStyle(0).empty());
};

// Ranges past the end of the document must not read out of bounds.
auto tRangePastEndIsSafe = test("Syntax/rangeBeyondTheDocumentIsSafe") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto document = Document::fromText("int a = 1;\n");

    highlighter.update(document, 0, 500);
    check(highlighter.lineStyle(400).empty());

    highlighter.update(document, 900, 950);
    check(highlighter.lineStyle(900).empty());
};

// Several distinct kinds must appear across a realistic file, which is what
// makes highlighting visible at all rather than a uniform wash.
auto tProducesVariedKinds = test("Syntax/producesSeveralDistinctKinds") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto document = sample();
    highlighter.update(document, 0, document.lineCount());

    auto kinds = std::vector<TokenKind> {};

    for (std::size_t line = 0; line < document.lineCount(); ++line)
        for (const auto& span: highlighter.lineStyle(line))
            if (std::find(kinds.begin(), kinds.end(), span.kind) == kinds.end())
                kinds.push_back(span.kind);

    check(kinds.size() >= 4);
};

// --- not doing the work twice ----------------------------------------------
//
// The query is the most expensive thing in a frame, and an editor sitting still
// asks for the same lines of the same text on every one of them. What follows
// is aimed at the two ways of getting that wrong: running it again for nothing,
// and *not* running it when the answer would have changed.

auto tRepeatedRangeIsNotQueriedTwice =
    test("Syntax/theSameRangeIsNotQueriedTwice") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto document = sample();

    highlighter.update(document, 0, document.lineCount());

    const auto after = highlighter.queries();

    check(after == 1);

    highlighter.update(document, 0, document.lineCount());

    check(highlighter.queries() == after);

    // Still answering, which is the half that keeps the skip honest.
    check(!highlighter.lineStyle(0).empty());
};

// A narrower band is already covered: a line's spans do not depend on the range
// it was asked for, because captures overlapping the range are delivered and
// clamped per line.
auto tNarrowerRangeReusesTheQuery =
    test("Syntax/aNarrowerRangeReusesTheWiderQuery") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto document = sample();

    highlighter.update(document, 0, document.lineCount());

    // Line 2 is the comment, so it has spans to still be there — line 1 of the
    // sample is blank, and a blank line reports nothing whether the query was
    // reused or thrown away.
    highlighter.update(document, 2, 3);

    check(highlighter.queries() == 1);
    check(!highlighter.lineStyle(2).empty());
};

// Scrolling to lines that were never queried has to query them, or they come
// back plain and the file looks half-highlighted.
auto tWiderRangeQueriesAgain = test("Syntax/aRangeNotYetQueriedIsQueried") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto document = longSample();

    highlighter.update(document, 0, 5);

    check(highlighter.queries() == 1);

    // Far enough that the band around the first window cannot reach it.
    highlighter.update(document, 400, 405);

    check(highlighter.queries() == 2);
    check(!highlighter.lineStyle(400).empty());
};

// The margin's whole purpose: a window that moves by a line is inside the band
// already queried, so scrolling runs no query at all. Without it, every scrolled
// row re-runs the most expensive thing in the frame.
auto tSmallScrollReusesTheQuery = test("Syntax/scrollingALineReusesTheQuery") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto document = longSample();

    highlighter.update(document, 200, 240);

    check(highlighter.queries() == 1);

    for (auto line = std::size_t {201}; line < 240; ++line)
        highlighter.update(document, line, line + 40);

    check(highlighter.queries() == 1);

    // And still answering for the lines that scrolled into view.
    check(!highlighter.lineStyle(275).empty());
};

// The expensive direction. A skipped query after an edit means the spans
// describe text that is no longer there — the colours drift off the words, and
// nothing about it looks like a caching bug.
//
// What this actually pins is the reparse path: a reported edit makes the tree
// dirty, and reparsing forgets the query. The revision comparison inside
// queryCovers is *also* enough on its own, so deleting it leaves this test
// green — see the note there, and PLAN.md §9.
auto tEditRequeries = test("Syntax/anEditIsQueriedAgain") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    auto document = Document::fromText("int a = 1;\nreturn a;\n");

    highlighter.update(document, 0, document.lineCount());

    const auto before = highlighter.lineStyle(0);

    check(highlighter.queries() == 1);
    check(!before.empty());

    // Turns the whole first line into a comment, so its spans must change.
    const auto edit = document.replace(0, 0, "// ");
    highlighter.applyEdit(document, edit);

    highlighter.update(document, 0, document.lineCount());

    check(highlighter.queries() == 2);

    const auto after = highlighter.lineStyle(0);

    check(!after.empty());
    check(after.front().kind == TokenKind::Comment);
};

// A reparse changes what a line already reported comes back as, and the
// document's own revision cannot say so — the text did not change, the tree
// did. Anything caching colours reads this instead.
auto tVersionTicksOnReparse = test("Syntax/theVersionTicksOnAReparse") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    auto document = Document::fromText("int a = 1;\n");

    highlighter.update(document, 0, document.lineCount());

    const auto parsed = highlighter.version();

    // No change: nothing was reparsed, so nothing downstream should throw work
    // away.
    highlighter.update(document, 0, document.lineCount());

    check(highlighter.version() == parsed);

    const auto edit = document.replace(0, 0, "// ");
    highlighter.applyEdit(document, edit);
    highlighter.update(document, 0, document.lineCount());

    check(highlighter.version() > parsed);

    highlighter.reset();

    check(highlighter.version() > parsed + 1);
};
