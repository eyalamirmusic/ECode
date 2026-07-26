#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ecode
{
class Document;
struct TextEdit;

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
    // A floor rather than a promise of exactness: an implementation may answer
    // for more than it was asked about — SyntaxHighlighter computes a band
    // around the window so that scrolling by a line needs no new work — so a
    // caller may not read anything into a line outside the range having spans.
    //
    // Part of the interface rather than of one implementation, so a view can
    // drive any highlighter without knowing which it has. Defaulted because a
    // highlighter that computes everything up front has nothing to do here.
    virtual void update(const Document&, std::size_t firstLine, std::size_t lastLine)
    {
        (void) firstLine;
        (void) lastLine;
    }

    // Tells the highlighter the document changed, with `document` in the state
    // *after* the edit, so the next update can reuse whatever the edit did not
    // touch instead of starting from the whole text.
    //
    // On the interface for the reason update() is: a workspace wires each open
    // file's editor to that file's own highlighter, and it has no business
    // knowing which implementation it holds. Defaulted because a highlighter
    // that recomputes from the text has nothing incremental to be told.
    virtual void applyEdit(const Document&, const TextEdit&) {}

    // Discards whatever was derived from the old text. For opening a file, or
    // any change too wholesale to describe as a TextEdit.
    virtual void reset() {}

    // Spans for one line. Returning empty means "plain text", which is both the
    // correct answer for an unrecognised language and a safe fallback when a
    // parse has not finished yet.
    virtual const LineStyle& lineStyle(std::size_t line) = 0;

    // Changes whenever a line already reported on could now come back
    // differently — a reparse, a new grammar, a theme of kinds.
    //
    // The renderer caches the colours it derived from lineStyle(), and the
    // document's own revision does not cover this: a parse that finishes, or a
    // reset, changes the spans without changing a byte of the text. A
    // highlighter that computes everything up front and never changes its mind
    // can leave this alone; one that does change its mind and does not tick it
    // will be drawn with the colours it gave the first time.
    virtual std::uint64_t version() const { return 0; }
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
