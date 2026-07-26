#include "Common.h"

#include <ECodeCore/Editor.h>

// More than one cursor: the set that holds them, and every editing operation
// applied at all of them at once.
//
// The properties worth pinning here are the ones that fail *silently*. Two
// cursors that overlap type every character twice; two out of order make the
// edit at one shift the other by a stale amount; a keystroke at N cursors that
// lands on the undo stack as N steps takes N presses of ⌘Z to take back. None
// of those look like a crash — the text simply comes out wrong — so most of
// these tests are chosen for inputs where a plausible wrong implementation
// gives a *different* answer rather than no answer.

using namespace nano;
using namespace ecode;

namespace
{
Editor editorWith(std::string text)
{
    return Editor {Document::fromText(std::move(text))};
}

Cursor caretAt(std::size_t offset)
{
    auto caret = Cursor {};
    caret.moveTo(offset);

    return caret;
}

Cursor selection(std::size_t anchor, std::size_t head)
{
    auto caret = Cursor {};
    caret.anchor = anchor;
    caret.head = head;

    return caret;
}

// Every cursor's head, in document order, which is what most of these assert on.
std::vector<std::size_t> heads(const Editor& editor)
{
    auto found = std::vector<std::size_t> {};

    for (const auto& caret: editor.cursors())
        found.push_back(caret.head);

    return found;
}
} // namespace

// --- the set ----------------------------------------------------------------

auto tStartsWithOne = test("CursorSet/startsWithExactlyOneCursor") = []
{
    const auto set = CursorSet {};

    check(set.count() == 1);
    check(!set.hasMultiple());
    check(set.primary().head == 0);
};

// Out of order in, in order out. Everything downstream — the running shift an
// edit accumulates, the renderer's one-line-at-a-time band — reads the set as
// sorted, and a set that only happened to be sorted because the tests added
// cursors in order would let all of them pass over a broken sort.
auto tSortsOnAdd = test("CursorSet/addingOutOfOrderStillComesBackSorted") = []
{
    auto set = CursorSet {};

    set.reset(caretAt(20));

    check(set.add(caretAt(5)));
    check(set.add(caretAt(12)));

    check(set.count() == 3);
    check(set[0].head == 5);
    check(set[1].head == 12);
    check(set[2].head == 20);
};

// The last one added is the primary, and it stays so after the sort puts it
// somewhere else. That is the whole reason the flag travels with the cursor
// rather than being an index: ⌥-clicking above the caret and then pressing ⌘F
// has to search from the click.
auto tPrimarySurvivesTheSort =
    test("CursorSet/theCursorJustAddedIsPrimaryWhereverItSorts") = []
{
    auto set = CursorSet {};

    set.reset(caretAt(20));
    set.add(caretAt(5));

    check(set.primary().head == 5);
    check(set[0].head == 5);
};

// Two carets in one place are one caret. Left alone they insert every character
// twice at the same offset, which reads as a stutter in the typing rather than
// as two cursors.
auto tMergesIdenticalCarets = test("CursorSet/twoCaretsInOnePlaceBecomeOne") = []
{
    auto set = CursorSet {};

    set.reset(caretAt(7));

    check(!set.add(caretAt(7)));
    check(set.count() == 1);
};

// Touching counts as overlapping: [0,4) and [4,8) describe one run of text.
// Chosen over an ordinary overlap because a merge rule written as a strict `<`
// passes every overlap test and leaves exactly this case behind — and the seam
// is where the double insertion would land.
auto tMergesTouchingSelections =
    test("CursorSet/twoSelectionsThatTouchBecomeOne") = []
{
    auto set = CursorSet {};

    set.reset(selection(0, 4));
    set.add(selection(4, 8));

    check(set.count() == 1);
    check(set[0].start() == 0);
    check(set[0].end() == 8);
};

