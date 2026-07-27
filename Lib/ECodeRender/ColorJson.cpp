#include "ColorJson.h"

#include <algorithm>
#include <cmath>

namespace ecode
{
using namespace eacp;

namespace
{
constexpr auto hexDigits = "0123456789abcdef";

int toByte(float channel)
{
    return static_cast<int>(std::lround(std::clamp(channel, 0.f, 1.f) * 255.f));
}

void appendByte(std::string& text, float channel)
{
    const auto value = toByte(channel);

    text += hexDigits[value >> 4];
    text += hexDigits[value & 0xf];
}

// -1 for anything that is not a hex digit, which is what makes the length check
// alone insufficient: "#gggggg" is six characters and no colour.
int hexValue(char character)
{
    if (character >= '0' && character <= '9')
        return character - '0';

    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;

    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;

    return -1;
}

float channelAt(std::string_view digits, std::size_t index)
{
    const auto high = hexValue(digits[index]);
    const auto low = hexValue(digits[index + 1]);

    return static_cast<float>(high * 16 + low) / 255.f;
}
} // namespace

std::string toHexColor(const Graphics::Color& color)
{
    auto text = std::string {"#"};

    appendByte(text, color.r);
    appendByte(text, color.g);
    appendByte(text, color.b);

    // The alpha digits only when there is something to say. A theme is mostly
    // opaque colours, and "#1e1e2e" reads as one where "#1e1e2eff" reads as a
    // colour with a detail worth noticing.
    if (toByte(color.a) != 255)
        appendByte(text, color.a);

    return text;
}

Graphics::Color fromHexColor(std::string_view text, const Graphics::Color& fallback)
{
    if (!text.empty() && text.front() == '#')
        text.remove_prefix(1);

    if (text.size() != 6 && text.size() != 8)
        return fallback;

    for (const auto character: text)
        if (hexValue(character) < 0)
            return fallback;

    // Alpha defaults to opaque rather than to the fallback's, so "#ff0000" over
    // a translucent entry means the opaque red it looks like.
    return {channelAt(text, 0),
            channelAt(text, 2),
            channelAt(text, 4),
            text.size() == 8 ? channelAt(text, 6) : 1.f};
}
} // namespace ecode

namespace eacp::Graphics
{
void reflectValue(Miro::Reflector& ref, Color& color)
{
    auto text = ecode::toHexColor(color);

    ref.visit(text);

    if (!ref.isLoading())
        return;

    // Only when the file genuinely said something. This is the whole of the
    // override semantics — load a palette into the struct, load the file's
    // colours over it, and a key the file omits keeps the palette's answer
    // without either side keeping a list of which were set — but "omits" has to
    // be asked rather than inferred from the string coming back unchanged.
    //
    // Because the hex round trip is lossy. A channel that was never a multiple
    // of 1/255 comes back as the nearest one, so re-parsing on an absent key
    // would nudge every colour in the palette by up to half a level, and the
    // palette a file inherited would stop being the palette the code declared.
    // Half a level is invisible; a theme that is not the theme it says it is,
    // is not.
    if (ref.kind() == Miro::ValueKind::String)
        color = ecode::fromHexColor(text, color);
}
} // namespace eacp::Graphics
