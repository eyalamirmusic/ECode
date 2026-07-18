#include "Common.h"

#include <ECodeCore/TextEdit.h>

// Edits and undo history.
//
// The interesting behaviour is *grouping*: what counts as one step. Get it
// wrong and undo either erases a whole file at once or gives back one character
// at a time, and both feel broken in ways that are hard to pin down later. The
// rule is adjacency, deliberately rather than elapsed time, so it is
// deterministic and can be tested at all.

using namespace nano;
using namespace ecode;

namespace
{
TextEdit insertion(std::size_t at, std::string text)
{
    return {at, "", std::move(text)};
}

TextEdit deletion(std::size_t at, std::string text)
{
    return {at, std::move(text), ""};
}

// Applies edits to a string the way a document would, so history tests can
// check the text actually comes back.
void applyTo(std::string& text, const TextEdit& edit)
{
    text.replace(edit.start, edit.removed.size(), edit.inserted);
}
} // namespace

auto tInverseSwapsStrings = test("TextEdit/inverseSwapsRemovedAndInserted") = []
{
    const auto edit = TextEdit {4, "old", "new"};
    const auto back = edit.inverted();

    check(back.start == 4);
    check(back.removed == "new");
    check(back.inserted == "old");
};

// Applying an edit then its inverse must return the original text exactly.
// This is the property the whole undo design rests on.
auto tInverseRoundTrips = test("TextEdit/applyingAnInverseRestoresTheText") = []
{
    auto text = std::string {"hello world"};

    const auto edit = TextEdit {6, "world", "there"};
    applyTo(text, edit);
    check(text == "hello there");

    applyTo(text, edit.inverted());
    check(text == "hello world");
};

auto tDeltaReportsSizeChange = test("TextEdit/deltaReportsTheSizeChange") = []
{
    check(insertion(0, "abc").delta() == 3);
    check(deletion(0, "abc").delta() == -3);
    check(TextEdit {0, "ab", "cd"}.delta() == 0);
};

auto tEmptyEditsAreIgnored = test("EditHistory/emptyEditsAreNotRecorded") = []
{
    auto history = EditHistory {};

    history.record({});

    check(!history.canUndo());
};

auto tRecordsAnEdit = test("EditHistory/recordsAnEdit") = []
{
    auto history = EditHistory {};

    history.record(insertion(0, "a"));

    check(history.canUndo());
    check(!history.canRedo());
    check(history.undoDepth() == 1);
};

// Typing a word must undo as a word. Consecutive insertions that continue where
// the last ended merge into the step already open.
auto tMergesConsecutiveTyping = test("EditHistory/consecutiveTypingIsOneStep") = []
{
    auto history = EditHistory {};

    history.record(insertion(0, "h"));
    history.record(insertion(1, "e"));
    history.record(insertion(2, "l"));
    history.record(insertion(3, "l"));
    history.record(insertion(4, "o"));

    check(history.undoDepth() == 1);

    auto text = std::string {"hello"};

    for (const auto& edit: history.undo())
        applyTo(text, edit);

    check(text.empty());
};

// Typing somewhere else starts a new step: the two runs are separate intentions
// and should undo separately.
auto tNonAdjacentTypingSplits =
    test("EditHistory/typingElsewhereStartsANewStep") = []
{
    auto history = EditHistory {};

    history.record(insertion(0, "a"));
    history.record(insertion(1, "b"));
    history.record(insertion(40, "z")); // caret moved

    check(history.undoDepth() == 2);
};

// A newline ends the step, so undo after typing several lines goes back a line
// at a time rather than wiping everything typed.
auto tNewlineBreaksTheStep = test("EditHistory/newlinesEndAStep") = []
{
    auto history = EditHistory {};

    history.record(insertion(0, "a"));
    history.record(insertion(1, "\n"));
    history.record(insertion(2, "b"));

    check(history.undoDepth() == 3);
};