auto tMergesOverlappingSelections =
    test("CursorSet/twoSelectionsThatOverlapBecomeTheirUnion") = []
{
    auto set = CursorSet {};

    set.reset(selection(0, 6));
    set.add(selection(4, 12));

    check(set.count() == 1);
    check(set[0].start() == 0);
    check(set[0].end() == 12);
};

// A merge keeps the direction of whichever cursor was the primary, because the
// head is the end a following Shift+Arrow moves. Backwards is the case worth
// testing: forwards is also what a naive `head = end` would produce.
auto tMergeKeepsThePrimarysDirection =
    test("CursorSet/mergingKeepsTheDirectionOfThePrimary") = []
{
    auto set = CursorSet {};

    set.reset(selection(0, 6));
    set.add(selection(12, 4)); // reversed: head at 4, anchor at 12

    check(set.count() == 1);
    check(set.primary().isReversed());
    check(set.primary().head == 0);
    check(set.primary().anchor == 12);
};

auto tRemovesTheCursorUnderAPoint =
    test("CursorSet/removingTakesAwayTheCursorCoveringThatOffset") = []
{
    auto set = CursorSet {};

    set.reset(caretAt(3));
    set.add(caretAt(9));

    check(set.removeCovering(3));
    check(set.count() == 1);
    check(set[0].head == 9);
};

// The set is never empty. A ⌥-click that emptied it would leave a window with
// no caret and nothing to type into, and every caller downstream is written
// against "the primary" existing.
auto tRefusesToRemoveTheLast = test("CursorSet/theLastCursorCannotBeRemoved") = []
{
    auto set = CursorSet {};

    set.reset(caretAt(4));

    check(!set.removeCovering(4));
    check(set.count() == 1);
};

// A bare caret is found at its own offset. Under the half-open rule a
// zero-length range contains nothing, so ⌥-clicking a caret to take it away
// again would find nothing to remove and add a second one in the same place —
// which the merge then swallows, leaving the click doing nothing at all.
auto tACaretIsFoundAtItsOwnOffset =
    test("CursorSet/aBareCaretIsCoveredAtItsOwnOffset") = []
{
    auto set = CursorSet {};

    set.reset(caretAt(0));
    set.add(caretAt(5));

    check(set.indexCovering(5) == 1);
    check(set.indexCovering(4) < 0);
};

// --- editing at every cursor ------------------------------------------------

// The running-shift test, and the numbers are the test. Three cursors and a
// two-character insertion: an implementation that forgets to shift the cursors
// below an edit writes at 0, 4 and 8 rather than at 0, 6 and 12, and every
// character after the first lands in the wrong word. A one-character insertion
// at two cursors would be off by only one and could still look plausible.
auto tInsertsAtEveryCursor = test("MultiCursor/typingLandsAtEveryCursor") = []
{
    auto editor = editorWith("one two three");

    editor.placeCaret(0);
    editor.toggleCursorAt(4);
    editor.toggleCursorAt(8);

    editor.insert("<>");

    check(editor.document().text() == "<>one <>two <>three");
    check(heads(editor) == std::vector<std::size_t> {2, 8, 14});
};

auto tTypingReplacesEverySelection =
    test("MultiCursor/typingReplacesEverySelection") = []
{
    auto editor = editorWith("aaa bbb ccc");

    editor.placeCaret(0);
    editor.selectNextOccurrence(); // selects "aaa"

    editor.insert("x");

    check(editor.document().text() == "x bbb ccc");

    editor.placeCaret(2);
    editor.selectNextOccurrence(); // "bbb"
    editor.selectNextOccurrence(); // and nothing else matches

    check(editor.cursors().count() == 1);
};

// Backspace at N cursors deletes N characters, one per cursor, and the two
// cursors that end up in the same place become one.
auto tBackspaceAtEveryCursor =
    test("MultiCursor/backspaceDeletesOneCharacterPerCursor") = []
{
    auto editor = editorWith("abcdef");

    editor.placeCaret(2);
    editor.toggleCursorAt(4);
    editor.toggleCursorAt(6);

    editor.backspace();

    check(editor.document().text() == "ace");
    check(heads(editor) == std::vector<std::size_t> {1, 2, 3});
};

