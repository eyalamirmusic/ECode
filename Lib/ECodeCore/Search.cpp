#include "Search.h"

#include "Editor.h"

namespace ecode
{
namespace
{
// ASCII case fold. The header says why this stops at ASCII, and why stopping
// there is safe rather than merely incomplete.
char fold(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// A byte that can be part of a word, for whole-word matching.
//
// Every non-ASCII byte counts as one. Without that, whole-word "caf" would match
// inside "café": the bytes of 'é' are not ASCII word characters, so the position
// after "caf" would read as a word boundary and the match would stand.
bool isWordByte(char c)
{
    const auto byte = static_cast<unsigned char>(c);

    return byte >= 0x80 || byte == '_' || (byte >= '0' && byte <= '9')
           || (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
}

bool matchesAt(std::string_view text, std::size_t at, const SearchQuery& query)
{
    const auto length = query.text.size();

    if (at + length > text.size())
        return false;

    for (std::size_t index = 0; index < length; ++index)
    {
        const auto a = text[at + index];
        const auto b = query.text[index];

        if (query.caseSensitive ? a != b : fold(a) != fold(b))
            return false;
    }

    if (!query.wholeWord)
        return true;

    const auto touchesWordBefore = at > 0 && isWordByte(text[at - 1]);
    const auto touchesWordAfter =
        at + length < text.size() && isWordByte(text[at + length]);

    return !touchesWordBefore && !touchesWordAfter;
}
} // namespace

eacp::Vector<SearchMatch> findMatches(const Document& document,
                                      const SearchQuery& query)
{
    auto results = eacp::Vector<SearchMatch> {};

    if (query.isEmpty())
        return results;

    const auto text = std::string_view {document.text()};
    const auto length = query.text.size();

    // A plain forward scan. O(text × query) and good enough at the sizes an
    // editor sees on a keystroke; the place to put Boyer-Moore is here, behind
    // an unchanged signature, once a profile asks for it.
    for (std::size_t at = 0; at + length <= text.size();)
    {
        if (!matchesAt(text, at, query))
        {
            ++at;
            continue;
        }

        results.push_back({at, at + length});

        // Occurrences do not overlap: searching "aa" in "aaa" finds one match,
        // not two. Two would mean replace-all writing over its own output.
        at += length;
    }

    return results;
}

const SearchMatch* Search::currentMatch() const
{
    if (index < 0 || index >= found.size())
        return nullptr;

    return &found[index];
}

void Search::refresh(const Document& document)
{
    // Where the current match was, read before the list that names it is thrown
    // away.
    const auto* selected = currentMatch();
    const auto hadSelection = selected != nullptr;
    const auto wasAt = hadSelection ? selected->start : std::size_t {0};

    found = findMatches(document, current);
    index = -1;

    if (hadSelection)
        selectAtOrAfter(wasAt);
}

void Search::selectAtOrAfter(std::size_t offset)
{
    index = -1;

    if (found.empty())
        return;

    for (auto i = 0; i < found.size(); ++i)
    {
        if (found[i].start >= offset)
        {
            index = i;
            return;
        }
    }

    // Everything is behind the offset, so wrap to the top.
    index = 0;
}

void Search::selectBefore(std::size_t offset)
{
    index = -1;

    if (found.empty())
        return;

    for (auto i = found.size() - 1; i >= 0; --i)
    {
        if (found[i].start < offset)
        {
            index = i;
            return;
        }
    }

    // Everything is ahead of the offset, so wrap to the bottom.
    index = found.size() - 1;
}

void replaceMatch(Editor& editor, SearchMatch match, std::string_view replacement)
{
    // Its own step, whatever surrounds it: a replace is one action to undo, and
    // typing either side of it is not part of that action.
    editor.breakUndoStep();

    editor.placeCaret(match.start);
    editor.placeCaret(match.end, true);
    editor.insert(replacement);

    editor.breakUndoStep();
}

int replaceAll(Editor& editor,
               const SearchQuery& query,
               std::string_view replacement)
{
    const auto matches = findMatches(editor.document(), query);

    if (matches.empty())
        return 0;

    // One step for the lot. Without the group each replacement is a separate
    // entry on the undo stack — the history's merge rule only ever joins
    // insertions that continue where the last one ended, and these are
    // replacements running backwards — so undoing a replace-all over 200
    // occurrences would take 200 presses of ⌘Z.
    const auto group = UndoGroup {editor};

    // Back to front, so each replacement lands at an offset the earlier ones
    // have not moved.
    for (auto index = matches.size() - 1; index >= 0; --index)
    {
        const auto& match = matches[index];

        editor.placeCaret(match.start);
        editor.placeCaret(match.end, true);
        editor.insert(replacement);
    }

    return matches.size();
}
} // namespace ecode
