#pragma once

#include "GlyphBatch.h"

#include <ECodeCore/Document.h>

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
};

// Draws the visible slice of a Document through a glyph atlas.
//
// Only the lines actually on screen are touched — the loop is bounded by the
// viewport, not by the document — so scrolling a 100 MB file costs the same as
// scrolling a small one. That is the property the whole viewer milestone rests
// on, and it is easy to lose by iterating the document and clipping late.
class TextRenderer
{
public:
    TextRenderer(eacp::Text::GlyphAtlas& atlasToUse, const TextTheme& themeToUse);

    // Lays out and draws the lines visible in `viewport` at the given scroll
    // offset. `pass` is needed to clip the text to the viewport, so a long line
    // stops at the edge instead of running under the chrome.
    //
    // Every glyph the frame needs must already be in the atlas: call
    // prepare() first, then GlyphAtlas::commit(), then this. Uploading in the
    // middle of a pass would mutate a texture the earlier draws have bound.
    void draw(eacp::GPU::RenderPass& pass,
              eacp::Sprites::SpriteRenderer& sprites,
              GlyphBatch& batch,
              const Document& document,
              const eacp::Graphics::Rect& viewport,
              float scrollY,
              float backingScale);

    // Rasterizes the glyphs the next draw() will need, without drawing.
    void prepare(const Document& document,
                 const eacp::Graphics::Rect& viewport,
                 float scrollY);

    float lineHeight() const;

    // Width of the line-number gutter for a document of this many lines.
    float gutterWidth(std::size_t lineCount) const;

    // First and last document line touching the viewport at this scroll offset.
    std::size_t firstVisibleLine(float scrollY) const;
    std::size_t lastVisibleLine(const Document& document,
                                const eacp::Graphics::Rect& viewport,
                                float scrollY) const;

    // Total height of the document, for the scroll range.
    float contentHeight(const Document& document) const;

private:
    void drawLine(GlyphBatch& batch,
                  std::string_view text,
                  float x,
                  float baseline,
                  const eacp::Graphics::Color& color,
                  float backingScale);

    void prepareLine(std::string_view text);

    eacp::Text::GlyphAtlas& atlas;
    TextTheme theme;

    float advance = 0.f;
    float ascent = 0.f;
    float height = 0.f;
};
} // namespace ecode
