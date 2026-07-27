#include "TextRenderer.h"

#include <ECodeCore/Utf8.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace ecode
{
using namespace eacp;

namespace
{
// Tabs render as this many columns. A real editor makes it configurable and
// aligns to tab stops; a fixed width is enough to keep indentation readable.
constexpr auto tabWidth = 4;

constexpr auto gutterPadding = 12.f;
constexpr auto textPadding = 8.f;
constexpr auto caretWidth = 2.f;

std::string lineNumberText(std::size_t line)
{
    return std::to_string(line + 1);
}
} // namespace

TextRenderer::TextRenderer(Text::GlyphAtlas& atlasToUse,
                           const TextTheme& themeToUse,
                           float backingScale)
    : atlas(atlasToUse)
    , theme(themeToUse)
    , scale(backingScale)
{
    const auto metrics = atlas.metrics();

    advance = metrics.advance;
    ascent = metrics.ascent;

    // Whole points per line: a fractional line height accumulates down the
    // viewport and leaves rows landing on different subpixel phases, which
    // reads as uneven spacing.
    height = std::max(std::round(metrics.lineHeight() * 1.25f), 1.f);
}

float TextRenderer::rowHeight() const
{
    return height;
}

float TextRenderer::columnWidth() const
{
    return advance;
}

float TextRenderer::rowTop(std::size_t row) const
{
    return static_cast<float>(row) * height;
}

float TextRenderer::gutterWidth(std::size_t lineCount) const
{
    const auto digits = lineNumberText(lineCount == 0 ? 0 : lineCount - 1).size();

    return static_cast<float>(digits) * advance + gutterPadding * 2.f;
}

float TextRenderer::contentHeight(const DocumentView& view) const
{
    return rowTop(view.lines.rowCount(view.document));
}

float TextRenderer::textWidth(const Graphics::Rect& viewport,
                              std::size_t lineCount) const
{
    return viewport.w - gutterWidth(lineCount);
}

float TextRenderer::contentWidth(const DocumentView& view) const
{
    const auto widest = view.document.widestLineText();

    // One column past the end rather than one caret width: at a large font size
    // a caret width is much the smaller of the two, and the room being left is
    // for the character about to be typed as much as for the caret marking
    // where it will go.
    return textPadding * 2.f + columnToX(widest, widest.size()) + advance;
}

std::size_t TextRenderer::wrapColumnsFor(const Graphics::Rect& viewport,
                                         std::size_t lineCount) const
{
    if (advance <= 0.f)
        return 0;

    // The caret needs somewhere to sit past the last character, so the width is
    // one column short of what would fit — otherwise a row filled exactly to the
    // edge draws its caret in the gutter of the row below.
    const auto text =
        textWidth(viewport, lineCount) - textPadding * 2.f - caretWidth;

    if (text < advance)
        return 0;

    return static_cast<std::size_t>(text / advance);
}

std::size_t TextRenderer::firstVisibleRow(float scrollY) const
{
    if (scrollY >= 0.f || height <= 0.f)
        return 0;

    return static_cast<std::size_t>(-scrollY / height);
}

std::size_t TextRenderer::lastVisibleRow(const DocumentView& view,
                                         const Graphics::Rect& viewport,
                                         float scrollY) const
{
    if (height <= 0.f)
        return 0;

    const auto rows = static_cast<std::size_t>(viewport.h / height) + 2;

    return std::min(firstVisibleRow(scrollY) + rows,
                    view.lines.rowCount(view.document));
}

void TextRenderer::prepareLine(std::string_view text)
{
    for (std::size_t index = 0; index < text.size();)
    {
        const auto codepoint = Utf8::next(text, index);

        if (codepoint == U'\t')
            continue;

        atlas.glyph(codepoint, Text::FontStyle::Regular);
    }
}

RowCacheStamp TextRenderer::stampFor(const DocumentView& view) const
{
    auto stamp = RowCacheStamp {};

    stamp.document = view.document.revision();
    stamp.highlight = view.highlighter != nullptr ? view.highlighter->version() : 0;
    stamp.atlas = atlas.generation();
    stamp.wrapColumns = view.lines.wrapColumns();
    stamp.tabWidth = view.lines.tabWidth();

    return stamp;
}

void TextRenderer::prepare(const DocumentView& view,
                           const Graphics::Rect& viewport,
                           float scrollY)
{
    const auto first = firstVisibleRow(scrollY);
    const auto last = lastVisibleRow(view, viewport, scrollY);

    // The rows the frame is about to draw, and no others: this is what bounds
    // the cache to a screenful rather than to the file.
    cache.revalidate(stampFor(view));
    cache.setWindow(first, last);

    for (auto index = first; index < last; ++index)
    {
        // A row that survived revalidation was laid out against this same atlas
        // generation, so every glyph it names is still in the texture and there
        // is nothing to rasterize.
        if (cache.find(index) != nullptr)
            continue;

        const auto row = view.lines.row(view.document, index);

        prepareLine(row.textIn(view.document));

        if (!row.isContinuation())
            prepareLine(lineNumberText(row.line));
    }
}

void TextRenderer::layoutLine(std::vector<PlacedGlyph>& out,
                              std::string_view text,
                              const LineStyle* spans,
                              std::size_t spanOffset,
                              const Graphics::Color& color) const
{
    auto pen = 0.f;

    // Walks forward with the loop rather than searching per glyph; the spans and
    // the text are both traversed left to right exactly once.
    auto spanCursor = std::size_t {0};

    for (std::size_t index = 0; index < text.size();)
    {
        const auto glyphStart = index;
        const auto codepoint = Utf8::next(text, index);

        auto glyphColor = color;

        if (spans != nullptr)
            if (const auto* span =
                    spanAt(*spans, spanOffset + glyphStart, spanCursor))
                glyphColor = theme.colorFor(span->kind);

        if (codepoint == U'\t')
        {
            // Advance to the next tab stop rather than by a fixed amount, so
            // indentation lines up the way the file's author saw it.
            const auto column = pen / advance;
            const auto next = std::floor(column / tabWidth + 1.f) * tabWidth;
            pen = next * advance;
            continue;
        }

        const auto glyph = atlas.glyph(codepoint, Text::FontStyle::Regular);

        if (!glyph.valid)
            continue;

        if (!glyph.empty)
        {
            const auto colored = glyph.format == Text::GlyphFormat::Color;

            // The atlas rect is in device pixels; the destination is in points,
            // measured from the row's own origin.
            const auto destination = Graphics::Rect {pen + glyph.offset.x,
                                                     glyph.offset.y,
                                                     glyph.src.w / scale,
                                                     glyph.src.h / scale};

            out.push_back({destination, glyph.src, glyphColor, colored});
        }

        pen += glyph.advance;
    }
}

void TextRenderer::submitLine(Text::GlyphRenderer& glyphs,
                              const std::vector<PlacedGlyph>& placed,
                              float x,
                              float baseline)
{
    for (const auto& glyph: placed)
        glyphs.add({glyph.destination.x + x,
                    glyph.destination.y + baseline,
                    glyph.destination.w,
                    glyph.destination.h},
                   glyph.source,
                   glyph.color,
                   glyph.colored);
}

CachedRow TextRenderer::buildRow(const DocumentView& view, std::size_t index) const
{
    const auto row = view.lines.row(view.document, index);

    auto entry = CachedRow {};

    const auto* spans = view.highlighter != nullptr
                            ? &view.highlighter->lineStyle(row.line)
                            : nullptr;

    layoutLine(entry.text, row.textIn(view.document), spans, row.start, theme.text);

    // A continuation row carries no number: the gutter numbers lines, and a
    // wrapped line is one line. Repeating it down the rows is the single most
    // obvious way to make wrapping look broken.
    if (!row.isContinuation())
    {
        const auto number = lineNumberText(row.line);

        entry.numberWidth = static_cast<float>(number.size()) * advance;

        layoutLine(entry.number, number, nullptr, 0, theme.lineNumber);
    }

    return entry;
}

const CachedRow& TextRenderer::rowLayout(const DocumentView& view, std::size_t index)
{
    if (const auto* held = cache.find(index))
        return *held;

    auto entry = buildRow(view, index);

    if (const auto* stored = cache.store(index, std::move(entry)))
        return *stored;

    // The cache refused the row, which means it is outside the window the frame
    // said it was drawing. Nothing in the app can reach this — draw() declares
    // the window itself — but drawing the row anyway is the difference between
    // a caller that gets it wrong seeing a layout it did not expect and seeing
    // a blank line. `entry` is untouched: store only takes it on success.
    scratch = std::move(entry);

    return scratch;
}

float TextRenderer::columnToX(std::string_view text, std::size_t column) const
{
    // Walks the line rather than multiplying, because a tab is not one advance
    // wide and the caret has to land where the glyph actually is.
    auto x = 0.f;

    for (std::size_t index = 0; index < text.size() && index < column;)
    {
        const auto codepoint = Utf8::next(text, index);

        if (codepoint == U'\t')
        {
            const auto stop = std::floor(x / advance / tabWidth + 1.f) * tabWidth;
            x = stop * advance;
        }
        else
        {
            x += advance;
        }
    }

    return x;
}

float TextRenderer::caretX(const DocumentView& view, std::size_t offset) const
{
    const auto& document = view.document;

    const auto row =
        view.lines.row(document, view.lines.rowOfOffset(document, offset));
    const auto rowStart = document.offsetAt(row.line, row.start);

    // Row-local, because a continuation row starts at the left margin and its
    // tab stops start with it.
    const auto within = offset > rowStart ? offset - rowStart : 0;

    return textPadding + columnToX(row.textIn(document), within);
}

std::size_t TextRenderer::offsetAtPoint(const DocumentView& view,
                                        const Graphics::Point& point,
                                        const Graphics::Rect& viewport,
                                        ScrollOffset scroll) const
{
    const auto& document = view.document;

    const auto gutter = gutterWidth(document.lineCount());
    const auto relativeY = point.y - viewport.y - scroll.y;

    const auto at = static_cast<std::ptrdiff_t>(std::floor(relativeY / height));
    const auto lastRow =
        static_cast<std::ptrdiff_t>(view.lines.rowCount(document)) - 1;
    const auto rowIndex =
        static_cast<std::size_t>(std::clamp(at, std::ptrdiff_t {0}, lastRow));

    // Row-local from here on. A click past the end of a wrapped row lands on the
    // row's end, which is the same offset the next row begins at — so the caret
    // is drawn at the start of the row below. That is the affinity question no
    // part of this models yet, and it is only visible at a wrap point.
    const auto row = view.lines.row(document, rowIndex);
    const auto text = row.textIn(document);
    const auto x = point.x - viewport.x - gutter - textPadding - scroll.x;

    // Nearest boundary rather than the one before, so clicking the right half
    // of a character puts the caret after it.
    auto best = std::size_t {0};
    auto bestDistance = std::abs(x);

    for (std::size_t index = 0; index <= text.size();)
    {
        const auto distance = std::abs(x - columnToX(text, index));

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = index;
        }

        if (index == text.size())
            break;

        auto next = index;
        Utf8::next(text, next);
        index = next;
    }

    return document.offsetAt(row.line, row.start + best);
}

