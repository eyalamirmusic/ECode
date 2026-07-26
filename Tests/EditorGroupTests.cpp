#include "Common.h"

#include <ECodeCore/EditorGroups.h>

#include <filesystem>
#include <fstream>

// Editor groups: several workspaces side by side, which one is active, and what
// happens to a file moved between them.
//
// Real files for the same reason WorkspaceTests uses them — the question these
// answer is about paths, and "is this file already open somewhere else" is one
// only a real path can settle.

using namespace nano;
using namespace ecode;

namespace
{
std::filesystem::path scratch(const std::string& name)
{
    auto dir = std::filesystem::temp_directory_path() / ("ecode-groups-" + name);

    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    return dir;
}

eacp::FilePath write(const std::filesystem::path& path, std::string_view contents)
{
    auto out = std::ofstream {path, std::ios::binary | std::ios::trunc};
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    out.close();

    return eacp::FilePath {path};
}

// Counts the edits it was told about, so a move can be checked to have carried
// the wiring rather than only the bytes.
struct CountingHighlighter final : Highlighter
{
    const LineStyle& lineStyle(std::size_t) override { return empty; }

    void applyEdit(const Document&, const TextEdit&) override { ++edits; }
    void reset() override { ++resets; }

    int edits = 0;
    int resets = 0;

    LineStyle empty;
};
} // namespace

// There is always a group, before anything has been split and after everything
// has been closed — the same invariant a Workspace holds over its tabs, and for
// the same reason: everything downstream is written against "the active file".
auto tStartsWithOneGroup = test("EditorGroups/startsWithOneGroupHoldingOneFile") = []
{
    auto groups = EditorGroups {};

    check(groups.count() == 1);
    check(groups.activeIndex() == 0);
    check(groups.active().count() == 1);
    check(groups.active().active().file.path().empty());
};

// A split makes an empty pane beside the current one and moves the work into
// it. Empty rather than a copy of the current file, because a file lives in
// exactly one group; see EditorGroups' header.
auto tSplitAddsAGroupAfterTheActive =
    test("EditorGroups/splitInsertsAfterTheActiveGroupAndActivatesIt") = []
{
    auto dir = scratch("split");
    const auto a = write(dir / "a.txt", "a\n");

    auto groups = EditorGroups {};

    check(groups.open(a));

    groups.split();

    check(groups.count() == 2);
    check(groups.activeIndex() == 1);

    // The group split from keeps its file; the new one starts empty.
    check(groups.at(0).at(0).file.path().str() == a.str());
    check(groups.at(1).active().file.path().empty());

    std::filesystem::remove_all(dir);
};

// A split from the middle lands beside the pane it came from rather than at the
// end, which is where the eye already is. Three groups is the smallest case
// that can tell "after the active" from "at the end" apart.
auto tSplitLandsBesideNotAtTheEnd =
    test("EditorGroups/splitFromTheMiddleLandsBesideTheActiveGroup") = []
{
    auto groups = EditorGroups {};

    groups.split();
    groups.split();

    check(groups.count() == 3);

    groups.activate(0);
    groups.active().editor().insert("first");

    groups.split();

    check(groups.count() == 4);
    check(groups.activeIndex() == 1);

    // The new group is empty and the two that were already to the right are
    // still to the right of it.
    check(groups.at(1).active().file.editor().document().text().empty());
    check(groups.at(0).active().file.editor().document().text() == "first");
};

// The rule the whole design turns on. Opening a file that some other group is
// already showing goes to it rather than making a second copy — two groups over
// one path would be two undo histories and two dirty flags over one set of
// bytes, which is the failure §7.8 rejected for tabs.
auto tOpenGoesToTheGroupThatHasIt =
    test("EditorGroups/openingAFileAlreadyOpenElsewhereGoesToThatGroup") = []
{
    auto dir = scratch("shared");
    const auto a = write(dir / "a.txt", "aaa\n");
    const auto b = write(dir / "b.txt", "bbb\n");

    auto groups = EditorGroups {};

    check(groups.open(a));

    groups.split();

    check(groups.open(b));
    check(groups.activeIndex() == 1);

    // Back to the file the other group holds.
    check(groups.open(a));

    check(groups.count() == 2);
    check(groups.activeIndex() == 0);

    // And exactly one copy of it, so the tab count in each group is unchanged.
    check(groups.at(0).count() == 1);
    check(groups.at(1).count() == 1);
    check(groups.groupOf(a) == 0);
    check(groups.groupOf(b) == 1);

    std::filesystem::remove_all(dir);
};

