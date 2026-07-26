#pragma once

#include "Document.h"
#include "TextEdit.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ecode
{
// --- display columns -------------------------------------------------------
//
// The unit both wrapping and horizontal placement work in. Not bytes — a tab is
// one byte and up to tabWidth columns — and not codepoints, for the same
// reason.
//
// One column per codepoint, which is wrong for CJK and for emoji: those are two
// cells wide in every editor and terminal. Fixing it needs a width table, which
// is PLAN.md's gap 9. Until then a wrapped line of CJK breaks late rather than
// in the wrong place, which is the cheaper of the two failures.

std::size_t displayWidth(std::string_view text, std::size_t tabWidth);

// The display column a byte offset within `text` sits at.
std::size_t
    displayColumnAt(std::string_view text, std::size_t offset, std::size_t tabWidth);

// The byte offset of a display column, rounded to a character boundary and
// clamped to the text. A column inside a tab lands on the tab rather than
// splitting it.
std::size_t offsetAtDisplayColumn(std::string_view text,
                                  std::size_t column,
                                  std::size_t tabWidth);

// --- the mapping -----------------------------------------------------------

// One row of text on screen: a slice of a logical line, or the whole of it when
// nothing is wrapping.
//
// Offsets are within the line rather than within the document, because that is
// what the renderer needs to slice `Document::line` and what tab stops are
// measured from — a continuation row starts its own stops, since it starts at
// the left margin.
struct VisualRow
{
    std::size_t line = 0;
    std::size_t start = 0;
    std::size_t end = 0;

    // Only the first row of a line carries its number.
    bool isContinuation() const { return start > 0; }

    std::size_t length() const { return end - start; }

    std::string_view textIn(const Document& document) const
    {
        return document.line(line).substr(start, end - start);
    }
};

// Which visual row each logical line occupies, so that nothing above this has
// to assume a line is a row.
//
// That assumption is the one PLAN.md §7.3 names as the most expensive to leave
// in place: soft wrap, folding, inline diagnostics and image lines all break
// it, and every one of them is a change to this class rather than to the
// renderer, the widget and the cursor. Soft wrap is the first of them and the
// reason this exists now rather than later — a mapping with no consumer is a
// guess about what the consumer will need.
//
// Row *height* is still uniform and still the renderer's, which is the half of
// "variable line height" not done here. The mapping is the part that could not
// be retrofitted; a per-row height is a lookup added to a class that already
// has one row per screen strip.
//
// The document is passed in rather than held, so the map cannot outlive it or
// be copied into a dangling reference. The cost is that the caller must keep
// them in step, which is why Editor owns both.
class LineMap
{
public:
    static constexpr std::size_t defaultTabWidth = 4;

    // Zero turns wrapping off, and off is a genuinely free path: nothing is
    // stored per line, every query is arithmetic on the document's own line
    // index, and an edit costs nothing here at all.
    void setWrapColumns(const Document& document, std::size_t columns);
    std::size_t wrapColumns() const { return columns; }
    bool wraps() const { return columns > 0; }

    void setTabWidth(const Document& document, std::size_t width);
    std::size_t tabWidth() const { return tabColumns; }

    void rebuild(const Document& document);

    // Repairs the map around an edit that has already been applied to the
    // document. Only the lines the edit touched are re-wrapped; everything
    // after them shifts.
    void applyEdit(const Document& document, const TextEdit& edit);

    std::size_t rowCount(const Document& document) const;

    std::size_t firstRowOfLine(std::size_t line) const;
    std::size_t rowsInLine(std::size_t line) const;

    std::size_t lineOfRow(const Document& document, std::size_t row) const;

    // Clamped rather than checked: a row past the end answers with the last
    // one, because every caller of this is a draw loop or a hit test and
    // neither has anything useful to do with a failure.
    VisualRow row(const Document& document, std::size_t index) const;

    std::size_t rowOfOffset(const Document& document, std::size_t offset) const;

    // Where a document offset sits horizontally, in display columns measured
    // from its own row's left edge.
    std::size_t columnOfOffset(const Document& document, std::size_t offset) const;

    // The reverse: a document offset on `row` at that column, clamped to the
    // row. This is what vertical movement lands on.
    std::size_t offsetAtColumn(const Document& document,
                               std::size_t row,
                               std::size_t column) const;

    // How many times the whole map has been rebuilt from scratch.
    //
    // Exposed for tests rather than for callers: an incremental update that
    // quietly fell back to a rebuild would pass an oracle comparing it against
    // a rebuild, which is the one thing that oracle cannot see. See PLAN.md §9.
    std::uint64_t rebuildCount() const { return rebuilds; }

private:
    // Row starts of one line's text, always beginning at 0.
    void breaksOf(std::string_view text, std::vector<std::size_t>& out) const;

    // The same, cached: a draw loop asks for consecutive rows of one line, and
    // recomputing its breaks per row would make drawing a wrapped paragraph
    // quadratic in its length.
    const std::vector<std::size_t>& breaksOfLine(const Document& document,
                                                 std::size_t line) const;

    // The scratch vector is the caller's so that building the whole map does
    // not allocate once per line.
    std::size_t rowsIn(std::string_view text,
                       std::vector<std::size_t>& scratch) const;

    void invalidateCache() { cachedLine = noLine; }

    static constexpr auto noLine = static_cast<std::size_t>(-1);

    std::size_t columns = 0;
    std::size_t tabColumns = defaultTabWidth;

    // Empty while wrapping is off. Otherwise one entry per line holding the
    // index of its first row, plus a final entry holding the total — so a
    // line's row count is the difference between two adjacent entries and no
    // case is needed for the last line.
    std::vector<std::size_t> rowStarts;

    mutable std::size_t cachedLine = noLine;
    mutable std::vector<std::size_t> cachedBreaks;

    std::uint64_t rebuilds = 0;
};
} // namespace ecode
