#include "TextRenderer.h"

#include <ECodeCore/Utf8.h>

#include <algorithm>
#include <cmath>

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

std::string lineNumberText(std::size_t line)
{
    return std::to_string(line + 1);
}
} // namespace

const Graphics::Color& TextTheme::colorFor(TokenKind kind) const
{
    switch (kind)
    {
        case TokenKind::Keyword:
            return keyword;
        case TokenKind::String:
            return string;
        case TokenKind::Comment:
            return comment;
        case TokenKind::Number:
            return number;
        case TokenKind::Function:
            return function;
        case TokenKind::Type:
            return type;
        case TokenKind::Constant:
            return constant;
        case TokenKind::Operator:
            return operatorColor;
        case TokenKind::Punctuation:
            return punctuation;
        case TokenKind::Preprocessor:
            return preprocessor;
        case TokenKind::Text:
            break;
    }

    return text;
}

TextRenderer::TextRenderer(Text::GlyphAtlas& atlasToUse, const TextTheme& themeToUse)
    : atlas(atlasToUse)
    , theme(themeToUse)
{
    const auto metrics = atlas.metrics();

    advance = metrics.advance;
    ascent = metrics.ascent;

    // Whole points per line: a fractional line height accumulates down the
    // viewport and leaves rows landing on different subpixel phases, which
    // reads as uneven spacing.
    height = std::max(std::round(metrics.lineHeight() * 1.25f), 1.f);
}

float TextRenderer::lineHeight() const
{
    return height;
}

float TextRenderer::gutterWidth(std::size_t lineCount) const
{
    const auto digits = lineNumberText(lineCount == 0 ? 0 : lineCount - 1).size();

    return static_cast<float>(digits) * advance + gutterPadding * 2.f;
}

float TextRenderer::contentHeight(const Document& document) const
{
    return static_cast<float>(document.lineCount()) * height;
}

std::size_t TextRenderer::firstVisibleLine(float scrollY) const
{
    if (scrollY >= 0.f || height <= 0.f)
        return 0;

    return static_cast<std::size_t>(-scrollY / height);
}

std::size_t TextRenderer::lastVisibleLine(const Document& document,
                                          const Graphics::Rect& viewport,
                                          float scrollY) const
{
    if (height <= 0.f)
        return 0;

    const auto rows = static_cast<std::size_t>(viewport.h / height) + 2;

    return std::min(firstVisibleLine(scrollY) + rows, document.lineCount());
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

void TextRenderer::prepare(const Document& document,
                           const Graphics::Rect& viewport,
                           float scrollY)
{
    const auto first = firstVisibleLine(scrollY);
    const auto last = lastVisibleLine(document, viewport, scrollY);

    for (auto line = first; line < last; ++line)
    {
        prepareLine(document.line(line));
        prepareLine(lineNumberText(line));
    }
}

void TextRenderer::drawLine(Text::GlyphRenderer& glyphs,
                            std::string_view text,
                            const LineStyle* spans,
                            float x,
                            float baseline,
                            const Graphics::Color& color,
                            float backingScale)
{
    auto pen = x;

    // Walks forward with the loop rather than searching per glyph; the spans and
    // the text are both traversed left to right exactly once.
    auto spanCursor = std::size_t {0};

    for (std::size_t index = 0; index < text.size();)
    {
        const auto glyphStart = index;
        const auto codepoint = Utf8::next(text, index);

        auto glyphColor = color;

        if (spans != nullptr)
            if (const auto* span = spanAt(*spans, glyphStart, spanCursor))
                glyphColor = theme.colorFor(span->kind);

        if (codepoint == U'\t')
        {
            // Advance to the next tab stop rather than by a fixed amount, so
            // indentation lines up the way the file's author saw it.
            const auto column = (pen - x) / advance;
            const auto next = std::floor(column / tabWidth + 1.f) * tabWidth;
            pen = x + next * advance;
            continue;
        }

        const auto glyph = atlas.glyph(codepoint, Text::FontStyle::Regular);

        if (!glyph.valid)
            continue;

        if (!glyph.empty)
        {
            const auto colored = glyph.format == Text::GlyphFormat::Color;

            // The atlas rect is in device pixels; the destination is in points.
            const auto destination = Graphics::Rect {pen + glyph.offset.x,
                                                     baseline + glyph.offset.y,
                                                     glyph.src.w / backingScale,
                                                     glyph.src.h / backingScale};

            glyphs.add(destination, glyph.src, glyphColor, colored);
        }

        pen += glyph.advance;
    }
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

std::size_t TextRenderer::offsetAtPoint(const Document& document,
                                        const Graphics::Point& point,
                                        const Graphics::Rect& viewport,
                                        float scrollY) const
{
    const auto gutter = gutterWidth(document.lineCount());
    const auto relativeY = point.y - viewport.y - scrollY;

    const auto row = static_cast<std::ptrdiff_t>(std::floor(relativeY / height));
    const auto lastLine = static_cast<std::ptrdiff_t>(document.lineCount()) - 1;
    const auto line =
        static_cast<std::size_t>(std::clamp(row, std::ptrdiff_t {0}, lastLine));

    const auto text = document.line(line);
    const auto x = point.x - viewport.x - gutter - textPadding;

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

    return document.offsetAt(line, best);
}

void TextRenderer::fillRange(Sprites::SpriteRenderer& sprites,
                             const Document& document,
                             std::size_t rangeStart,
                             std::size_t rangeEnd,
                             const Graphics::Rect& textRect,
                             float scrollY,
                             std::size_t first,
                             std::size_t last,
                             const Graphics::Color& color)
{
    if (rangeEnd <= rangeStart)
        return;

    const auto from = document.lineAt(rangeStart);
    const auto to = document.lineAt(rangeEnd);

    for (auto line = std::max(first, from); line < last && line <= to; ++line)
    {
        const auto text = document.line(line);
        const auto lineBegin = document.offsetAt(line, 0);

        const auto startColumn =
            line == from ? rangeStart - lineBegin : std::size_t {0};
        const auto endColumn = line == to ? rangeEnd - lineBegin : text.size();

        const auto left = columnToX(text, startColumn);

        // A range crossing a line end shows the newline as a sliver of trailing
        // width, so an empty selected line is still visible.
        const auto right = line == to
                               ? columnToX(text, endColumn)
                               : columnToX(text, text.size()) + advance * 0.5f;

        const auto y = textRect.y + scrollY + static_cast<float>(line) * height;

        sprites.fillRect({textRect.x + textPadding + left,
                          y,
                          std::max(right - left, 1.f),
                          height},
                         color);
    }
}

void TextRenderer::drawMatches(Sprites::SpriteRenderer& sprites,
                               const Document& document,
                               const EditorOverlay& overlay,
                               const Graphics::Rect& textRect,
                               float scrollY,
                               std::size_t first,
                               std::size_t last)
{
    if (overlay.matches == nullptr || overlay.matches->empty())
        return;

    const auto& matches = *overlay.matches;

    // Bounded by the viewport rather than by the match count. That is the
    // property the rest of this class is built on — a 100 MB file costs what a
    // small one does — and searching for "e" in it would otherwise put tens of
    // thousands of skipped ranges back into every frame. The list is in document
    // order, so the visible run is contiguous and can be found rather than
    // filtered for.
    const auto windowStart = document.offsetAt(first, 0);
    const auto windowEnd =
        last < document.lineCount() ? document.offsetAt(last, 0) : document.length();

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
                  document,
                  match->start,
                  match->end,
                  textRect,
                  scrollY,
                  first,
                  last,
                  index == overlay.currentMatch ? theme.currentSearchMatch
                                                : theme.searchMatch);
    }
}