// Paints a byte range as one band per visual row it covers.
//
// Per row rather than per line, because with wrap on a single logical line can
// be several bands with different left edges — and because a range that begins
// mid-row has to start where that row's text starts, not where its line does.
void TextRenderer::fillRange(Sprites::SpriteRenderer& sprites,
                             const DocumentView& view,
                             std::size_t rangeStart,
                             std::size_t rangeEnd,
                             const Graphics::Rect& textRect,
                             ScrollOffset scroll,
                             std::size_t first,
                             std::size_t last,
                             const Graphics::Color& color) const
{
    if (rangeEnd <= rangeStart)
        return;

    const auto& document = view.document;

    for (auto index = first; index < last; ++index)
    {
        const auto row = view.lines.row(document, index);

        const auto rowStart = document.offsetAt(row.line, row.start);
        const auto rowEnd = document.offsetAt(row.line, row.end);

        // Rows run in document order, so once one starts past the range there
        // is nothing further to draw.
        if (rangeEnd <= rowStart)
            break;

        if (rangeStart > rowEnd)
            continue;

        const auto text = row.textIn(document);

        const auto from = rangeStart > rowStart
                              ? std::min(rangeStart - rowStart, row.length())
                              : std::size_t {0};
        const auto to = rangeEnd < rowEnd ? rangeEnd - rowStart : row.length();

        // A range crossing a line end shows the newline as a sliver of trailing
        // width, so an empty selected line is still visible. Only at a real
        // newline: a wrap point has the next row immediately below it, and a
        // sliver there would draw a notch into the middle of a paragraph.
        const auto endsTheLine = row.end >= document.line(row.line).size();
        const auto spillsOver = rangeEnd > rowEnd && endsTheLine;

        if (to <= from && !spillsOver)
            continue;

        const auto left = columnToX(text, from);
        const auto right = columnToX(text, to) + (spillsOver ? advance * 0.5f : 0.f);

        sprites.fillRect({textRect.x + textPadding + scroll.x + left,
                          textRect.y + scroll.y + rowTop(index),
                          std::max(right - left, 1.f),
                          height},
                         color);
    }
}

