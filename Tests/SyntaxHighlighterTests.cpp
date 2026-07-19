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

// The property the whole design rests on: only the requested lines are
// computed, so scrolling a large file costs what is on screen rather than what
// is in the document.
auto tOnlyTheRequestedRangeIsPopulated =
    test("Syntax/populatesOnlyTheRequestedRange") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto document = sample();

    highlighter.update(document, 3, 5);

    check(!highlighter.lineStyle(3).empty());
    check(highlighter.lineStyle(0).empty()); // before the range
    check(highlighter.lineStyle(8).empty()); // after it
};

// Moving the range must recompute rather than accumulate: a cursor has to be
// re-exec'd per range, and forgetting that leaves the previous range's spans
// behind or yields nothing at all.
auto tRangeCanMove = test("Syntax/movingTheRangeRecomputes") = []
{
    auto highlighter = SyntaxHighlighter {};

    if (!highlighter.isValid())
        return;

    const auto document = sample();

    highlighter.update(document, 0, 3);
    check(!highlighter.lineStyle(2).empty());
    check(highlighter.lineStyle(8).empty());

    highlighter.update(document, 6, 10);
    check(!highlighter.lineStyle(8).empty());
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