auto tGroupOfIsMinusOneForAnUnopenedFile =
    test("EditorGroups/groupOfAnUnopenedFileIsMinusOne") = []
{
    auto dir = scratch("unopened");
    const auto a = write(dir / "a.txt", "a\n");

    auto groups = EditorGroups {};

    check(groups.groupOf(a) == -1);

    std::filesystem::remove_all(dir);
};

// The gesture that fills a split. What it must not do is rebuild the file on
// the other side: the address is the test, because an OpenFile that changes
// address takes its Editor with it and the callbacks the workspace installed
// captured that editor by pointer.
auto tMovePreservesTheFileItself =
    test("EditorGroups/movingAFileCarriesTheSameObjectAcross") = []
{
    auto dir = scratch("move");
    const auto a = write(dir / "a.txt", "aaa\n");
    const auto b = write(dir / "b.txt", "bbb\n");

    auto groups = EditorGroups {};

    check(groups.open(a));
    check(groups.open(b));

    groups.split();
    groups.activate(0);
    groups.at(0).activate(1);

    const auto* const moved = &groups.at(0).at(1);
    const auto* const highlighter = groups.at(0).at(1).highlighter.get();

    check(groups.moveActiveFile(1));

    check(groups.count() == 2);
    check(groups.activeIndex() == 1);
    check(groups.at(0).count() == 1);
    check(groups.at(1).count() == 1);

    check(&groups.at(1).at(0) == moved);
    check(groups.at(1).at(0).highlighter.get() == highlighter);
    check(groups.at(1).at(0).file.path().str() == b.str());

    std::filesystem::remove_all(dir);
};

// The half an address comparison cannot see. A file rebuilt on the far side
// would arrive with an empty undo stack, and one whose editor moved would leave
// the workspace's onEdit lambda pointing at freed memory — the first is silent,
// the second is a crash on the next keystroke.
auto tMoveKeepsUndoAndWiring =
    test("EditorGroups/aMovedFileKeepsItsHistoryAndItsHighlighter") = []
{
    auto dir = scratch("wiring");
    const auto a = write(dir / "a.txt", "aaa\n");
    const auto b = write(dir / "b.txt", "bbb\n");

    auto groups = EditorGroups {
        []
        { return eacp::OwningPointer<Highlighter> {new CountingHighlighter {}}; }};

    check(groups.open(a));
    check(groups.open(b));

    groups.at(0).activate(1);
    groups.at(0).editor().insert("typed");

    const auto before =
        static_cast<CountingHighlighter&>(*groups.at(0).at(1).highlighter).edits;

    check(before == 1);

    groups.split();
    groups.activate(0);
    groups.at(0).activate(1);

    check(groups.moveActiveFile(1));

    auto& editor = groups.at(1).at(0).file.editor();

    // The history came with it.
    check(editor.canUndo());

    editor.insert("more");

    // And so did the wiring, into the same highlighter rather than a new one.
    check(static_cast<CountingHighlighter&>(*groups.at(1).at(0).highlighter).edits
          == before + 1);

    editor.undo();
    editor.undo();

    check(editor.document().text() == "bbb\n");

    std::filesystem::remove_all(dir);
};

// Off the end makes a group. The alternative — refusing — leaves "put this file
// beside that one" needing a split first and a move second, for one gesture.
auto tMovePastTheEndMakesAGroup =
    test("EditorGroups/movingPastTheLastGroupMakesOne") = []
{
    auto dir = scratch("past-end");
    const auto a = write(dir / "a.txt", "aaa\n");
    const auto b = write(dir / "b.txt", "bbb\n");

    auto groups = EditorGroups {};

    check(groups.open(a));
    check(groups.open(b));

    check(groups.count() == 1);
    check(groups.moveActiveFile(1));

    check(groups.count() == 2);
    check(groups.activeIndex() == 1);
    check(groups.at(0).count() == 1);
    check(groups.at(1).count() == 1);
    check(groups.at(1).active().file.path().str() == b.str());

    std::filesystem::remove_all(dir);
};

