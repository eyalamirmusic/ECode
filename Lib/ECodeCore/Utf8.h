#pragma once

#include <cstddef>
#include <string_view>

namespace ecode::Utf8
{
// Decodes the codepoint at `index` and advances it past the sequence.
//
// Here rather than in eacp because Strings has no codepoint iteration yet —
// gap 9 in PLAN.md. Deliberately permissive: a malformed sequence yields
// whatever the bits say and still advances, because a decoder that stalls or
// throws on bad input turns one corrupt byte in a file into an editor that
// cannot open it.
//
// The caller must have checked `index < text.size()`.
inline char32_t next(std::string_view text, std::size_t& index)
{
    const auto lead = static_cast<unsigned char>(text[index]);

    if (lead < 0x80)
        return static_cast<char32_t>(text[index++]);

    auto extra = 0;
    auto value = char32_t {0};

    if ((lead & 0xe0) == 0xc0)
    {
        extra = 1;
        value = lead & 0x1fu;
    }
    else if ((lead & 0xf0) == 0xe0)
    {
        extra = 2;
        value = lead & 0x0fu;
    }
    else
    {
        extra = 3;
        value = lead & 0x07u;
    }

    ++index;

    for (auto i = 0; i < extra && index < text.size(); ++i, ++index)
        value = (value << 6) | (static_cast<unsigned char>(text[index]) & 0x3fu);

    return value;
}
} // namespace ecode::Utf8
