#include "PaintContext.h"

namespace ecode
{
using namespace eacp;

PaintContext::PaintContext(GPU::RenderPass& passToUse,
                           Sprites::SpriteRenderer& spritesToUse,
                           Text::GlyphRenderer& glyphsToUse,
                           Text::GlyphAtlas& atlasToUse,
                           const Graphics::Rect& surface,
                           float backingScaleToUse)
    : renderPass(passToUse)
    , spriteRenderer(spritesToUse)
    , glyphRenderer(glyphsToUse)
    , glyphAtlas(&atlasToUse)
    , currentClip(surface)
    , scale(backingScaleToUse)
{
    glyphRenderer.begin();

    // Once per frame, here, and never again.
    //
    // Not lazily on the first fillRect, which is what this used to do.
    // SpriteRenderer::begin clears the queue as part of joining the pass, so
    // calling it a second time mid-frame silently discards every rectangle
    // issued since the first. It was called again after every glyph flush, on
    // the belief that the sprite pipeline needed rebinding — it does not:
    // SpriteRenderer::flush draws through its own ShaderProgram, which binds
    // what it needs at the point of drawing, so nothing the glyph pipeline does
    // to the pass can outlast it.
    spriteRenderer.begin(renderPass);

    setClip(surface);
}

PaintContext::~PaintContext()
{
    // Rectangles first, then text, which is the order they were issued in
    // wherever both are still pending — a widget draws its background and then
    // its label. Anything issued the other way round has already been drained by
    // the accessors below.
    flushSprites();
    flushGlyphs();
}

Sprites::SpriteRenderer& PaintContext::sprites()
{
    flushGlyphs();

    return spriteRenderer;
}

Text::GlyphRenderer& PaintContext::glyphs()
{
    flushSprites();

    return glyphRenderer;
}

void PaintContext::flushGlyphs()
{
    // flush() clears the queues itself, so drawing can continue straight after
    // one. Skipping the empty case keeps a deep tree of chrome widgets from
    // issuing a pipeline bind per widget just to draw nothing.
    if (glyphRenderer.queuedGlyphs() == 0)
        return;

    glyphRenderer.flush(renderPass, *glyphAtlas);
}

void PaintContext::flushSprites()
{
    // Unconditional, unlike the glyph side: SpriteRenderer::flush returns on an
    // empty queue by itself, and there is no cheaper question to ask from here.
    spriteRenderer.flush();
}

void PaintContext::setAtlas(Text::GlyphAtlas& atlasToDrawFrom)
{
    if (&atlasToDrawFrom == glyphAtlas)
        return;

    // Anything already queued names texels in the atlas it was queued against.
    flushGlyphs();

    glyphAtlas = &atlasToDrawFrom;
}

void PaintContext::setClip(const Graphics::Rect& area)
{
    if (area.x == currentClip.x && area.y == currentClip.y
        && area.w == currentClip.w && area.h == currentClip.h)
        return;

    // Anything already queued belongs to the clip it was queued under, and that
    // is as true of rectangles as of text.
    //
    // The scissor is pass state rather than anything either renderer holds, so a
    // quad still sitting in a batch when this moves is drawn under the *new*
    // rect — clipped by a widget it was never inside, or scissored away and lost
    // outright. eacp says as much in SpriteRenderer's header: a caller that sets
    // the scissor by hand has to say when.
    flushSprites();
    flushGlyphs();

    currentClip = area;

    // Scissor rects are in render-target pixels; everything above this line is
    // in points. RenderPass clamps an oversized rect and discards on an empty
    // one, so an off-screen widget costs a state change and no fragments.
    renderPass.setScissorRect(
        {area.x * scale, area.y * scale, area.w * scale, area.h * scale});
}

ClipScope::ClipScope(PaintContext& contextToUse, const Graphics::Rect& area)
    : context(contextToUse)
    , previous(contextToUse.clip())
{
    const auto narrowed = previous.intersection(area);

    empty = narrowed.isEmpty();

    context.setClip(narrowed);
}

ClipScope::~ClipScope()
{
    context.setClip(previous);
}

AtlasScope::AtlasScope(PaintContext& contextToUse, Text::GlyphAtlas& atlas)
    : context(contextToUse)
    , previous(contextToUse.atlas())
{
    context.setAtlas(atlas);
}

AtlasScope::~AtlasScope()
{
    context.setAtlas(previous);
}
} // namespace ecode
