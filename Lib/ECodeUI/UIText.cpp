#include "UIText.h"

#include <ECodeCore/Utf8.h>

namespace ecode::UIText
{
using namespace eacp;

void prepare(Text::GlyphAtlas& atlas, std::string_view text)
{
    for (std::size_t index = 0; index < text.size();)
        atlas.glyph(Utf8::next(text, index), Text::FontStyle::Regular);
}

float width(Text::GlyphAtlas& atlas, std::string_view text)
{
    auto total = 0.f;

    // Sums real advances rather than multiplying by a cell width: the chrome
    // font is the same monospace face as the editor's today, but a tab title
    // measured by character count would be wrong the moment it is not.
    for (std::size_t index = 0; index < text.size();)
        total += atlas.glyph(Utf8::next(text, index), Text::FontStyle::Regular)
                     .advance;

    return total;
}

float draw(PaintContext& context,
           std::string_view text,
           float x,
           float baseline,
           const Graphics::Color& color)
{
    auto& atlas = context.atlas();
    const auto scale = context.backingScale();

    auto pen = x;

    for (std::size_t index = 0; index < text.size();)
    {
        const auto codepoint = Utf8::next(text, index);
        const auto glyph = atlas.glyph(codepoint, Text::FontStyle::Regular);

        if (!glyph.valid)
            continue;

        if (!glyph.empty)
        {
            // The atlas rect is in device pixels; the destination is in points.
            const auto destination = Graphics::Rect {pen + glyph.offset.x,
                                                     baseline + glyph.offset.y,
                                                     glyph.src.w / scale,
                                                     glyph.src.h / scale};

            context.glyphs().add(destination,
                                 glyph.src,
                                 color,
                                 glyph.format == Text::GlyphFormat::Color);
        }

        pen += glyph.advance;
    }

    return pen;
}

float centredBaseline(Text::GlyphAtlas& atlas, const Graphics::Rect& area)
{
    const auto metrics = atlas.metrics();
    const auto textHeight = metrics.ascent + metrics.descent;

    return area.y + (area.h - textHeight) * 0.5f + metrics.ascent;
}
} // namespace ecode::UIText
