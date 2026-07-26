#pragma once

#include "PaintContext.h"
#include "RowCache.h"

#include <ECodeCore/Editor.h>
#include <ECodeCore/Document.h>
#include <ECodeCore/LineMap.h>
#include <ECodeCore/Search.h>
#include <ECodeCore/Style.h>

#include <eacp/Sprites/Sprites.h>
#include <eacp/Text/Text.h>

namespace ecode
{
// Theme colours the renderer needs. A placeholder until themes are data-driven.
struct TextTheme
{
    eacp::Graphics::Color background {0.118f, 0.125f, 0.149f};
    eacp::Graphics::Color text {0.85f, 0.87f, 0.91f};
    eacp::Graphics::Color lineNumber {0.38f, 0.41f, 0.48f};
    eacp::Graphics::Color currentLineNumber {0.75f, 0.78f, 0.85f};
    eacp::Graphics::Color gutterEdge {1.f, 1.f, 1.f, 0.05f};
    eacp::Graphics::Color caret {0.55f, 0.78f, 0.98f};
    eacp::Graphics::Color selection {0.22f, 0.32f, 0.46f};
    eacp::Graphics::Color currentLine {1.f, 1.f, 1.f, 0.035f};

    // Search hits. Every match gets the dim one and the match being looked at
    // gets the bright one, because the two answer different questions — "where
    // else is this?" and "which one am I on?" — and a single colour for both
    // makes the second unanswerable without counting.
    //
    // Both are drawn under the glyphs, so they have to stay dark enough to read
    // through. That is why the current match is a stronger orange rather than a
    // lighter fill: raising the brightness far enough to distinguish it would
    // start to swallow the text on top of it.
    eacp::Graphics::Color searchMatch {0.35f, 0.31f, 0.16f};
    eacp::Graphics::Color currentSearchMatch {0.62f, 0.44f, 0.13f};

    // One colour per TokenKind. A syntax engine maps its captures onto kinds and
    // never names a colour; this is the only place colours live.
    eacp::Graphics::Color keyword {0.78f, 0.57f, 0.92f};
    eacp::Graphics::Color string {0.65f, 0.85f, 0.55f};
    eacp::Graphics::Color comment {0.42f, 0.47f, 0.55f};
    eacp::Graphics::Color number {0.95f, 0.72f, 0.45f};
    eacp::Graphics::Color function {0.45f, 0.72f, 0.95f};
    eacp::Graphics::Color type {0.40f, 0.85f, 0.82f};
    eacp::Graphics::Color constant {0.95f, 0.62f, 0.60f};
    eacp::Graphics::Color operatorColor {0.80f, 0.82f, 0.88f};
    eacp::Graphics::Color punctuation {0.62f, 0.66f, 0.74f};
    eacp::Graphics::Color preprocessor {0.90f, 0.68f, 0.50f};

    const eacp::Graphics::Color& colorFor(TokenKind kind) const;
};

// What the renderer draws on top of the text, as against the text itself.
//
// Grouped rather than passed one by one: draw() was already at seven arguments
// with two bools among them, and search would have taken it to nine. A caller
// that wants none of this passes a default-constructed one and gets a plain
// document.
struct EditorOverlay
{
    // Null while the editor has no caret to show — a view being rendered to an
    // image, or one that has never been focused.
    //
    // The whole set rather than the primary: every cursor draws a caret, and
    // every selection is painted. A renderer given only the primary would draw
    // a multi-cursor edit as though it were happening in one place, which is
    // the one thing about it that has to be visible.
    const CursorSet* cursors = nullptr;
    bool caretVisible = false;

    // Every search hit on screen, and which of them the find bar is on. Null
    // when nothing is being searched for.
    const eacp::Vector<SearchMatch>* matches = nullptr;
    int currentMatch = -1;
};

// What is being drawn: the text, how its lines fall onto rows, and how it is
// coloured.
//
// Grouped for the reason EditorOverlay was — draw() stood at seven arguments
// and the line map would have made it eight — but also because the three are
// only ever correct together. A map describes one document at one wrap width,
// and passing them separately invites a caller to hand over a map built for
// something else.
//
// `highlighter` may be null, in which case everything draws as plain text.
struct DocumentView
{
    const Document& document;
    const LineMap& lines;
    Highlighter* highlighter = nullptr;
};

// Draws the visible slice of a Document through a glyph atlas.
//
// Only the rows actually on screen are touched — the loop is bounded by the
// viewport, not by the document — so scrolling a 100 MB file costs the same as
// scrolling a small one. That is the property the whole viewer milestone rests
// on, and it is easy to lose by iterating the document and clipping late.
//
// Rows, not lines. With soft wrap on, one logical line is several rows, and
// nothing here may assume otherwise: PLAN.md §7.3 names `row n at n *
// lineHeight` as the assumption that gets more expensive the longer it is left
// in. It now lives in exactly two places — LineMap for which text a row holds,
// and rowTop() for where the row sits.
class TextRenderer
{
public:
    // `backingScale` is the scale the atlas was rasterized at, which is what
    // turns its device-pixel slots back into points. It belongs here rather
    // than arriving with each frame's PaintContext because the two passes over
    // a row — rasterizing its glyphs and laying them out — have to agree about
    // it, and only one of them is given a context.
    TextRenderer(eacp::Text::GlyphAtlas& atlasToUse,
                 const TextTheme& themeToUse,
                 float backingScale);