void TextRenderer::draw(PaintContext& context,
                        const Document& document,
                        const EditorOverlay& overlay,
                        Highlighter* highlighter,
                        const Graphics::Rect& viewport,
                        float scrollY)
{
    const auto* cursor = overlay.cursor;

    const auto first = firstVisibleLine(scrollY);
    const auto last = lastVisibleLine(document, viewport, scrollY);
    const auto gutter = gutterWidth(document.lineCount());

    auto& glyphs = context.glyphs();
    const auto backingScale = context.backingScale();

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

        if (cursor != nullptr)
        {
            const auto caretLine = document.lineAt(cursor->head);

            if (!cursor->hasSelection() && caretLine >= first && caretLine < last)
            {
                const auto y =
                    textRect.y + scrollY + static_cast<float>(caretLine) * height;

                context.sprites().fillRect({textRect.x, y, textRect.w, height},
                                           theme.currentLine);
            }
        }

        if (cursor != nullptr && cursor->hasSelection())
            fillRange(context.sprites(),
                      document,
                      cursor->start(),
                      cursor->end(),
                      textRect,
                      scrollY,
                      first,
                      last,
                      theme.selection);

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
        drawMatches(
            context.sprites(), document, overlay, textRect, scrollY, first, last);
    }

    // Each region draws under its own clip. The context flushes the glyph batch
    // whenever the clip changes, which is what keeps the gutter's numbers from
    // being cut at the text's edge instead of their own.
    {
        const auto clip = ClipScope {context, gutterRect};

        for (auto line = first; line < last; ++line)
        {
            const auto y = viewport.y + scrollY + static_cast<float>(line) * height;
            const auto number = lineNumberText(line);

            // Right-aligned against the gutter's inner edge.
            const auto width = static_cast<float>(number.size()) * advance;
            const auto x = viewport.x + gutter - gutterPadding - width;

            drawLine(glyphs,
                     number,
                     nullptr,
                     x,
                     y + ascent,
                     theme.lineNumber,
                     backingScale);
        }
    }

    {
        const auto clip = ClipScope {context, textRect};

        for (auto line = first; line < last; ++line)
        {
            const auto y = viewport.y + scrollY + static_cast<float>(line) * height;

            const auto* spans =
                highlighter != nullptr ? &highlighter->lineStyle(line) : nullptr;

            drawLine(glyphs,
                     document.line(line),
                     spans,
                     textRect.x + textPadding,
                     y + ascent,
                     theme.text,
                     backingScale);
        }

        // The batch has to reach the GPU before the caret is drawn over it,
        // rather than at the end of the scope. context.sprites() rebinds after
        // the flush on its own.
        context.flushGlyphs();

        // The caret goes on top of the text: at a line's end it would otherwise
        // sit under the glyph that follows it after an edit.
        if (cursor != nullptr && overlay.caretVisible)
        {
            const auto caretLine = document.lineAt(cursor->head);

            if (caretLine >= first && caretLine < last)
            {
                const auto column = cursor->head - document.offsetAt(caretLine, 0);
                const auto x = columnToX(document.line(caretLine), column);
                const auto y =
                    textRect.y + scrollY + static_cast<float>(caretLine) * height;

                context.sprites().fillRect(
                    {textRect.x + textPadding + x, y, 2.f, height}, theme.caret);
            }
        }
    }

    context.sprites().fillRect({viewport.x + gutter, viewport.y, 1.f, viewport.h},
                               theme.gutterEdge);
}
} // namespace ecode