// Deletion never merges into typing, in either direction.
auto tDeletionsDoNotMerge = test("EditHistory/deletionsAreTheirOwnStep") = []
{
    auto history = EditHistory {};

    history.record(insertion(0, "a"));
    history.record(deletion(0, "a"));
    history.record(insertion(0, "b"));

    check(history.undoDepth() == 3);
};

auto tBreakStepForcesASplit = test("EditHistory/breakStepSplitsTyping") = []
{
    auto history = EditHistory {};

    history.record(insertion(0, "a"));
    history.breakStep();
    history.record(insertion(1, "b"));

    check(history.undoDepth() == 2);
};

// Within a step, later edits were applied to text the earlier ones had already
// shifted, so the inverses have to come back in reverse order. Undoing them
// front to back would corrupt the text.
auto tUndoReturnsInversesInReverse = test("EditHistory/undoReversesWithinAStep") = []
{
    auto history = EditHistory {};
    auto text = std::string {};

    const TextEdit typed[] = {
        insertion(0, "a"), insertion(1, "b"), insertion(2, "c")};

    for (const auto& edit: typed)
    {
        applyTo(text, edit);
        history.record(edit);
    }

    check(text == "abc");

    for (const auto& edit: history.undo())
        applyTo(text, edit);

    check(text.empty());
};

auto tRedoReplaysTheStep = test("EditHistory/redoReappliesWhatUndoRemoved") = []
{
    auto history = EditHistory {};
    auto text = std::string {};

    for (const auto& edit: {insertion(0, "h"), insertion(1, "i")})
    {
        applyTo(text, edit);
        history.record(edit);
    }

    for (const auto& edit: history.undo())
        applyTo(text, edit);

    check(text.empty());
    check(history.canRedo());

    for (const auto& edit: history.redo())
        applyTo(text, edit);

    check(text == "hi");
};

// A new edit after an undo discards the redo branch: the future it described no
// longer follows from the present.
auto tNewEditClearsRedo = test("EditHistory/editingAfterUndoDropsTheRedoBranch") = []
{
    auto history = EditHistory {};

    history.record(insertion(0, "a"));
    history.undo();

    check(history.canRedo());

    history.record(insertion(0, "z"));

    check(!history.canRedo());
};

// Typing after an undo must not reopen the step that was just undone.
auto tUndoClosesTheStep = test("EditHistory/typingAfterUndoStartsAFreshStep") = []
{
    auto history = EditHistory {};

    history.record(insertion(0, "a"));
    history.record(insertion(1, "b"));
    check(history.undoDepth() == 1);

    history.undo();
    history.record(insertion(0, "x"));
    history.record(insertion(1, "y"));

    check(history.undoDepth() == 1);
    check(!history.canRedo());
};

auto tUndoOnEmptyHistoryIsSafe =
    test("EditHistory/undoAndRedoOnEmptyHistoryDoNothing") = []
{
    auto history = EditHistory {};

    check(history.undo().empty());
    check(history.redo().empty());
    check(!history.canUndo());
    check(!history.canRedo());
};

auto tClearResetsEverything = test("EditHistory/clearResetsBothStacks") = []
{
    auto history = EditHistory {};

    history.record(insertion(0, "a"));
    history.undo();
    history.clear();

    check(!history.canUndo());
    check(!history.canRedo());
};

// Several steps undo and redo in order, ending back where they started. The
// property that matters over a long session.
auto tRoundTripsManySteps = test("EditHistory/manyStepsRoundTrip") = []
{
    auto history = EditHistory {};
    auto text = std::string {};

    const TextEdit edits[] = {insertion(0, "one"),
                              insertion(3, "\n"),
                              insertion(4, "two"),
                              insertion(7, "\n"),
                              insertion(8, "three")};

    for (const auto& edit: edits)
    {
        applyTo(text, edit);
        history.record(edit);
    }

    const auto original = text;
    const auto steps = history.undoDepth();

    check(steps > 1);

    while (history.canUndo())
        for (const auto& edit: history.undo())
            applyTo(text, edit);

    check(text.empty());

    while (history.canRedo())
        for (const auto& edit: history.redo())
            applyTo(text, edit);

    check(text == original);
    check(history.undoDepth() == steps);
};
