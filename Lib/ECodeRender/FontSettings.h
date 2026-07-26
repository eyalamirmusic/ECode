#pragma once

#include <eacp/Text/Text.h>

#include <string>

namespace ecode
{
// The font a document is drawn in, and the only place its size is decided.
//
// The size is two numbers rather than one. `pointSize` is what was configured
// and `zoom` is how far ⌘+ has moved from it, so Reset means "back to the
// configured size" rather than "back to 13" — a difference that shows up only
// once something sets the size to anything else, which is exactly when nobody
// is looking for it. Until PLAN.md §5's config file exists, this struct is the
// configuration.
//
// The chrome has its own instance of this at a fixed size: ⌘+ makes the code
// bigger and leaves the tab strip and the status bar alone. See AtlasScope for
// what that costs at the point of drawing.
struct FontSettings
{
    std::string family = eacp::Text::defaultMonospaceFamily();

    float pointSize = 13.f;

    // In points, added to pointSize.
    float zoom = 0.f;

    // Below the floor the caret is wider than a column; above the ceiling a
    // screenful is a paragraph. Both sit far outside any size anyone would
    // choose — the range is here so that holding ⌘+ stops somewhere rather than
    // rasterizing glyphs the size of the window.
    static constexpr auto minimumSize = 6.f;
    static constexpr auto maximumSize = 48.f;

    // A whole point per press, because the atlas rasterizes onto a pixel grid:
    // a fractional step would leave a press or two doing nothing visible.
    static constexpr auto zoomStep = 1.f;

    // What to rasterize at.
    float size() const;

    // Whether a step in this direction would change anything, so a command can
    // grey out at the ends of the range rather than looking broken.
    bool canZoom(int steps) const;

    // Clamps the resulting *size*, not the accumulated zoom: a zoom allowed to
    // run past the ceiling would need as many presses of ⌘- before the text
    // started coming back.
    void zoomBy(int steps);

    void resetZoom() { zoom = 0.f; }

    // Whole-struct equality, which is what "does the atlas still answer for
    // this?" needs — the family and the size an atlas was rasterized for are
    // only ever correct together.
    bool operator==(const FontSettings& other) const;
    bool operator!=(const FontSettings& other) const { return !(*this == other); }
};

// The atlas to draw this font through, or null when the family resolved to
// nothing at all.
//
// `scale` is the display's backing scale: the rasterizer works in device pixels
// and everything above it in points, so an atlas answers for one display and
// has to be rebuilt when the view moves to another.
//
// A resolved family is not necessarily the family that was asked for — CoreText
// substitutes silently for a name it does not know, and there is no way to ask
// it which it picked — so this cannot report a misspelt family, only one that
// left nothing to draw with.
eacp::OwningPointer<eacp::Text::GlyphAtlas> makeGlyphAtlas(const FontSettings& font,
                                                           float scale);
} // namespace ecode
