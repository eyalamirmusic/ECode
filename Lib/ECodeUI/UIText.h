#pragma once

#include <ECodeRender/PaintContext.h>

#include <string_view>

namespace ecode::UIText
{
// Drawing and measuring a plain run of chrome text — a filename in a tab, a
// caret position in the status bar.
//
// Deliberately not TextRenderer: that one exists to draw a *document*, so it
// carries tab stops, style spans, a gutter and a viewport, none of which mean
// anything for a label. What the two do share is the UTF-8 walk and the
// per-glyph placement, and that lives in ecode::Utf8 rather than being copied.

// Rasterizes what draw() will need. Chrome text is under the same prepass rule
// as the editor's: every glyph into the atlas, then GlyphAtlas::commit(), then
// draw. A glyph first touched during the paint walk would upload into a texture
// the pass has already bound.
void prepare(eacp::Text::GlyphAtlas& atlas, std::string_view text);

// Advance width of the run in points. Rasterizes on the way, so it is subject
// to the same rule as prepare().
float width(eacp::Text::GlyphAtlas& atlas, std::string_view text);

// Draws the run and returns the pen x after it.
float draw(PaintContext& context,
           std::string_view text,
           float x,
           float baseline,
           const eacp::Graphics::Color& color);

// Baseline that centres one line of text vertically within `area`. Chrome rows
// are laid out by height and the text hung inside them, so this is what every
// label needs and nothing else has to know the metrics.
float centredBaseline(eacp::Text::GlyphAtlas& atlas,
                      const eacp::Graphics::Rect& area);
} // namespace ecode::UIText