    // Lays out and draws the rows visible in `viewport` at the given scroll
    // offset. Clipping goes through the context rather than straight to the
    // pass: the gutter and the text each need their own clip, and both have to
    // be intersected with whatever the enclosing widget already narrowed the
    // scissor to — otherwise an editor inside a scrolling container would draw
    // outside it.
    //
    // Every glyph the frame needs must already be in the atlas: call
    // prepare() first, then GlyphAtlas::commit(), then this. Uploading in the
    // middle of a pass would mutate a texture the earlier draws have bound.
    void draw(PaintContext& context,
              const DocumentView& view,
              const EditorOverlay& overlay,
              const eacp::Graphics::Rect& viewport,
              float scrollY);

    // Rasterizes the glyphs the next draw() will need, without drawing.
    void prepare(const DocumentView& view,
                 const eacp::Graphics::Rect& viewport,
                 float scrollY);

    float rowHeight() const;

    // Where a row's top edge sits, before scrolling. The only multiplication by
    // a row index in the codebase, and the seam a variable row height replaces.
    float rowTop(std::size_t row) const;

    // Width of the line-number gutter for a document of this many lines.
    float gutterWidth(std::size_t lineCount) const;

    // First and last visual row touching the viewport at this scroll offset.
    std::size_t firstVisibleRow(float scrollY) const;
    std::size_t lastVisibleRow(const DocumentView& view,
                               const eacp::Graphics::Rect& viewport,
                               float scrollY) const;

    // Total height of the document, for the scroll range.
    float contentHeight(const DocumentView& view) const;

    // How many characters fit across the text area, which is the width soft
    // wrap breaks at. Zero when there is no room for even one, so a viewport
    // squeezed to nothing turns wrapping off rather than dividing by it.
    std::size_t wrapColumnsFor(const eacp::Graphics::Rect& viewport,
                               std::size_t lineCount) const;

    // Where a point in the viewport falls in the document, for click-to-place.
    // Clamps rather than failing, so a click in the margin lands on the nearest
    // real position.
    std::size_t offsetAtPoint(const DocumentView& view,
                              const eacp::Graphics::Point& point,
                              const eacp::Graphics::Rect& viewport,
                              float scrollY) const;

    // The x offset of a byte position within a row's own text, so the caret and
    // selection can be placed without re-walking the glyphs. Row-local, because
    // a continuation row starts at the left margin and its tab stops with it.
    float columnToX(std::string_view text, std::size_t column) const;

    // The rows laid out and held for the next frame, for tests: a cache that
    // silently rebuilt every row would draw the same picture as one that never
    // did, so only the counters can tell them apart.
    const RowCache& rows() const { return cache; }

private:
    // Turns one run of text into glyphs placed relative to the row's own
    // origin, which is the expensive half of drawing and the half worth
    // keeping: a UTF-8 decode, an atlas lookup and a span search per character.
    //
    // spans may be null for uniformly coloured text (the line-number gutter).
    // `spanOffset` is where this text starts within the line the spans describe,
    // which is non-zero for every row after a wrap.
    void layoutLine(std::vector<PlacedGlyph>& out,
                    std::string_view text,
                    const LineStyle* spans,
                    std::size_t spanOffset,
                    const eacp::Graphics::Color& color) const;

    // The cheap half: places an already laid-out row at an origin and queues it.
    static void submitLine(eacp::Text::GlyphRenderer& glyphs,
                           const std::vector<PlacedGlyph>& placed,
                           float x,
                           float baseline);

    CachedRow buildRow(const DocumentView& view, std::size_t index) const;

    // Lays a row out unless it already has been, and hands back what to draw.
    const CachedRow& rowLayout(const DocumentView& view, std::size_t index);

    // What the cached rows are only valid for.
    RowCacheStamp stampFor(const DocumentView& view) const;

    void prepareLine(std::string_view text);

    // Paints one byte range as a band per row it covers, clipped to the rows on
    // screen. A selection and a search hit are the same shape, so they are the
    // same code — the only thing that differs is the colour.
    void fillRange(eacp::Sprites::SpriteRenderer& sprites,
                   const DocumentView& view,
                   std::size_t from,
                   std::size_t to,
                   const eacp::Graphics::Rect& textRect,
                   float scrollY,
                   std::size_t first,
                   std::size_t last,
                   const eacp::Graphics::Color& color);

    void drawMatches(eacp::Sprites::SpriteRenderer& sprites,
                     const DocumentView& view,
                     const EditorOverlay& overlay,
                     const eacp::Graphics::Rect& textRect,
                     float scrollY,
                     std::size_t first,
                     std::size_t last);

    eacp::Text::GlyphAtlas& atlas;
    TextTheme theme;

    RowCache cache;

    // Where a row the cache refused is laid out instead. See rowLayout.
    CachedRow scratch;

    float scale = 1.f;
    float advance = 0.f;
    float ascent = 0.f;
    float height = 0.f;
};
} // namespace ecode
