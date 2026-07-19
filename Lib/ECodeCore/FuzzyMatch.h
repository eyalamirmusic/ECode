#pragma once

#include <eacp/Core/Core.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string_view>

namespace ecode
{
// An fzf-style case-insensitive subsequence match, scored so the better of two
// matches sorts first.
//
// Adapted from CowTerm's `FuzzyMatch.h`, which PLAN.md §5 named as directly
// liftable, with one addition: the matched byte offsets come back too, so the
// palette can pick out the characters the query hit rather than highlighting
// the whole row. That is the difference between a list that shows you *what* it
// matched and one that only shows you *that* it matched.
struct FuzzyMatch
{
    int score = 0;

    // Byte offsets into the matched text, ascending. Safe to split a UTF-8
    // string on: the query is compared byte-wise against ASCII, and a
    // continuation byte is >= 0x80, so a position never lands mid-sequence.
    eacp::Vector<std::size_t> positions;
};

// Nothing when the query is not a subsequence of the text. An empty query
// matches everything at zero, which is what an unfiltered palette wants.
inline std::optional<FuzzyMatch> fuzzyMatch(std::string_view query,
                                            std::string_view text)
{
    const auto lower = [](char c)
    { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };

    // Word starts score highly: "sa" should rank "File: Save" above "Select
    // All", where the same two letters are buried mid-word.
    const auto isBoundary = [&](std::size_t index)
    {
        if (index == 0)
            return true;

        return std::isalnum(static_cast<unsigned char>(text[index - 1])) == 0;
    };

    auto result = FuzzyMatch {};

    std::size_t position = 0;
    auto hasPrevious = false;

    for (auto queryChar: query)
    {
        // Spaces are ignored rather than matched, so "fi sa" finds "File: Save"
        // without the query having to reproduce the punctuation between them.
        if (queryChar == ' ')
            continue;

        auto found = false;

        for (auto i = position; i < text.size(); ++i)
        {
            if (lower(text[i]) != lower(queryChar))
                continue;

            // Immediately after the previous match — a run, which is what
            // makes a typed prefix beat letters scattered across the string.
            result.score += hasPrevious && i == position ? 8 : 1;

            if (isBoundary(i))
                result.score += 4;

            // Skipped text costs, but only up to a point: past three
            // characters the gap says nothing more about how good the match is.
            result.score -= static_cast<int>(std::min<std::size_t>(i - position, 3));

            result.positions.push_back(i);

            position = i + 1;
            hasPrevious = true;
            found = true;

            break;
        }

        if (!found)
            return std::nullopt;
    }

    return result;
}
} // namespace ecode