void TextRenderer::drawMatches(Sprites::SpriteRenderer& sprites,
                               const DocumentView& view,
                               const EditorOverlay& overlay,
                               const Graphics::Rect& textRect,
                               ScrollOffset scroll,
                               std::size_t first,
                               std::size_t last) const
{
    if (overlay.matches == nullptr || overlay.matches->empty())
        return;

    const auto& document = view.document;
    const auto& matches = *overlay.matches;

    // Bounded by the viewport rather than by the match count. That is the
    // property the rest of this class is built on — a 100 MB file costs what a
    // small one does — and searching for "e" in it would otherwise put tens of
    // thousands of skipped ranges back into every frame. The list is in document
    // order, so the visible run is contiguous and can be found rather than
    // filtered for.
    const auto top = view.lines.row(document, first);
    const auto windowStart = document.offsetAt(top.line, top.start);

    const auto windowEnd = [&]
    {
        if (last >= view.lines.rowCount(document))
            return document.length();

        const auto bottom = view.lines.row(document, last);

        return document.offsetAt(bottom.line, bottom.start);
    }();

    auto visible = std::lower_bound(matches.begin(),
                                    matches.end(),
                                    windowStart,
                                    [](const SearchMatch& match, std::size_t offset)
                                    { return match.start < offset; });

    // One step back, in case a match begins above the window and reaches into
    // it. Only reachable for a query containing a newline, which the find field
    // cannot produce today — but fillRange already handles multi-line ranges and
    // relying on the field's key handling to keep this correct would be a
    // coupling nobody would think to look for.
    if (visible != matches.begin())
        --visible;

    for (auto match = visible; match != matches.end() && match->start < windowEnd;
         ++match)
    {
        const auto index = static_cast<int>(std::distance(matches.begin(), match));

        fillRange(sprites,
                  view,
                  match->start,
                  match->end,
                  textRect,
                  scroll,
                  first,
                  last,
                  index == overlay.currentMatch ? theme.currentSearchMatch
                                                : theme.searchMatch);
    }
}

