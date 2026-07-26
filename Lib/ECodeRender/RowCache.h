#pragma once

#include <eacp/Graphics/Graphics.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace ecode
{
// One glyph, placed relative to the row it belongs to rather than to the
// window.
//
// Row-local is what makes the cache survive a scroll: the row's text has not
// changed, only where it sits, so the whole entry is reusable by adding an
// origin at submit time. Absolute positions would mean throwing the layout away
// every time the view moved by a pixel.
struct PlacedGlyph
{
    // From the row's own origin: x from where its text starts, y from the
    // baseline.
    eacp::Graphics::Rect destination;

    // In atlas texels, which stay valid while the atlas grows — a shelf only
    // ever extends right and down — and go stale only when it clears.
    eacp::Graphics::Rect source;

    eacp::Graphics::Color color;
    bool colored = false;
};

// Everything drawing one row of text needs, laid out once.
struct CachedRow
{
    std::vector<PlacedGlyph> text;

    // Empty on a continuation row, which carries no number.
    std::vector<PlacedGlyph> number;

    // The number's width, so the gutter can right-align it without building the
    // string again.
    float numberWidth = 0.f;
};

// What a set of laid-out rows was derived from.
//
// Any of these changing means the rows describe something that is no longer on
// screen — different text, different colours, different places in the atlas, or
// text broken into different rows.
struct RowCacheStamp
{
    std::uint64_t document = 0;
    std::uint64_t highlight = 0;
    std::uint32_t atlas = 0;
    std::size_t wrapColumns = 0;
    std::size_t tabWidth = 0;

    bool operator==(const RowCacheStamp& other) const = default;
};

// The rows currently on screen, laid out and kept until something changes them.
//
// This is the damage tracking PLAN.md §7.3 asks for, in the only form the GPU
// leaves open. Skipping the *drawing* of unchanged rows is not available: a
// Metal drawable comes from a rotating pool, so loading its previous contents
// gives whatever was in that texture two or three frames ago rather than the
// last frame. What can be skipped is the *deriving* — the UTF-8 walk, the atlas
// lookup per glyph and the span search that turn a row of bytes into positioned
// quads — and that is what a frame spends its time on. Every frame still draws
// every visible row; an idle one no longer works out how.
//
// Held per window of rows rather than per document: an entry for every row ever
// scrolled past would grow without bound on a large file, so the window is
// narrowed to what is about to be drawn and the rest is dropped.
class RowCache
{
public:
    // Drops everything if the rows were derived from something that has since
    // changed. Returns true when the cache survived, which is what the tests
    // assert on.
    bool revalidate(const RowCacheStamp& stamp);

    // Narrows to [first, last), keeping whatever the old and new windows share.
    void setWindow(std::size_t first, std::size_t last);

    const CachedRow* find(std::size_t row) const;

    // Takes the row and returns where it landed, or null for a row outside the
    // current window — a caller cannot grow the cache past the rows it declared
    // it was drawing, which is what keeps it bounded by the viewport.
    //
    // On refusal `entry` is left untouched rather than moved from, so a caller
    // holding the only copy can still draw it.
    CachedRow* store(std::size_t row, CachedRow&& entry);

    void clear();

    std::size_t rowsHeld() const;

    // How many rows have been laid out, and how many times the whole cache has
    // been thrown away.
    //
    // For tests rather than for callers, and they are what make a test of this
    // real: a cache that quietly rebuilds every row draws exactly the same
    // picture as one that never does, so pixels alone cannot tell the two
    // apart. PLAN.md §9 — an oracle proves the answer, never the path.
    std::uint64_t layouts() const { return laidOut; }
    std::uint64_t discards() const { return discarded; }

private:
    // rows[i] describes row firstRow + i, or nothing if it has not been laid
    // out since the window last moved.
    std::vector<std::optional<CachedRow>> rows;
    std::size_t firstRow = 0;

    RowCacheStamp current;

    std::uint64_t laidOut = 0;
    std::uint64_t discarded = 0;
};
} // namespace ecode
