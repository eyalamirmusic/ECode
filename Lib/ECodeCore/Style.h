#pragma once

#include <cstddef>
#include <vector>

namespace ecode
{
class Document;

// What a run of text *is*, rather than what colour it should be.
//
// Kept deliberately small and vocabulary-level rather than mirroring any one
// grammar's capture names: a syntax engine maps its own captures onto these,
// and a theme maps these onto colours. Neither side has to know about the
// other, and swapping tree-sitter for something else does not reach the theme.
enum class TokenKind
{
    Text,
    Keyword,
    String,
    Comment,
    Number,
    Function,
    Type,
    Constant,
    Operator,
    Punctuation,
    Preprocessor
};

// A styled run within one line, measured in bytes from the line's start.
//
// Byte offsets rather than character indices because that is what both the
// document and the syntax engine work in; the renderer is walking bytes as it
// decodes UTF-8 anyway, so it can compare directly without converting.
struct StyleSpan
{
    std::size_t start = 0;
    std::size_t length = 0;
    TokenKind kind = TokenKind::Text;

    std::size_t end() const { return start + length; }
};

// Spans for a single line, sorted by start and non-overlapping. Gaps are plain
// text — a highlighter only has to describe what it recognises.
using LineStyle = std::vector<StyleSpan>;

// Produces styling for a document, one line at a time.
//
// An interface so ECodeRender never links a syntax engine: the renderer asks
// for the visible lines' spans and knows nothing about how they were derived.
class Highlighter
{
public:
    virtual ~Highlighter() = default;

    // Makes sure lineStyle() has answers for [firstLine, lastLine). Called with
    // exactly the lines about to be drawn, which is what keeps the cost of
    // scrolling proportional to the viewport rather than to the file: a parser
    // may hold a tree for the whole document, but querying all of it per frame
    // would put file size back into the frame time.
    //
    // Part of the interface rather than of one implementation, so a view can
    // drive any highlighter without knowing which it has. Defaulted because a
    // highlighter that computes everything up front has nothing to do here.
    virtual void update(const Document&, std::size_t firstLine, std::size_t lastLine)
    {
        (void) firstLine;
        (void) lastLine;
    }

    // Spans for one line. Returning empty means "plain text", which is both the
    // correct answer for an unrecognised language and a safe fallback when a
    // parse has not finished yet.
    virtual const LineStyle& lineStyle(std::size_t line) = 0;
};

// Finds the span covering a byte offset, or nullptr for the gaps between spans.
// Linear from a caller-held cursor rather than a binary search, because the
// renderer walks a line left to right and would otherwise re-search per glyph.
inline const StyleSpan*
    spanAt(const LineStyle& spans, std::size_t offset, std::size_t& cursor)
{
    while (cursor < spans.size() && spans[cursor].end() <= offset)
        ++cursor;

    if (cursor >= spans.size() || offset < spans[cursor].start)
        return nullptr;

    return &spans[cursor];
}
} // namespace ecode