void TextRenderer::draw(PaintContext& context,
                        const DocumentView& view,
                        const EditorOverlay& overlay,
                        const Graphics::Rect& viewport,
                        ScrollOffset scroll)
{
    const auto& document = view.document;
    const auto* cursors = overlay.cursors;

    // The document is drawn through this renderer's own atlas rather than the
    // chrome's, which is what lets the two be different sizes. Held open across
    // the whole of the draw, so the caret flush in the middle of it lands on the
    // right texture too.
    const auto glyphSource = AtlasScope {context, atlas};

    const auto first = firstVisibleRow(scroll.y);
    const auto last = lastVisibleRow(view, viewport, scroll.y);
    const auto gutter = gutterWidth(document.lineCount());

    // Again here rather than trusting prepare(): a caller may draw without
    // preparing, and every row this loop lays out has to land in a window that
    // holds it. Prepared or not, the overlapping rows keep their layout.
    cache.revalidate(stampFor(view));
    cache.setWindow(first, last);

    auto& glyphs = context.glyphs();

    // Line numbers are clipped to the gutter and the text to what remains, so
    // neither can spill into the other however long a line is.
    const auto gutterRect =
        Graphics::Rect {viewport.x, viewport.y, gutter, viewport.h};
    const auto textRect = Graphics::Rect {
        viewport.x + gutter, viewport.y, viewport.w - gutter, viewport.h};

    // Selection, search hits and the current-line band go behind the text, so
    // they are drawn through the sprite renderer before any glyph is queued.
    {
        const auto clip = ClipScope {context, textRect};

        if (cursors != nullptr)
        {
            // The band is drawn once per *line*, not once per caret. Two
            // carets on one line would otherwise paint it twice, and at 3.5%
            // white the doubled alpha reads as a different colour rather than
            // as a mistake. Cursors arrive in document order, so two on one
            // line arrive consecutively and remembering the last line filled
            // is the whole of the deduplication.
            auto filled = std::numeric_limits<std::size_t>::max();

            for (const auto& caret: *cursors)
            {
                if (caret.hasSelection())
                    continue;

                const auto caretLine = document.lineAt(caret.head);

                if (caretLine == filled)
                    continue;

                filled = caretLine;

                // The whole of the caret's logical line, however many rows it
                // takes. Lighting only the row the caret is on would break a
                // wrapped paragraph into a lit strip and an unlit one, which
                // reads as two different lines rather than one.
                const auto from = view.lines.firstRowOfLine(caretLine);
                const auto to = from + view.lines.rowsInLine(caretLine);

                // Edge to edge whatever the horizontal offset is: the band
                // says which line the caret is on, and one that slid out of
                // view with the text would stop saying it.
                for (auto row = std::max(first, from); row < std::min(last, to);
                     ++row)
                    context.sprites().fillRect({textRect.x,
                                                textRect.y + scroll.y + rowTop(row),
                                                textRect.w,
                                                height},
                                               theme.currentLine);
            }

            // Non-overlapping by CursorSet's invariant, so these cannot double
            // up the way the band above can.
            for (const auto& caret: *cursors)
                if (caret.hasSelection())
                    fillRange(context.sprites(),
                              view,
                              caret.start(),
                              caret.end(),
                              textRect,
                              scroll,
                              first,
                              last,
                              theme.selection);
        }

        // Over the selection, not under it. Finding a hit *selects* it, so the
        // two always coincide on the current match — and drawn underneath, the
        // current-match colour is covered by the selection every single time it
        // matters, leaving the hit being looked at painted the same blue as any
        // other selection. The whole point of a separate colour is lost.
        //
        // Painting on top means the current hit reads as a hit while the search
        // is live, and reverts to an ordinary selection the moment the bar
        // closes and the highlighting goes away.
        //
        // Found by running it. Every test passed with the order the wrong way
        // round, because none of them rendered a selection and a hit at once —
        // which is the only arrangement in which the bug exists.
        drawMatches(context.sprites(), view, overlay, textRect, scroll, first, last);
    }

    // Each region draws under its own clip. The context flushes the glyph batch
    // whenever the clip changes, which is what keeps the gutter's numbers from
    // being cut at the text's edge instead of their own.
    {
        const auto clip = ClipScope {context, gutterRect};

        for (auto index = first; index < last; ++index)
        {
            const auto& row = rowLayout(view, index);

            if (row.number.empty())
                continue;

            // Right-aligned against the gutter's inner edge. Not offset
            // horizontally: the numbers name the rows, so they stay put while
            // the text slides under them, which is what every editor does.
            const auto x = viewport.x + gutter - gutterPadding - row.numberWidth;

            submitLine(glyphs,
                       row.number,
                       x,
                       viewport.y + scroll.y + rowTop(index) + ascent);
        }
    }

    {
        const auto clip = ClipScope {context, textRect};

        for (auto index = first; index < last; ++index)
            submitLine(glyphs,
                       rowLayout(view, index).text,
                       textRect.x + textPadding + scroll.x,
                       viewport.y + scroll.y + rowTop(index) + ascent);

        // The batch has to reach the GPU before the caret is drawn over it,
        // rather than at the end of the scope. context.sprites() rebinds after
        // the flush on its own.
        context.flushGlyphs();

        // The carets go on top of the text: at a line's end one would otherwise
        // sit under the glyph that follows it after an edit.
        //
        // Every cursor draws the same caret, the primary included. Marking it
        // out would say which one a following ⌘F searches from, and cost the
        // much more useful reading that all of them are equally live — a
        // multi-cursor edit happens at all of them at once.
        if (cursors != nullptr && overlay.caretVisible)
        {
            for (const auto& caret: *cursors)
            {
                const auto index = view.lines.rowOfOffset(document, caret.head);

                if (index < first || index >= last)
                    continue;

                context.sprites().fillRect(
                    {textRect.x + scroll.x + caretX(view, caret.head),
                     textRect.y + scroll.y + rowTop(index),
                     caretWidth,
                     height},
                    theme.caret);
            }
        }
    }

    context.sprites().fillRect({viewport.x + gutter, viewport.y, 1.f, viewport.h},
                               theme.gutterEdge);
}
} // namespace ecode
