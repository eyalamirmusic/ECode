#pragma once

#include "GlyphBatch.h"

#include <ECodeCore/Document.h>
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
    // highlighter may be null, in which case everything draws as plain text.
    void draw(eacp::GPU::RenderPass& pass,
              eacp::Sprites::SpriteRenderer& sprites,
              GlyphBatch& batch,
              const Document& document,
              Highlighter* highlighter,
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
    // spans may be null for uniformly coloured text (the line-number gutter).
    void drawLine(GlyphBatch& batch,
                  std::string_view text,
                  const LineStyle* spans,
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
