#include "Common.h"

#include <ECodeCore/Editor.h>
#include <ECodeCore/Search.h>

// Finding text, moving between occurrences, and replacing them.
//
// The interesting cases are not "does it find the word" — they are the ends of
// the file, where wrapping has to happen; the folds where whole-word and
// case-insensitivity change the answer; and what happens to the current match
// when the document moves underneath it.

using namespace nano;
using namespace ecode;

namespace
{
SearchQuery queryFor(std::string text)
{
    auto query = SearchQuery {};
    query.text = std::move(text);

    return query;
}

eacp::Vector<SearchMatch> matchesIn(const std::string& text,
                                    const SearchQuery& query)
{
    return findMatches(Document::fromText(text), query);
}

Search searchFor(const Document& document, const SearchQuery& query)
{
    auto search = Search {};

    search.setQuery(query);
    search.refresh(document);

    return search;
}

Editor editorWith(std::string text)
{
    return Editor {Document::fromText(std::move(text))};
}
} // namespace

// --- finding ----------------------------------------------------------------

auto tFindsEveryOccurrence = test("Search/findsEveryOccurrence") = []
{
    const auto found = matchesIn("one two one two one", queryFor("one"));

    check(found.size() == 3);
    check(found[0] == SearchMatch {0, 3});
    check(found[1] == SearchMatch {8, 11});
    check(found[2] == SearchMatch {16, 19});
};

auto tEmptyQueryFindsNothing = test("Search/anEmptyQueryFindsNothing") = []
{
    // Not "everything". An empty query with a match at every offset would make
    // the highlight cover the file and the counter read the byte count.
    check(matchesIn("hello", queryFor("")).empty());
};

// Overlap is the case that separates a scan that advances by the match length
// from one that advances by a byte — and the difference is not academic:
// overlapping matches would make replace-all write over its own output.
auto tMatchesDoNotOverlap = test("Search/matchesDoNotOverlap") = []
{
    const auto found = matchesIn("aaaa", queryFor("aa"));

    check(found.size() == 2);
    check(found[0] == SearchMatch {0, 2});
    check(found[1] == SearchMatch {2, 4});
};

auto tCaseInsensitiveByDefault = test("Search/isCaseInsensitiveByDefault") = []
{ check(matchesIn("Hello HELLO hello", queryFor("hello")).size() == 3); };

auto tCaseSensitiveWhenAsked = test("Search/isCaseSensitiveWhenAsked") = []
{
    auto query = queryFor("hello");
    query.caseSensitive = true;

    const auto found = matchesIn("Hello HELLO hello", query);

    check(found.size() == 1);
    check(found[0].start == 12);
};

// The fold is ASCII-only, which is a documented limitation rather than a bug —
// but it must not corrupt what it cannot fold. A multi-byte sequence has to
// survive a case-insensitive scan intact, and a match must never begin partway
// through one.
auto tFoldingLeavesUtf8Alone = test("Search/caseFoldingDoesNotSplitUtf8") = []
{
    // "é" is 0xC3 0xA9 and "É" is 0xC3 0x89. Folding ASCII gets the first three
    // characters to agree and leaves the accented pair differing, so this finds
    // one match rather than two — the documented limitation, pinned so that a
    // later fold reaching past ASCII has to come with a decision about it.
    const auto found = matchesIn("café CAFÉ", queryFor("café"));

    check(found.size() == 1);
    check(found[0] == SearchMatch {0, 5});

    // And the fold does not damage the bytes it cannot fold: the sequence still
    // matches itself exactly.
    auto exact = queryFor("café");
    exact.caseSensitive = true;

    check(matchesIn("café", exact).size() == 1);
};

// Searching bytes rather than characters raises the question of whether a match
// can begin partway through a multi-byte sequence. It cannot, and the reason is
// UTF-8's own design rather than anything this code does: continuation bytes are
// 0x80-0xBF, and no ASCII byte or lead byte falls in that range — so a query
// that is itself well-formed can never align to one.
//
// A query that is *not* well-formed is a different matter, and it does match
// mid-sequence. That is reachable only by constructing one in code: the find
// field is fed by the keyboard, which produces whole characters.
auto tWellFormedQueryCannotAlignMidSequence =
    test("Search/aWellFormedQueryCannotMatchInsideASequence") = []
{
    // "日本語" — three three-byte sequences, no ASCII anywhere to anchor to.
    const auto text = std::string {"日本語"};

    const auto found = matchesIn(text, queryFor("本"));

    check(found.size() == 1);
    check(found[0].start == 3); // the sequence boundary, not a byte inside one

    // The lone continuation byte 0xAC appears inside "本" (0xE6 0x9C 0xAC), and
    // a byte scan does find it — which is why the guarantee above is about
    // well-formed queries specifically, and is stated that way in the header.
    check(matchesIn(text, queryFor("\xac")).size() == 1);
};

