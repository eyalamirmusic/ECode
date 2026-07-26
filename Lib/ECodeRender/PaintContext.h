#pragma once

#include <eacp/GPU/GPU.h>
#include <eacp/Sprites/Sprites.h>
#include <eacp/Text/Text.h>

namespace ecode
{
// Everything a widget needs in order to draw, plus the clip stack the GPU does
// not have.
//
// The clip and the glyph batch are owned by the same object because they are
// coupled, and the coupling is silent if they are not. GlyphRenderer batches
// between begin() and flush(), while a scissor rect is pass state read when a
// draw is finally issued — so glyphs queued under one clip and flushed under
// the next are clipped by the *next* one, and the symptom is text belonging to
// one widget being cut at another widget's edge. Every clip change here flushes
// the batch first, which is the whole reason this is a class and not a struct
// of references.
class PaintContext
{
public:
    // `surface` is the whole drawable in points, and the clip every widget
    // starts out narrowing.
    PaintContext(eacp::GPU::RenderPass& passToUse,
                 eacp::Sprites::SpriteRenderer& spritesToUse,
                 eacp::Text::GlyphRenderer& glyphsToUse,
                 eacp::Text::GlyphAtlas& atlasToUse,
                 const eacp::Graphics::Rect& surface,
                 float backingScaleToUse);

    // Flushes whatever is still queued. A widget tree that ends its paint walk
    // mid-batch would otherwise drop its last glyphs on the floor.
    ~PaintContext();

    PaintContext(const PaintContext&) = delete;
    PaintContext& operator=(const PaintContext&) = delete;

    eacp::GPU::RenderPass& pass() const { return renderPass; }
    eacp::Text::GlyphRenderer& glyphs() const { return glyphRenderer; }

    // The atlas glyphs are being drawn from right now, which is the chrome's
    // unless an AtlasScope is open.
    eacp::Text::GlyphAtlas& atlas() const { return *glyphAtlas; }

    // Rebinds the sprite pipeline if a glyph flush has clobbered it since the
    // last call, which is the second half of the same coupling: flushing text
    // leaves the glyph pipeline bound, so the next fillRect would otherwise
    // draw through the glyph shader. Fetch it at the point of use rather than
    // caching the reference — a cached one goes stale across a clip change.
    eacp::Sprites::SpriteRenderer& sprites();

    // Pixels per point. Destinations are in points and scissor rects are in
    // pixels, so this is the one conversion the layer owns rather than leaving
    // every widget to repeat it.
    float backingScale() const { return scale; }

    const eacp::Graphics::Rect& clip() const { return currentClip; }

    // Submits the queued glyphs under the clip they were queued with. Called
    // for you on every clip change and at the end of the frame; call it by
    // hand only when issuing a draw that must land after pending text.
    void flushGlyphs();

private:
    friend class ClipScope;
    friend class AtlasScope;

    void setClip(const eacp::Graphics::Rect& area);
    void setAtlas(eacp::Text::GlyphAtlas& atlasToDrawFrom);

    eacp::GPU::RenderPass& renderPass;
    eacp::Sprites::SpriteRenderer& spriteRenderer;
    eacp::Text::GlyphRenderer& glyphRenderer;

    // Never null: a context is constructed with one and an AtlasScope only ever
    // swaps it for another.
    eacp::Text::GlyphAtlas* glyphAtlas;

    eacp::Graphics::Rect currentClip;
    float scale = 1.f;

    // Starts true so the first sprite draw of the frame binds, which is why no
    // caller has to call SpriteRenderer::begin itself.
    bool spritesNeedRebind = true;
};

// Narrows the clip for the lifetime of the scope and restores it after.
//
// Intersects rather than replaces: the GPU has exactly one scissor rect and no
// stack, so a child's clip has to be combined with its parent's on the way down
// or a child would be able to draw outside the widget containing it.
class ClipScope
{
public:
    ClipScope(PaintContext& contextToUse, const eacp::Graphics::Rect& area);
    ~ClipScope();

    ClipScope(const ClipScope&) = delete;
    ClipScope& operator=(const ClipScope&) = delete;

    // True when the intersection came out empty — the widget is scrolled or
    // laid out entirely off its parent, so there is nothing to draw and the
    // caller can return without walking its children.
    bool isEmpty() const { return empty; }

private:
    PaintContext& context;
    eacp::Graphics::Rect previous;
    bool empty = false;
};

// Draws glyphs from a different atlas for the lifetime of the scope.
//
// The same coupling ClipScope exists for, one step further down. GlyphRenderer
// batches until a flush, and the flush is what names the texture to sample from
// — so glyphs queued against one atlas and flushed against another are drawn
// from whatever texels happen to sit at those coordinates in the other, which
// is a screenful of the wrong letters rather than anything that looks like a
// bug in the switching. Every change flushes first.
//
// There are two atlases because the editor's font size is settable and the
// chrome's is not: ⌘+ makes the code bigger and leaves the tabs, the tree and
// the status bar where they are. A GlyphAtlas is one face at one size by
// construction, so two sizes is two atlases.
class AtlasScope
{
public:
    AtlasScope(PaintContext& contextToUse, eacp::Text::GlyphAtlas& atlas);
    ~AtlasScope();

    AtlasScope(const AtlasScope&) = delete;
    AtlasScope& operator=(const AtlasScope&) = delete;

private:
    PaintContext& context;
    eacp::Text::GlyphAtlas& previous;
};
} // namespace ecode
