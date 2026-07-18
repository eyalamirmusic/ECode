#include "TextRenderer.h"

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

// Minimal UTF-8 decode. eacp::Strings has no codepoint iteration, so this lives
// here until it does; multi-byte sequences are rare enough in source that the
// ASCII path is what matters for speed.
char32_t nextCodepoint(std::string_view text, std::size_t& index)
{
    const auto lead = static_cast<unsigned char>(text[index]);

    if (lead < 0x80)
        return static_cast<char32_t>(text[index++]);

    auto extra = 0;
    auto value = char32_t {0};

    if ((lead & 0xe0) == 0xc0)
    {
        extra = 1;
        value = lead & 0x1fu;
    }
    else if ((lead & 0xf0) == 0xe0)
    {
        extra = 2;
        value = lead & 0x0fu;
    }
    else
    {
        extra = 3;
        value = lead & 0x07u;
    }

    ++index;

    for (auto i = 0; i < extra && index < text.size(); ++i, ++index)
        value = (value << 6) | (static_cast<unsigned char>(text[index]) & 0x3fu);

    return value;
}

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
        const auto codepoint = nextCodepoint(text, index);

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
        const auto codepoint = nextCodepoint(text, index);

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

void TextRenderer::draw(GPU::RenderPass& pass,
                        Sprites::SpriteRenderer& sprites,
                        Text::GlyphRenderer& glyphs,
                        const Document& document,
                        Highlighter* highlighter,
                        const Graphics::Rect& viewport,
                        float scrollY,
                        float backingScale)
{
    const auto first = firstVisibleLine(scrollY);
    const auto last = lastVisibleLine(document, viewport, scrollY);
    const auto gutter = gutterWidth(document.lineCount());

    const auto toPixels = [backingScale](const Graphics::Rect& rect)
    {
        return Graphics::Rect {rect.x * backingScale,
                               rect.y * backingScale,
                               rect.w * backingScale,
                               rect.h * backingScale};
    };

    // Line numbers are clipped to the gutter and the text to what remains, so
    // neither can spill into the other however long a line is.
    const auto gutterRect =
        Graphics::Rect {viewport.x, viewport.y, gutter, viewport.h};
    const auto textRect = Graphics::Rect {
        viewport.x + gutter, viewport.y, viewport.w - gutter, viewport.h};

    // Scissor is pass state applied when a draw is issued, so each region has to
    // be flushed under its own rect rather than everything being queued and
    // submitted once at the end.
    pass.setScissorRect(toPixels(gutterRect));
    glyphs.begin();

    for (auto line = first; line < last; ++line)
    {
        const auto y = viewport.y + scrollY + static_cast<float>(line) * height;
        const auto number = lineNumberText(line);

        // Right-aligned against the gutter's inner edge.
        const auto width = static_cast<float>(number.size()) * advance;
        const auto x = viewport.x + gutter - gutterPadding - width;

        drawLine(
            glyphs, number, nullptr, x, y + ascent, theme.lineNumber, backingScale);
    }

    glyphs.flush(pass, atlas);

    pass.setScissorRect(toPixels(textRect));
    glyphs.begin();

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

    glyphs.flush(pass, atlas);
    pass.clearScissorRect();

    // The batch left its own pipeline bound, so the sprite renderer has to
    // rebind before drawing anything else.
    sprites.begin(pass);
    sprites.fillRect({viewport.x + gutter, viewport.y, 1.f, viewport.h},
                     theme.gutterEdge);
}
} // namespace ecode