auto tWholeWordRejectsSubstrings = test("Search/wholeWordRejectsSubstrings") = []
{
    auto query = queryFor("in");
    query.wholeWord = true;

    const auto found = matchesIn("in inside pin in", query);

    check(found.size() == 2);
    check(found[0].start == 0);
    check(found[1].start == 14);
};

// Non-ASCII is the fold in whole-word matching: 'é' is not an ASCII word
// character, so a boundary test that only knows ASCII reads the position after
// "caf" as a word end and lets the substring through.
auto tWholeWordTreatsNonAsciiAsWord =
    test("Search/wholeWordTreatsNonAsciiAsAWord") = []
{
    auto query = queryFor("caf");
    query.wholeWord = true;

    check(matchesIn("café", query).empty());
};

auto tWholeWordMatchesAtFileEnds =
    test("Search/wholeWordMatchesAgainstTheFileEnds") = []
{
    auto query = queryFor("a");
    query.wholeWord = true;

    // No byte before the first or after the last, which must read as a boundary
    // rather than as a missing one.
    check(matchesIn("a", query).size() == 1);
};

// --- moving between matches -------------------------------------------------

auto tSelectsFromAnOffset = test("Search/selectsTheFirstMatchAtOrAfterAnOffset") = []
{
    const auto document = Document::fromText("one two one two one");
    auto search = searchFor(document, queryFor("one"));

    search.selectAtOrAfter(4);

    check(search.currentIndex() == 1);
    check(search.currentMatch()->start == 8);
};

// At-or-after includes the offset itself. Typing into the find field searches
// from where the caret is, and a match starting exactly there is the one meant.
auto tSelectIncludesTheOffset =
    test("Search/selectAtOrAfterIncludesTheOffsetItself") = []
{
    const auto document = Document::fromText("one two one");
    auto search = searchFor(document, queryFor("one"));

    search.selectAtOrAfter(8);

    check(search.currentMatch()->start == 8);
};

auto tSelectWrapsForward =
    test("Search/selectingPastTheLastMatchWrapsToTheFirst") = []
{
    const auto document = Document::fromText("one two one");
    auto search = searchFor(document, queryFor("one"));

    search.selectAtOrAfter(100);

    check(search.currentIndex() == 0);
};

auto tSelectBeforeWrapsBackwards =
    test("Search/selectingBeforeTheFirstMatchWrapsToTheLast") = []
{
    const auto document = Document::fromText("one two one");
    auto search = searchFor(document, queryFor("one"));

    search.selectBefore(0);

    check(search.currentIndex() == 1);
};

// Walking forward from hit to hit is "search from just past the one I am on",
// so the wrap at the end is the same code path as an offset past the last match
// — this drives it the way the editor does rather than the way the wrap test
// above does.
auto tWalkingForwardWraps = test("Search/walkingForwardFromEachHitWraps") = []
{
    const auto document = Document::fromText("a a");
    auto search = searchFor(document, queryFor("a"));

    search.selectAtOrAfter(0);
    check(search.currentIndex() == 0);

    search.selectAtOrAfter(search.currentMatch()->end);
    check(search.currentIndex() == 1);

    search.selectAtOrAfter(search.currentMatch()->end);
    check(search.currentIndex() == 0);
};

auto tWalkingBackwardsWraps = test("Search/walkingBackwardsFromEachHitWraps") = []
{
    const auto document = Document::fromText("a a a");
    auto search = searchFor(document, queryFor("a"));

    search.selectAtOrAfter(0);
    check(search.currentIndex() == 0);

    search.selectBefore(search.currentMatch()->start);
    check(search.currentIndex() == 2);
};

auto tCountsForDisplay = test("Search/countsItselfForDisplay") = []
{
    const auto document = Document::fromText("a b a b a");
    auto search = searchFor(document, queryFor("a"));

    check(search.count() == 3);
    check(search.currentNumber() == 0); // nothing selected yet

    search.selectAtOrAfter(0);

    check(search.currentNumber() == 1);
};

auto tNoMatchesLeavesNothingSelected =
    test("Search/withNoMatchesNothingIsSelected") = []
{
    const auto document = Document::fromText("hello");
    auto search = searchFor(document, queryFor("zzz"));

    search.selectAtOrAfter(0);
    search.selectBefore(5);

    check(search.currentMatch() == nullptr);
    check(search.currentNumber() == 0);
};

// --- surviving an edit ------------------------------------------------------

