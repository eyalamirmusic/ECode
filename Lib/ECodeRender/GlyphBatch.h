#pragma once

#include <eacp/GPU/GPU.h>
#include <eacp/Text/Text.h>

#include <vector>

namespace ecode
{
// A unit-quad corner, each component 0 or 1, mapped onto each glyph's rect.
struct GlyphCorner
{
    float corner[2];
};

// One glyph to draw. Everything varying per glyph lives here so a whole screen
// of text is a single draw call.
struct GlyphInstance
{
    // Destination rect in logical points: x, y, width, height.
    float rect[4];

    // Source rect in atlas texels: x, y, width, height.
    float source[4];

    float color[4];
};

// Draws glyphs from a coverage atlas, batched and instanced.
//
// Two reasons this exists rather than reusing eacp's SpriteRenderer:
//
//  1. **Correctness.** An R8Unorm texture samples as (r, 0, 0, 1) — coverage in
//     red, alpha pinned to 1. SpriteRenderer multiplies the sample by its tint,
//     which turns a mask into opaque red rather than tinted text. A glyph
//     shader has to read coverage from .r and put it in the *alpha* channel:
//     `float4(colour.rgb, colour.a * coverage)`.
//
//  2. **Cost.** SpriteRenderer issues one draw call per quad, with a fresh
//     uniform upload and texture bind each time. A screenful of code is
//     thousands of glyphs; here it is one drawInstanced.
class GlyphBatch
{
public:
    GlyphBatch();

    // Logical size of the surface being drawn into, for the pixel-to-clip
    // mapping. Cheap to call every frame — unlike SpriteRenderer, whose logical
    // size is baked at construction and forces a rebuild on every resize.
    void setViewportSize(eacp::Graphics::Point size);

    void begin();

    // Queues one glyph. Mask and colour glyphs go to separate queues because
    // they sample different textures and shade differently: a mask is tinted,
    // a colour glyph carries its own colour and is drawn as-is.
    void add(const eacp::Graphics::Rect& destination,
             const eacp::Graphics::Rect& source,
             const eacp::Graphics::Color& color,
             bool colored);

    // Submits the queued glyphs: at most two draw calls, one per atlas.
    void flush(eacp::GPU::RenderPass& pass, eacp::Text::GlyphAtlas& atlas);

    std::size_t queuedGlyphs() const { return masks.size() + colors.size(); }

private:
    struct Program;

    void drawQueue(eacp::GPU::RenderPass& pass,
                   std::vector<GlyphInstance>& queue,
                   eacp::GPU::Texture& texture,
                   bool colored);

    eacp::OwningPointer<Program> maskProgram;
    eacp::OwningPointer<Program> colorProgram;

    std::vector<GlyphInstance> masks;
    std::vector<GlyphInstance> colors;

    eacp::Graphics::Point viewport {1.f, 1.f};
    bool prepared = false;
};
} // namespace ecode