// The case the shift bookkeeping gets wrong when it is only applied to cursors
// that actually edit: the last cursor is at the end of the file and has nothing
// to forward-delete, but the cursor before it has just shortened the document
// underneath it. Left unshifted it ends up pointing past the end.
auto tACursorThatDeletesNothingStillMoves =
    test("MultiCursor/aCursorWithNothingToDeleteStillFollowsTheText") = []
{
    auto editor = editorWith("ab");

    editor.placeCaret(0);
    editor.toggleCursorAt(2);

    editor.deleteForward();

    check(editor.document().text() == "b");

    for (const auto& caret: editor.cursors())
        check(caret.head <= editor.document().length());

    check(heads(editor) == std::vector<std::size_t> {0, 1});
};

auto tWordDeleteAtEveryCursor =
    test("MultiCursor/wordDeleteAppliesAtEveryCursor") = []
{
    auto editor = editorWith("alpha beta gamma");

    editor.placeCaret(5); // after "alpha"
    editor.toggleCursorAt(10); // after "beta"

    editor.deleteWordBefore();

    check(editor.document().text() == "  gamma");
};

// --- undo -------------------------------------------------------------------

// One keystroke at three cursors is one thing to undo. Without the group each
// insertion lands on the stack separately — the merge rule only ever joins
// insertions that continue where the last one ended, and these do not — so it
// would take three presses of ⌘Z to take back one keystroke.
auto tOneKeystrokeIsOneUndo =
    test("MultiCursor/aKeystrokeAtThreeCursorsUndoesInOnePress") = []
{
    auto editor = editorWith("one two three");

    editor.placeCaret(0);
    editor.toggleCursorAt(4);
    editor.toggleCursorAt(8);

    editor.insert("x");
    editor.undo();

    check(editor.document().text() == "one two three");
};

// The other half of that, and the reason the grouping is conditional: with one
// cursor a typed word still has to undo as a word. beginGroup ends the open
// step, so grouping unconditionally would put every letter on the stack
// separately — the opposite bug, and equally invisible until someone presses
// ⌘Z.
auto tSingleCursorTypingStillMerges =
    test("MultiCursor/oneCursorTypingAWordStillUndoesAsAWord") = []
{
    auto editor = editorWith("");

    editor.insert("h");
    editor.insert("e");
    editor.insert("y");

    check(editor.document().text() == "hey");

    editor.undo();

    check(editor.document().text().empty());
};

// Back to one cursor, whatever there were. The alternative worth ruling out is
// a cursor per edit in the undone step, which is right for a multi-cursor
// keystroke and wrong for the other kind of grouped edit — undoing a
// replace-all would leave a cursor on every occurrence in the file.
auto tUndoCollapsesTheSet = test("MultiCursor/undoLeavesOneCursor") = []
{
    auto editor = editorWith("one two three");

    editor.placeCaret(0);
    editor.toggleCursorAt(4);

    editor.insert("x");

    check(editor.cursors().count() == 2);

    editor.undo();

    check(editor.cursors().count() == 1);
};

// --- movement ---------------------------------------------------------------

auto tMovementAppliesToEveryCursor =
    test("MultiCursor/everyCursorMovesTogether") = []
{
    auto editor = editorWith("abcdef");

    editor.placeCaret(1);
    editor.toggleCursorAt(4);

    editor.moveRight();

    check(heads(editor) == std::vector<std::size_t> {2, 5});
};

