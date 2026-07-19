#include <ECodeCore/Commands.h>
#include <ECodeCore/FuzzyMatch.h>

#include <NanoTest/NanoTest.h>

// The command registry and the scorer the palette ranks with. Both are plain
// logic — no widgets, no device — which is why they live in ECodeCore rather
// than beside the palette that consumes them.

using namespace nano;
using namespace ecode;

// --- fuzzy matching ---------------------------------------------------------

auto tFuzzyMatchesASubsequence = test("Fuzzy/matchesASubsequenceNotASubstring") = []
{
    // Not a substring of "File: Save" anywhere, but the letters are in order.
    check(fuzzyMatch("fsv", "File: Save").has_value());
    check(fuzzyMatch("save", "File: Save").has_value());
};

// Order is the whole difference between a subsequence match and a bag of
// letters: "ea" is in "Save", but not in that order.
auto tFuzzyRequiresOrder = test("Fuzzy/requiresTheQueryLettersInOrder") = []
{
    check(!fuzzyMatch("ea", "Save").has_value());
    check(fuzzyMatch("ae", "Save").has_value());
};

auto tFuzzyIsCaseInsensitive = test("Fuzzy/ignoresCase") = []
{
    check(fuzzyMatch("SAVE", "File: Save").has_value());
    check(fuzzyMatch("save", "FILE: SAVE").has_value());
};

// An empty query matches everything, which is what an unfiltered palette shows.
auto tFuzzyEmptyQueryMatches = test("Fuzzy/emptyQueryMatchesEverything") = []
{
    const auto match = fuzzyMatch("", "Anything at all");

    check(match.has_value());
    check(match->positions.empty());
    check(match->score == 0);
};

// Spaces are skipped rather than matched, so a query can be typed the way the
// title reads without reproducing its punctuation.
auto tFuzzyIgnoresSpaces = test("Fuzzy/ignoresSpacesInTheQuery") = []
{
    check(fuzzyMatch("fi sa", "File: Save").has_value());
};

// The positions are what the palette highlights, so they have to be the bytes
// that actually matched rather than merely the right *number* of them. The case
// is chosen so a run of consecutive indices would be wrong: 'a' and 'e' in
// "Save" are two apart.
auto tFuzzyReportsMatchedPositions = test("Fuzzy/reportsWhereItMatched") = []
{
    const auto match = fuzzyMatch("ae", "Save");

    check(match.has_value());
    check(match->positions.size() == 2);
    check(match->positions[0] == 1);
    check(match->positions[1] == 3);
};

// The reason the score exists rather than a plain filter. Both titles contain
// s-a in order, so filtering alone would offer them in registration order and
// put the obvious answer second.
//
// What separates them here is the *run* bonus: "sa" is contiguous in "Save" and
// six characters apart in "Select All".
auto tFuzzyRanksRunsHigher = test("Fuzzy/ranksAContiguousRunAboveScatteredLetters") = []
{
    const auto save = fuzzyMatch("sa", "File: Save");
    const auto selectAll = fuzzyMatch("sa", "Edit: Select All");

    check(save.has_value());
    check(selectAll.has_value());
    check(save->score > selectAll->score);
};

// And the boundary bonus separately, which needs a single-character query —
// with two characters the run bonus swamps it, so a two-character case cannot
// tell the two bonuses apart and scores the same either way.
auto tFuzzyRanksWordStartsHigher = test("Fuzzy/ranksAWordStartAboveALetterMidWord") = []
{
    const auto sidebar = fuzzyMatch("s", "Toggle Sidebar");
    const auto close = fuzzyMatch("s", "Close");

    check(sidebar.has_value());
    check(close.has_value());
    check(sidebar->score > close->score);
};

// --- the registry -----------------------------------------------------------

namespace
{
CommandRegistry registryWith(std::function<void()> action)
{
    auto registry = CommandRegistry {};
    registry.add({"test.run", "Test: Run", std::move(action)});

    return registry;
}
} // namespace

auto tRegistryFindsAndRuns = test("Commands/findsAndRunsById") = []
{
    auto ran = 0;
    const auto registry = registryWith([&ran] { ++ran; });

    check(registry.find("test.run") != nullptr);
    check(registry.find("test.run")->title == "Test: Run");

    check(registry.run("test.run"));
    check(ran == 1);
};

// A keymap can name a command that was never registered — a typo in a config
// file, or a binding left behind by a command that was removed. That has to be
// a dead binding rather than a crash.
auto tRegistryUnknownIdIsHarmless = test("Commands/unknownIdRunsNothing") = []
{
    auto ran = 0;
    const auto registry = registryWith([&ran] { ++ran; });

    check(registry.find("nope") == nullptr);
    check(!registry.run("nope"));
    check(ran == 0);
};

// The expensive direction is running the action anyway: a paste with an empty
// clipboard, an undo with no history. Asserting only on run()'s return value
// would pass against an implementation that reported false *after* calling it.
auto tRegistryDoesNotRunDisabled = test("Commands/doesNotRunADisabledCommand") = []
{
    auto ran = 0;

    auto registry = CommandRegistry {};
    registry.add({"test.off", "Test: Off", [&ran] { ++ran; }, [] { return false; }});

    check(!registry.run("test.off"));
    check(ran == 0);
};

// Registering an id twice replaces it. The alternative — two entries, one of
// them unreachable — shows up as a command listed twice in the palette where
// only one of the two does anything.
auto tRegistryReplacesById = test("Commands/reRegisteringReplacesRatherThanDuplicates") = []
{
    auto first = 0;
    auto second = 0;

    auto registry = CommandRegistry {};
    registry.add({"test.run", "First", [&first] { ++first; }});
    registry.add({"test.run", "Second", [&second] { ++second; }});

    check(registry.commands().size() == 1);
    check(registry.commands()[0].title == "Second");

    check(registry.run("test.run"));
    check(first == 0);
    check(second == 1);
};

// Order is the palette's order for an empty query, so it is part of the
// contract rather than an accident of the container.
auto tRegistryKeepsOrder = test("Commands/keepsRegistrationOrder") = []
{
    auto registry = CommandRegistry {};

    registry.add({"a", "A"});
    registry.add({"b", "B"});
    registry.add({"c", "C"});

    check(registry.commands().size() == 3);
    check(registry.commands()[0].id == "a");
    check(registry.commands()[2].id == "c");
};