// The current match is re-found by offset rather than by index, and this is the
// case that tells the two apart: inserting an occurrence *before* the current
// one shifts every index by one, so an implementation that kept the index would
// silently move the selection to the previous occurrence while the counter went
// on reading the same number.
auto tRefreshKeepsThePlaceNotTheIndex =
    test("Search/refreshKeepsTheOccurrenceRatherThanTheIndex") = []
{
    auto editor = editorWith("one two one");
    auto search = searchFor(editor.document(), queryFor("one"));

    search.selectAtOrAfter(8);

    check(search.currentIndex() == 1);
    check(search.currentMatch()->start == 8);

    // A new occurrence at the very front. The one being looked at has not
    // moved in the file, but it is now the second of three rather than the
    // second of two.
    editor.placeCaret(0);
    editor.insert("one ");

    search.refresh(editor.document());

    check(search.count() == 3);
    check(search.currentIndex() == 2);
    check(search.currentMatch()->start == 12);
};

auto tRefreshWithNoSelectionStaysUnselected =
    test("Search/refreshLeavesAnUnpointedSearchUnpointed") = []
{
    auto editor = editorWith("one two");
    auto search = searchFor(editor.document(), queryFor("one"));

    editor.placeCaret(0);
    editor.insert("x");

    search.refresh(editor.document());

    check(search.count() == 1);
    check(search.currentMatch() == nullptr);
};

// --- replacing --------------------------------------------------------------

auto tReplacesOneMatch = test("Search/replacesOneMatch") = []
{
    auto editor = editorWith("one two one");

    replaceMatch(editor, {0, 3}, "1");

    check(editor.document().text() == "1 two one");

    // The caret lands after the replacement, so a search carrying on from here
    // finds what follows rather than what was just written.
    check(editor.cursor().head == 1);
};

auto tReplaceAllReplacesEverything =
    test("Search/replaceAllReplacesEveryOccurrence") = []
{
    auto editor = editorWith("one two one two one");

    check(replaceAll(editor, queryFor("one"), "1") == 3);
    check(editor.document().text() == "1 two 1 two 1");
};

// A replacement longer than what it replaced is what catches an implementation
// that runs front to back without tracking the shift: the second replacement
// would land at an offset the first one has already moved.
auto tReplaceAllHandlesGrowth =
    test("Search/replaceAllSurvivesAGrowingReplacement") = []
{
    auto editor = editorWith("a b a b a");

    check(replaceAll(editor, queryFor("a"), "LONGER") == 3);
    check(editor.document().text() == "LONGER b LONGER b LONGER");
};

// The expensive direction: a replace-all that undid one occurrence at a time
// would leave the file half-replaced after a single ⌘Z, which reads as the undo
// having corrupted it.
auto tReplaceAllIsOneUndoStep = test("Search/replaceAllUndoesAsOneStep") = []
{
    auto editor = editorWith("one two one two one");
    const auto before = editor.document().text();

    replaceAll(editor, queryFor("one"), "1");
    editor.undo();

    check(editor.document().text() == before);
    check(!editor.canUndo());
};

auto tReplaceAllRedoesAsOneStep = test("Search/replaceAllRedoesAsOneStep") = []
{
    auto editor = editorWith("one two one");

    replaceAll(editor, queryFor("one"), "1");
    editor.undo();

    // Asserted before the redo, and it is the check that does the work: an
    // ungrouped replace-all leaves the file half-reverted here, and then the
    // redo puts that same half back and the final text comes out right anyway.
    // Without this line the test passes against the thing it was written to
    // rule out.
    check(editor.document().text() == "one two one");

    editor.redo();

    check(editor.document().text() == "1 two 1");
    check(!editor.canRedo());
};

// Typing either side of a grouped operation must stay outside it, or one ⌘Z
// would take the replace-all *and* the word typed after it.
auto tReplaceAllDoesNotSwallowSurroundingTyping =
    test("Search/replaceAllDoesNotSwallowSurroundingTyping") = []
{
    auto editor = editorWith("one one");

    editor.moveToDocumentEnd();
    editor.insert("XY");

    replaceAll(editor, queryFor("one"), "1");

    editor.moveToDocumentEnd();
    editor.insert("ZW");

    check(editor.document().text() == "1 1XYZW");

    editor.undo();
    check(editor.document().text() == "1 1XY");

    editor.undo();
    check(editor.document().text() == "one oneXY");

    editor.undo();
    check(editor.document().text() == "one one");
};

auto tReplaceAllWithNoMatchesDoesNothing =
    test("Search/replaceAllWithNoMatchesChangesNothing") = []
{
    auto editor = editorWith("hello");

    check(replaceAll(editor, queryFor("zzz"), "x") == 0);
    check(editor.document().text() == "hello");

    // And leaves nothing on the undo stack, so ⌘Z after a replace-all that
    // matched nothing does not undo whatever came before it instead.
    check(!editor.canUndo());
};

auto tReplaceAllHonoursTheQueryOptions =
    test("Search/replaceAllHonoursTheQueryOptions") = []
{
    auto editor = editorWith("in inside in");

    auto query = queryFor("in");
    query.wholeWord = true;

    check(replaceAll(editor, query, "out") == 2);
    check(editor.document().text() == "out inside out");
};