// Backwards off the front makes one too, and the active index has to be fixed
// up for it — every group shifted right by one when it was inserted. A missing
// fixup lands the move in the wrong pane and reads as the file having gone to
// the other side.
auto tMoveBeforeTheFirstGroupMakesOne =
    test("EditorGroups/movingBeforeTheFirstGroupMakesOneAndFixesTheIndex") = []
{
    auto dir = scratch("past-front");
    const auto a = write(dir / "a.txt", "aaa\n");
    const auto b = write(dir / "b.txt", "bbb\n");

    auto groups = EditorGroups {};

    check(groups.open(a));
    check(groups.open(b));

    check(groups.moveActiveFile(-1));

    check(groups.count() == 2);
    check(groups.activeIndex() == 0);

    // The moved file is on the left and what it left behind is on the right.
    check(groups.at(0).count() == 1);
    check(groups.at(0).active().file.path().str() == b.str());
    check(groups.at(1).count() == 1);
    check(groups.at(1).active().file.path().str() == a.str());

    std::filesystem::remove_all(dir);
};

// The only file in its group has nowhere to go: a new group, an emptied one
// behind it, and that one closing leaves exactly the arrangement it started
// from. Refused rather than performed, so the chord does nothing visible
// instead of appearing to do something and undoing itself.
auto tMoveRefusesWhenItWouldChangeNothing =
    test("EditorGroups/movingTheOnlyFileIntoANewGroupIsRefused") = []
{
    auto dir = scratch("only-file");
    const auto a = write(dir / "a.txt", "aaa\n");

    auto groups = EditorGroups {};

    check(groups.open(a));

    check(!groups.moveActiveFile(1));
    check(!groups.moveActiveFile(-1));

    check(groups.count() == 1);
    check(groups.at(0).count() == 1);

    std::filesystem::remove_all(dir);
};

// Moving the last file *out of* a group into one that already exists is a
// different case, and there the emptied group does close.
auto tMoveClosesTheGroupItEmptied =
    test("EditorGroups/movingTheLastFileOutOfAGroupClosesIt") = []
{
    auto dir = scratch("empties");
    const auto a = write(dir / "a.txt", "aaa\n");
    const auto b = write(dir / "b.txt", "bbb\n");

    auto groups = EditorGroups {};

    check(groups.open(a));

    groups.split();

    check(groups.open(b));
    check(groups.count() == 2);
    check(groups.activeIndex() == 1);

    // b is alone in group 1; moving it back onto a leaves group 1 with nothing.
    check(groups.moveActiveFile(-1));

    check(groups.count() == 1);
    check(groups.activeIndex() == 0);
    check(groups.at(0).count() == 2);

    std::filesystem::remove_all(dir);
};

// The fixup the test above cannot see, and the case that names the state it
// needs. Closing the emptied group renumbers everything after it, so the group
// the file just landed in is no longer at the index it was aimed at — and the
// mistake is invisible with two groups, because removing group 0 of 2 clamps
// the active index onto the survivor whether or not anything corrected it.
// It takes three groups, a *forward* move, and a source that is neither the
// last group nor the one being moved into: the file then arrives in the middle
// group while focus lands on the one beyond it.
auto tMoveForwardOutOfAnEmptiedGroupKeepsFocusOnTheFile =
    test("EditorGroups/movingForwardOutOfAnEmptiedGroupFollowsTheFile") = []
{
    auto dir = scratch("renumber");
    const auto a = write(dir / "a.txt", "aaa\n");
    const auto b = write(dir / "b.txt", "bbb\n");
    const auto c = write(dir / "c.txt", "ccc\n");

    auto groups = EditorGroups {};

    check(groups.open(a));

    groups.split();
    check(groups.open(b));

    groups.split();
    check(groups.open(c));

    check(groups.count() == 3);

    groups.activate(0);

    check(groups.moveActiveFile(1));

    // Group 0 emptied and closed, so what was group 1 is now group 0 — and that
    // is where the file went, so that is where focus has to be.
    check(groups.count() == 2);
    check(groups.activeIndex() == 0);
    check(groups.active().active().file.path().str() == a.str());

    check(groups.at(0).count() == 2);
    check(groups.at(1).count() == 1);
    check(groups.at(1).active().file.path().str() == c.str());

    std::filesystem::remove_all(dir);
};

