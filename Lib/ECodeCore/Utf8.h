#pragma once

#include <algorithm>
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

// A byte that continues a sequence rather than starting one. UTF-8 is
// self-synchronizing precisely because these occupy a range of their own, which
// is what makes the two functions below a scan rather than a re-decode from the
// start of the string.
inline bool isContinuation(char c)
{
    return (static_cast<unsigned char>(c) & 0xc0) == 0x80;
}

// The character boundary before `index`, and the one after it. Clamped to the
// ends of the text.
//
// What a caret moves by and what backspace removes. A byte would do for ASCII
// and leave half a sequence behind for anything else — and half a sequence is
// not merely wrong on screen, it stops the text matching anything.
inline std::size_t previousBoundary(std::string_view text, std::size_t index)
{
    index = std::min(index, text.size());

    while (index > 0)
    {
        --index;

        if (!isContinuation(text[index]))
            break;
    }

    return index;
}

inline std::size_t nextBoundary(std::string_view text, std::size_t index)
{
    if (index >= text.size())
        return text.size();

    ++index;

    while (index < text.size() && isContinuation(text[index]))
        ++index;

    return index;
}
} // namespace ecode::Utf8
