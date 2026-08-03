#include "FontSettings.h"

#include <algorithm>

namespace ecode
{
using namespace eacp;

namespace
{
// Where the atlas starts and how far it may grow. A screenful of Latin code
// fits in 512 long before the first commit; 4096 is the ceiling every desktop
// GPU allows, and reaching it is what makes the atlas clear rather than grow.
constexpr auto initialAtlasSize = 512;
constexpr auto maximumAtlasSize = 4096;
} // namespace

float FontSettings::size() const
{
    return std::clamp(pointSize + zoom, minimumSize, maximumSize);
}

bool FontSettings::canZoom(int steps) const
{
    if (steps > 0)
        return size() < maximumSize;

    if (steps < 0)
        return size() > minimumSize;

    return false;
}

void FontSettings::zoomBy(int steps)
{
    const auto wanted = size() + static_cast<float>(steps) * zoomStep;

    zoom = std::clamp(wanted, minimumSize, maximumSize) - pointSize;
}

bool FontSettings::operator==(const FontSettings& other) const
{
    return family == other.family && pointSize == other.pointSize
           && zoom == other.zoom;
}

OwningPointer<Text::GlyphAtlas> makeGlyphAtlas(const FontSettings& font, float scale)
{
    auto request = Text::FontRequest {};

    request.family = font.family;
    request.pointSize = font.size();
    request.scale = scale;

    // The atlas builds its own faces through the factory and reports nothing
    // when a family fails to resolve — it falls back to face 0. So the probe
    // stays: a caller asking for a font this machine does not have gets an
    // empty pointer here rather than an atlas quietly drawing something else.
    if (!Text::GlyphRasterizer {request}.isValid())
        return {};

    return makeOwned<Text::GlyphAtlas>(Text::rasterizerFaceFactory(),
                                       request,
                                       initialAtlasSize,
                                       maximumAtlasSize);
}
} // namespace ecode
