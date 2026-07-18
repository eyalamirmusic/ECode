#include "Common.h"

// spanAt, the lookup the renderer uses to colour each glyph.
//
// It is a *stateful* cursor rather than a search, because the renderer walks a
// line left to right and re-searching per glyph would make colouring quadratic
// in line length. That makes its contract easy to get subtly wrong — the cursor
// must survive gaps between spans and must never walk backwards — so it is
// worth pinning down properly.

using namespace nano;
using namespace ecode;

namespace
{
LineStyle sample()
{
    // "int" keyword, gap, "main" function, gap, "42" number.
    return {{0, 3, TokenKind::Keyword},
            {4, 4, TokenKind::Function},
            {12, 2, TokenKind::Number}};
}
} // namespace

auto tFindsSpanAtStart = test("Style/findsTheSpanCoveringAnOffset") = []
{
    const auto spans = sample();
    auto cursor = std::size_t {0};

    const auto* span = spanAt(spans, 0, cursor);

    check(span != nullptr);
    check(span->kind == TokenKind::Keyword);
};

auto tFindsSpanInMiddle = test("Style/matchesOffsetsInsideASpan") = []
{
    const auto spans = sample();
    auto cursor = std::size_t {0};

    check(spanAt(spans, 1, cursor)->kind == TokenKind::Keyword);
    check(spanAt(spans, 2, cursor)->kind == TokenKind::Keyword);
};

// The end offset is exclusive, so a span of length 3 covers 0..2 and not 3.
auto tEndIsExclusive = test("Style/spanEndIsExclusive") = []
{
    const auto spans = sample();
    auto cursor = std::size_t {0};

    check(spanAt(spans, 2, cursor) != nullptr);
    check(spanAt(spans, 3, cursor) == nullptr);
};

// Gaps are plain text. A highlighter only describes what it recognises, so the
// lookup has to report "nothing here" rather than snapping to a neighbour.
auto tGapsReturnNothing = test("Style/gapsBetweenSpansAreUnstyled") = []
{
    const auto spans = sample();
    auto cursor = std::size_t {0};

    check(spanAt(spans, 3, cursor) == nullptr); // between keyword and function
    check(spanAt(spans, 8, cursor) == nullptr); // between function and number
    check(spanAt(spans, 11, cursor) == nullptr);
};

// The whole point of the cursor: a single left-to-right sweep visits every
// offset once and still reports each one correctly, including across gaps.
auto tSweepsAcrossTheLine = test("Style/aSingleSweepColoursEveryOffset") = []
{
    const auto spans = sample();
    auto cursor = std::size_t {0};

    const TokenKind expected[] = {
        TokenKind::Keyword,
        TokenKind::Keyword,
        TokenKind::Keyword,
        TokenKind::Text,
        TokenKind::Function,
        TokenKind::Function,
        TokenKind::Function,
        TokenKind::Function,
        TokenKind::Text,
        TokenKind::Text,
        TokenKind::Text,
        TokenKind::Text,
        TokenKind::Number,
        TokenKind::Number,
    };

    for (std::size_t offset = 0; offset < 14; ++offset)
    {
        const auto* span = spanAt(spans, offset, cursor);
        const auto kind = span != nullptr ? span->kind : TokenKind::Text;

        check(kind == expected[offset]);
    }
};

// Past the last span the lookup keeps returning nothing rather than running off
// the end of the vector — a long line with styling only at its start.
auto tPastTheEndIsSafe = test("Style/offsetsPastTheLastSpanAreSafe") = []
{
    const auto spans = sample();
    auto cursor = std::size_t {0};

    for (std::size_t offset = 14; offset < 200; ++offset)
        check(spanAt(spans, offset, cursor) == nullptr);
};

auto tEmptyStyleIsPlainText = test("Style/noSpansMeansPlainText") = []
{
    const auto spans = LineStyle {};
    auto cursor = std::size_t {0};

    check(spanAt(spans, 0, cursor) == nullptr);
    check(spanAt(spans, 50, cursor) == nullptr);
};

// A span starting at zero length must not swallow the offset after it.
auto tZeroLengthSpanIsSkipped = test("Style/zeroLengthSpansAreSkipped") = []
{
    const auto spans =
        LineStyle {{0, 0, TokenKind::Keyword}, {0, 2, TokenKind::String}};
    auto cursor = std::size_t {0};

    const auto* span = spanAt(spans, 0, cursor);

    check(span != nullptr);
    check(span->kind == TokenKind::String);
};

auto tSpanEndArithmetic = test("Style/endIsStartPlusLength") = []
{
    const auto span = StyleSpan {5, 3, TokenKind::Type};

    check(span.end() == 8);
};