// Two cursors that walk into each other stop being two. Chosen with them one
// apart so a single press is enough — anything wider would pass against a
// merge rule that only fires on an exact overlap after several presses, and it
// is the *first* collision that has to be caught.
auto tCursorsMergeWhenTheyCollide =
    test("MultiCursor/twoCursorsThatWalkIntoEachOtherBecomeOne") = []
{
    auto editor = editorWith("abcdef");

    editor.placeCaret(2);
    editor.toggleCursorAt(3);

    check(editor.cursors().count() == 2);

    editor.moveLeft();

    check(editor.cursors().count() == 2); // 1 and 2, still apart

    editor.moveToDocumentStart();

    check(editor.cursors().count() == 1);
    check(editor.cursor().head == 0);
};

// A plain click is one caret again; a Shift+click is the gesture that grew from
// one, so it keeps the set and extends only the cursor it started at.
auto tClickCollapsesAndShiftClickDoesNot =
    test("MultiCursor/aPlainClickCollapsesAndAnExtendDoesNot") = []
{
    auto editor = editorWith("abcdef");

    editor.placeCaret(1);
    editor.toggleCursorAt(4);

    editor.placeCaret(3, true);

    check(editor.cursors().count() == 2);
    check(editor.cursor().hasSelection());

    editor.placeCaret(0);

    check(editor.cursors().count() == 1);
};

// --- adding cursors ---------------------------------------------------------

auto tAddsBelowAndAbove = test("MultiCursor/addsACursorOnTheRowBelow") = []
{
    auto editor = editorWith("alpha\nbeta\ngamma");

    editor.placeCaret(2); // line 0, column 2

    check(editor.addCursorBelow());
    check(editor.cursors().count() == 2);
    check(editor.document().lineAt(editor.cursor().head) == 1);
    check(editor.document().columnAt(editor.cursor().head) == 2);

    check(editor.addCursorBelow());
    check(editor.cursors().count() == 3);
    check(editor.document().lineAt(editor.cursor().head) == 2);
};

// Not below *each*: adding one under every cursor doubles the set on each
// press. Two rows cannot tell the two apart — both give two cursors from one
// press — so it takes three presses, which is 4 against 8.
auto tAddsBelowTheBottommostOnly =
    test("MultiCursor/holdingAddBelowGrowsAColumnRatherThanDoubling") = []
{
    auto editor = editorWith("a\nb\nc\nd\ne\nf\ng\nh");

    editor.placeCaret(0);

    editor.addCursorBelow();
    editor.addCursorBelow();
    editor.addCursorBelow();

    check(editor.cursors().count() == 4);
};

// Below the *bottommost*, which is a different claim, and one every test above
// is blind to: each add makes its own new cursor the primary, so in any
// sequence that only goes downwards the bottommost and the primary are the same
// cursor and both implementations agree. Found by mutation — "below the
// primary" left the whole suite green.
//
// It takes a ⌥-click, which is the one thing that makes the primary an *upper*
// cursor. Below the primary would land on line 2, where the existing cursor
// already is, and the two would merge — so the press would appear to do
// nothing at all.
auto tAddBelowIsRelativeToTheBottommost =
    test("MultiCursor/addBelowGoesUnderTheBottommostRatherThanThePrimary") = []
{
    auto editor = editorWith("a\nb\nc\nd\ne\nf");

    editor.placeCaret(editor.document().offsetAt(3, 0));
    editor.toggleCursorAt(editor.document().offsetAt(1, 0)); // now the primary

    check(editor.document().lineAt(editor.cursor().head) == 1);
    check(editor.addCursorBelow());
    check(editor.cursors().count() == 3);
    check(editor.document().lineAt(editor.cursors()[2].head) == 4);
};

auto tAddAboveIsRelativeToTheTopmost =
    test("MultiCursor/addAboveGoesOverTheTopmostRatherThanThePrimary") = []
{
    auto editor = editorWith("a\nb\nc\nd\ne\nf");

    editor.placeCaret(editor.document().offsetAt(1, 0));
    editor.toggleCursorAt(editor.document().offsetAt(3, 0)); // now the primary

    check(editor.document().lineAt(editor.cursor().head) == 3);
    check(editor.addCursorAbove());
    check(editor.cursors().count() == 3);
    check(editor.document().lineAt(editor.cursors()[0].head) == 0);
};

