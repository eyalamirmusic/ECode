#pragma once

#include <eacp/Graphics/Graphics.h>

#include <Miro/Reflect.h>

#include <string>
#include <string_view>

namespace ecode
{
// A colour as a theme file spells it: "#1e1e2e", or "#ffffff0d" when it is
// translucent.
//
// Hex rather than the four floats the struct actually holds, because a theme is
// written by hand and compared by eye. Every editor's theme format spells a
// colour this way, so a palette can be pasted between them; nobody can picture
// {"r": 0.098, "g": 0.106, "b": 0.125}, and a diff of one is unreadable.
//
// The mapping is the plain byte one — channel × 255, rounded — with no gamma
// step, because the floats in the palettes are already sRGB values that were
// written as n/255 in the first place. PLAN.md §4's gamma work is about the
// space the *blend* happens in, and does not change what a theme file means.
std::string toHexColor(const eacp::Graphics::Color& color);

// Six digits or eight, with the leading '#' optional. Anything else is
// `fallback`, which is how a typo in one entry costs that entry rather than the
// theme: the caller passes the colour the palette already had, so an
// unparseable override leaves the built-in one showing rather than painting a
// widget black.
eacp::Graphics::Color fromHexColor(std::string_view text,
                                   const eacp::Graphics::Color& fallback);
} // namespace ecode

namespace eacp::Graphics
{
// Teaches Miro that a Color is a *string* slot rather than an object of four
// numbers. Miro looks this up by ADL on the value type, which is why it lives in
// eacp's namespace rather than ecode's — the documented extension point, and the
// only spelling the dispatcher will find.
//
// Declared rather than defined here so that a translation unit reflecting a
// theme has to include this header: without it the fallback overload is the one
// for reflectable structs, Color satisfies none of its constraints, and the
// build fails loudly instead of quietly choosing a different JSON shape.
void reflectValue(Miro::Reflector& ref, Color& color);
} // namespace eacp::Graphics
