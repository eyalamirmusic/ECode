#pragma once

namespace ecode
{
// Where a view sits over its content, in points.
//
// Negative up and left, because it is *added* to the content's own coordinates
// rather than subtracted from them: a document scrolled down draws its rows
// above the viewport, so the offset that puts them there is negative. Zero is
// the top-left corner of the content, which is what makes clamping one-sided —
// the origin is always legal and only the far end has to be worked out.
struct ScrollOffset
{
    float x = 0.f;
    float y = 0.f;

    bool operator==(const ScrollOffset&) const = default;
};
} // namespace ecode
