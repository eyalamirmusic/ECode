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
    , glyphAtlas(atlasToUse)
    , currentClip(surface)
    , scale(backingScaleToUse)
{
    glyphRenderer.begin();
    setClip(surface);
}

PaintContext::~PaintContext()
{
    flushGlyphs();
}

Sprites::SpriteRenderer& PaintContext::sprites()
{
    if (spritesNeedRebind)
    {
        spriteRenderer.begin(renderPass);
        spritesNeedRebind = false;
    }

    return spriteRenderer;
}

void PaintContext::flushGlyphs()
{
    // flush() clears the queues itself, so drawing can continue straight after
    // one. Skipping the empty case keeps a deep tree of chrome widgets from
    // issuing a pipeline bind per widget just to draw nothing.
    if (glyphRenderer.queuedGlyphs() == 0)
        return;

    glyphRenderer.flush(renderPass, glyphAtlas);

    // The flush left the glyph pipeline bound; the next sprite draw rebinds.
    spritesNeedRebind = true;
}

void PaintContext::setClip(const Graphics::Rect& area)
{
    if (area.x == currentClip.x && area.y == currentClip.y
        && area.w == currentClip.w && area.h == currentClip.h)
        return;

    // Anything already queued belongs to the clip it was queued under.
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
} // namespace ecode