// The column is held across a short line, so a column of cursors dragged past
// one and onwards lines back up underneath itself. Without it the third cursor
// sits at the end of the short line — column 1 rather than column 4.
auto tAddBelowHoldsTheColumn =
    test("MultiCursor/addingDownwardsHoldsTheColumnPastAShortLine") = []
{
    auto editor = editorWith("aaaaaa\nb\ncccccc");

    editor.placeCaret(4); // line 0, column 4

    editor.addCursorBelow();
    editor.addCursorBelow();

    check(editor.cursors().count() == 3);
    check(editor.document().lineAt(editor.cursor().head) == 2);
    check(editor.document().columnAt(editor.cursor().head) == 4);
};

auto tAddAboveStopsAtTheTop =
    test("MultiCursor/addingAboveTheFirstRowDoesNothing") = []
{
    auto editor = editorWith("alpha\nbeta");

    editor.placeCaret(0);

    check(!editor.addCursorAbove());
    check(editor.cursors().count() == 1);
};

auto tToggleAddsThenRemoves =
    test("MultiCursor/altClickingTheSameSpotTwiceLeavesOneCursor") = []
{
    auto editor = editorWith("abcdef");

    editor.placeCaret(1);

    check(editor.toggleCursorAt(4));
    check(editor.cursors().count() == 2);

    check(editor.toggleCursorAt(4));
    check(editor.cursors().count() == 1);
    check(editor.cursor().head == 1);
};

auto tCollapseReportsWhetherItDidAnything =
    test("MultiCursor/collapsingReportsWhetherThereWasAnythingToCollapse") = []
{
    auto editor = editorWith("abcdef");

    editor.placeCaret(1);

    check(!editor.collapseCursors());

    editor.toggleCursorAt(4);

    check(editor.collapseCursors());
    check(editor.cursors().count() == 1);

    // The primary, which is the one ⌥-clicked rather than the one clicked.
    check(editor.cursor().head == 4);
};

// --- occurrences ------------------------------------------------------------

auto tFirstPressSelectsTheWord =
    test("MultiCursor/theFirstAddNextSelectsTheWordUnderTheCaret") = []
{
    auto editor = editorWith("value = other");

    editor.placeCaret(2);

    check(editor.selectNextOccurrence());
    check(editor.cursors().count() == 1);
    check(editor.selectedText() == "value");
};

auto tSecondPressAddsTheNextOccurrence =
    test("MultiCursor/theSecondAddNextPutsACursorOnTheNextMatch") = []
{
    auto editor = editorWith("one two one two one");

    editor.placeCaret(0);

    editor.selectNextOccurrence(); // selects the first "one"
    editor.selectNextOccurrence(); // adds the second

    check(editor.cursors().count() == 2);
    check(editor.cursors()[1].start() == 8);

    editor.selectNextOccurrence();

    check(editor.cursors().count() == 3);
};

// Past the end it wraps rather than stopping, which is what makes holding ⌘D
// reach the occurrences above where the person started.
auto tAddNextWraps = test("MultiCursor/addNextWrapsAtTheEndOfTheFile") = []
{
    auto editor = editorWith("one two one");

    editor.placeCaret(8); // the second "one"

    editor.selectNextOccurrence();
    editor.selectNextOccurrence();

    check(editor.cursors().count() == 2);
    check(editor.cursors()[0].start() == 0);
};

