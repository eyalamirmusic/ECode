#pragma once

#include "Document.h"

#include <eacp/Core/Core.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace ecode
{
class Editor;

// What to look for, and how strictly.
struct SearchQuery
{
    std::string text;

    bool caseSensitive = false;
    bool wholeWord = false;

    bool isEmpty() const { return text.empty(); }

    bool operator==(const SearchQuery&) const = default;
};

// One occurrence, as a half-open byte range.
//
// The same units the cursor and the renderer already speak, so a match can be
// selected or highlighted without converting anything — Cursor is two byte
// offsets and TextRenderer::columnToX takes one.
struct SearchMatch
{
    std::size_t start = 0;
    std::size_t end = 0;

    std::size_t length() const { return end - start; }

    bool operator==(const SearchMatch&) const = default;
};

// Every non-overlapping occurrence, in document order.
//
// Literal text, not a regular expression. Regex needs an engine, and none of
// the decisions above this level change with one: the match list, the wrap, the
// replace and the highlighting all take ranges and do not care how they were
// found. This function is the seam a regex engine slots into.
//
// Case-insensitive matching folds ASCII only. That is a real limitation — "Ä"
// does not match "ä" — and also the reason it is safe: every byte of a
// multi-byte UTF-8 sequence is >= 0x80, and folding only ever touches 'A'-'Z',
// so it cannot alter a sequence. A fold that reached further would need the
// Unicode case tables eacp does not have (PLAN.md gap 9), and getting it
// half-right silently corrupts non-ASCII text.
//
// Matching is over bytes, so it is worth being exact about what that does and
// does not guarantee. A *well-formed* query can never match partway through a
// sequence, because continuation bytes are 0x80-0xBF and neither ASCII nor a
// lead byte falls in that range. A malformed one can, and does — but the only
// producer of queries is a keyboard, which emits whole characters.
eacp::Vector<SearchMatch> findMatches(const Document& document,
                                      const SearchQuery& query);

// A live search over one document: the query, everywhere it occurs, and which
// occurrence is the current one.
//
// Kept in ECodeCore rather than in the find bar because the parts worth getting
// right are not visual — wrapping at the ends, staying on the same occurrence
// across an edit, and what "3 of 17" counts. All of it is testable by driving a
// document and reading the numbers back.
class Search
{
public:
    void setQuery(SearchQuery newQuery) { current = std::move(newQuery); }
    const SearchQuery& query() const { return current; }

    // Recomputes the match list against the document as it is now.
    //
    // The current match is then re-picked by the byte offset it was at rather
    // than by its index. An edit anywhere earlier in the file changes how many
    // occurrences precede this one, so the index quietly comes to name a
    // different place — and the symptom is the counter reading the same "3 of
    // 17" while the view jumps somewhere unrelated. An offset survives that.
    void refresh(const Document& document);

    const eacp::Vector<SearchMatch>& matches() const { return found; }

    int count() const { return found.size(); }
    bool hasMatches() const { return !found.empty(); }

    // Null when there is no current match, which covers an empty query, a query
    // that matches nothing, and a search that has not been pointed anywhere yet.
    const SearchMatch* currentMatch() const;

    int currentIndex() const { return index; }

    // The "3" in "3 of 17". Zero when there is no current match, so a caller can
    // print the pair without a special case for the empty state.
    int currentNumber() const { return index + 1; }

    // These two are the whole of "find next" and "find previous", and also what
    // typing in the find field does.
    //
    // Both take an offset rather than stepping an index, which is the one design
    // decision here worth defending: a search is driven by where the caret is,
    // so moving the caret and pressing ⌘G again looks from where the person is
    // rather than resuming a walk they have left. Wrapping then falls out of it
    // instead of being a case — past the last match is the same question as past
    // the end of the file, and both answers are "start again at the other end".
    void selectAtOrAfter(std::size_t offset);
    void selectBefore(std::size_t offset);

private:
    SearchQuery current;
    eacp::Vector<SearchMatch> found;

    // -1 for "no current match". Kept alongside the offset it refers to, which
    // is what refresh() re-derives it from.
    int index = -1;
};

// Replaces one match, as its own undo step, and leaves the caret after the
// replacement so a following search carries on past it rather than finding what
// was just written.
//
// Takes the match by value: it names a range in the text that this call is about
// to change, so holding a reference into the match list would be a read into a
// vector the caller is expected to rebuild immediately afterwards.
void replaceMatch(Editor& editor, SearchMatch match, std::string_view replacement);

// Replaces every occurrence, as one undo step, and returns how many.
//
// Runs back to front so that each replacement lands at an offset the earlier
// ones have not moved — the alternative is tracking a running delta, which is
// the same arithmetic with somewhere to get it wrong.
int replaceAll(Editor& editor,
               const SearchQuery& query,
               std::string_view replacement);
} // namespace ecode