// ⌘W until the pane is gone. The alternative leaves a blank group with an
// Untitled in it, which is a pane nobody asked for and no obvious way to be rid
// of.
auto tClosingTheLastFileClosesTheGroup =
    test("EditorGroups/closingTheLastFileInAGroupClosesTheGroup") = []
{
    auto dir = scratch("close-group");
    const auto a = write(dir / "a.txt", "aaa\n");
    const auto b = write(dir / "b.txt", "bbb\n");

    auto groups = EditorGroups {};

    check(groups.open(a));

    groups.split();

    check(groups.open(b));
    check(groups.count() == 2);

    check(groups.closeFile(1, 0) == CloseResult::closed);

    check(groups.count() == 1);
    check(groups.activeIndex() == 0);
    check(groups.at(0).count() == 1);
    check(groups.at(0).active().file.path().str() == a.str());

    std::filesystem::remove_all(dir);
};

// Except the last group, which keeps the untitled buffer exactly as a lone
// Workspace does. A window with no pane is a window with no caret.
auto tClosingTheLastFileOfTheLastGroupLeavesABuffer =
    test("EditorGroups/theLastGroupNeverCloses") = []
{
    auto dir = scratch("last-group");
    const auto a = write(dir / "a.txt", "aaa\n");

    auto groups = EditorGroups {};

    check(groups.open(a));
    check(groups.closeFile(0, 0) == CloseResult::closed);

    check(groups.count() == 1);
    check(groups.at(0).count() == 1);
    check(groups.at(0).active().file.path().empty());

    std::filesystem::remove_all(dir);
};

// Refused rather than discarded, the same way a Workspace refuses — and the
// group has to survive the refusal, or a ⌘W that meant "ask me" would take the
// pane and leave the file in it unreachable.
auto tClosingADirtyFileIsRefused =
    test("EditorGroups/closingADirtyFileRefusesAndKeepsTheGroup") = []
{
    auto dir = scratch("dirty");
    const auto a = write(dir / "a.txt", "aaa\n");

    auto groups = EditorGroups {};

    check(groups.open(a));

    groups.split();
    groups.active().editor().insert("typed");

    check(groups.closeFile(1, 0) == CloseResult::hasUnsavedChanges);
    check(groups.count() == 2);

    groups.closeFileDiscarding(1, 0);

    check(groups.count() == 1);

    std::filesystem::remove_all(dir);
};

// A "quit anyway?" has to ask about every pane, not the one being looked at.
auto tUnsavedIsAskedOfEveryGroup =
    test("EditorGroups/unsavedChangesAreFoundInAnyGroup") = []
{
    auto groups = EditorGroups {};

    check(!groups.hasUnsavedChanges());

    groups.split();
    groups.active().editor().insert("typed");

    groups.activate(0);

    check(groups.hasUnsavedChanges());
};

auto tGroupCyclingWraps = test("EditorGroups/nextAndPreviousWrapAtBothEnds") = []
{
    auto groups = EditorGroups {};

    groups.split();
    groups.split();

    check(groups.count() == 3);

    groups.activate(2);
    groups.activateNext();

    check(groups.activeIndex() == 0);

    groups.activatePrevious();

    check(groups.activeIndex() == 2);
};

// The window lays out one pane per group, so it has to be told when there is a
// different number of them — and *not* told when only the tabs moved, or every
// tab switch would rebuild the whole widget tree.
auto tGroupChangesAreAnnouncedSeparately =
    test("EditorGroups/onlyAddingOrRemovingAGroupAnnouncesIt") = []
{
    auto groups = EditorGroups {};

    auto structural = 0;
    auto changes = 0;

    groups.onGroupsChanged = [&structural] { ++structural; };
    groups.onChanged = [&changes] { ++changes; };

    groups.split();

    check(structural == 1);
    check(changes >= 1);

    const auto afterSplit = changes;

    groups.activate(0);

    check(structural == 1);
    check(changes > afterSplit);

    groups.activate(1);
    groups.closeFileDiscarding(1, 0);

    check(structural == 2);
    check(groups.count() == 1);
};

// A group made by a split colours its files like the one it came from. The
// factory is a constructor argument for the reason Workspace's is: a group
// created later would otherwise be permanently plain, and a plain file looks
// exactly like a language with no grammar.
auto tSplitGroupsGetHighlighters =
    test("EditorGroups/aGroupMadeByASplitStillGetsHighlighters") = []
{
    auto made = 0;

    auto groups = EditorGroups {
        [&made]
        {
            ++made;

            return eacp::OwningPointer<Highlighter> {new CountingHighlighter {}};
        }};

    check(made == 1);

    groups.split();

    check(made == 2);
    check(groups.at(1).active().highlighter.get() != nullptr);
    check(groups.at(0).active().highlighter.get()
          != groups.at(1).active().highlighter.get());
};