// ⌘D on a word matches whole words, so selecting `i` does not also catch the
// `i` inside every identifier. The input is chosen so the two rules disagree:
// substring matching finds four occurrences here and whole-word finds two.
auto tAddNextOnAWordMatchesWholeWords =
    test("MultiCursor/addNextOnAWordDoesNotMatchInsideOtherWords") = []
{
    auto editor = editorWith("id = idle + id + hybrid");

    editor.placeCaret(0);

    editor.selectNextOccurrence(); // selects "id"
    editor.selectNextOccurrence();

    check(editor.cursors().count() == 2);

    // The second "id", standing alone — not the "id" inside "idle" at 5.
    check(editor.cursors()[1].start() == 12);
};

// And an arbitrary selection is *not* whole-word: a selection nobody expanded
// from a word is a piece of text, and the person picked its boundaries.
auto tAddNextOnAFragmentMatchesAnywhere =
    test("MultiCursor/addNextOnAPartialSelectionMatchesInsideWords") = []
{
    auto editor = editorWith("idle + hybrid");

    editor.placeCaret(0);
    editor.placeCaret(2, true); // "id" out of "idle", not a whole word

    editor.selectNextOccurrence();

    check(editor.cursors().count() == 2);
    check(editor.cursors()[1].start() == 11); // inside "hybrid"
};

// Case-sensitive, because ⌘D is how a rename is done and renaming `Value` has
// no business also catching `value`.
auto tAddNextIsCaseSensitive =
    test("MultiCursor/addNextDoesNotMatchTheOtherCase") = []
{
    auto editor = editorWith("Value value Value");

    editor.placeCaret(0);

    editor.selectNextOccurrence();
    editor.selectNextOccurrence();

    check(editor.cursors().count() == 2);
    check(editor.cursors()[1].start() == 12);
};

auto tSelectAllOccurrences = test("MultiCursor/selectsEveryOccurrenceAtOnce") = []
{
    auto editor = editorWith("one two one two one");

    editor.placeCaret(0);
    editor.selectNextOccurrence(); // selects "one"

    check(editor.selectAllOccurrences());
    check(editor.cursors().count() == 3);
};

// And from a bare caret in one press, rather than needing ⌘D first. ⇧⌘L is one
// gesture; a version that only expanded the word would look like it had done
// nothing to anyone who had not also pressed ⌘D.
auto tSelectAllFromABareCaret =
    test("MultiCursor/selectsEveryOccurrenceFromACaretInOnePress") = []
{
    auto editor = editorWith("one two one two one");

    editor.placeCaret(1); // inside the first "one", nothing selected

    check(editor.selectAllOccurrences());
    check(editor.cursors().count() == 3);
    check(editor.selectedText() == "one\none\none");
};

// The primary stays on the occurrence the person was on. Otherwise the view
// scrolls to the last hit in the file the moment every hit is selected — which
// on a large file means losing sight of the work entirely.
auto tSelectAllKeepsThePrimaryWhereItWas =
    test("MultiCursor/selectingEveryOccurrenceLeavesThePrimaryWhereItWas") = []
{
    auto editor = editorWith("one two one two one");

    editor.placeCaret(8); // the second "one"
    editor.selectNextOccurrence();

    editor.selectAllOccurrences();

    check(editor.cursors().count() == 3);
    check(editor.cursor().start() == 8);
};

// --- what the clipboard sees ------------------------------------------------

// Every selection, joined. The primary's alone is the plausible wrong answer,
// and it silently drops everything else a person selected before pressing ⌘C.
auto tSelectedTextJoinsEverySelection =
    test("MultiCursor/copyingTakesEverySelectionInDocumentOrder") = []
{
    auto editor = editorWith("one two one");

    editor.placeCaret(0);
    editor.selectNextOccurrence();
    editor.selectNextOccurrence();

    check(editor.selectedText() == "one\none");
};

auto tSelectedTextIgnoresBareCarets =
    test("MultiCursor/copyingIgnoresCursorsWithNoSelection") = []
{
    auto editor = editorWith("abcdef");

    editor.placeCaret(0);
    editor.placeCaret(3, true); // "abc"
    editor.toggleCursorAt(5); // a bare caret

    check(editor.selectedText() == "abc");
};
