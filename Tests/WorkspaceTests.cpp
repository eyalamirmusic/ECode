#include "Common.h"

#include <ECodeCore/Workspace.h>

#include <filesystem>
#include <fstream>

// More than one file open at once: which tabs exist, which is active, and what
// each of them keeps to itself.
//
// These touch the real filesystem for the same reason TextFileTests does — the
// subject is files, and opening the same one by two spellings is a question only
// a real path can answer.

using namespace nano;
using namespace ecode;

namespace
{
std::filesystem::path scratch(const std::string& name)
{
    auto dir = std::filesystem::temp_directory_path() / ("ecode-workspace-" + name);

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

// A highlighter that does nothing but count, so the factory can be checked
// without ECodeSyntax — which ECodeCore deliberately cannot see.
struct CountingHighlighter final : Highlighter
{
    explicit CountingHighlighter(int& counter)
        : made(counter)
    {
        ++made;
    }

    const LineStyle& lineStyle(std::size_t) override { return empty; }

    void applyEdit(const Document&, const TextEdit&) override { ++edits; }
    void reset() override { ++resets; }

    int& made;
    int edits = 0;
    int resets = 0;

    LineStyle empty;
};
} // namespace

// There is always something to look at, before anything has been opened and
// after everything has been closed. Every caller downstream is written against
// "the active file"; an absent one is a null check at each of them.
auto tStartsWithOneFile = test("Workspace/startsWithOneUntitledFile") = []
{
    auto workspace = Workspace {};

    check(workspace.count() == 1);
    check(workspace.activeIndex() == 0);
    check(workspace.active().file.path().empty());
    check(!workspace.active().file.isDirty());
};

// The launch case. Opening into a workspace whose only tab is an untouched
// untitled buffer reuses it, or every launch would show a dead "Untitled" tab
// beside the file that was actually asked for.
auto tOpenReplacesTheScratchTab =
    test("Workspace/openingReplacesAnUntouchedUntitledTab") = []
{
    auto dir = scratch("scratch");
    const auto path = write(dir / "a.txt", "hello\n");

    auto workspace = Workspace {};

    check(workspace.open(path));
    check(workspace.count() == 1);
    check(workspace.active().file.name() == "a.txt");

    std::filesystem::remove_all(dir);
};

// ...but an untitled buffer with work in it is a file, and opening beside it
// must not throw that work away.
auto tOpenKeepsATypedInUntitledTab =
    test("Workspace/openingKeepsAnUntitledTabThatHasBeenTypedIn") = []
{
    auto dir = scratch("typed");
    const auto path = write(dir / "a.txt", "hello\n");

    auto workspace = Workspace {};
    workspace.editor().insert("scratch work");

    check(workspace.open(path));
    check(workspace.count() == 2);
    check(workspace.at(0).file.document().text() == "scratch work");
    check(workspace.at(1).file.name() == "a.txt");

    std::filesystem::remove_all(dir);
};

auto tOpeningSeveralFiles =
    test("Workspace/openingSeveralFilesMakesSeveralTabs") = []
{
    auto dir = scratch("several");
    const auto a = write(dir / "a.txt", "a\n");
    const auto b = write(dir / "b.txt", "b\n");
    const auto c = write(dir / "c.txt", "c\n");

    auto workspace = Workspace {};

    check(workspace.open(a));
    check(workspace.open(b));
    check(workspace.open(c));

    check(workspace.count() == 3);

    // Each opens beside the one it was opened from, and becomes active.
    check(workspace.activeIndex() == 2);
    check(workspace.at(0).file.name() == "a.txt");
    check(workspace.at(1).file.name() == "b.txt");
    check(workspace.at(2).file.name() == "c.txt");

    std::filesystem::remove_all(dir);
};

// Two tabs over one path would be two undo histories and two dirty flags over
// one set of bytes, and whichever saved last would win silently.
auto tOpeningTwiceActivates = test("Workspace/openingAnOpenFileActivatesItsTab") = []
{
    auto dir = scratch("twice");
    const auto a = write(dir / "a.txt", "a\n");
    const auto b = write(dir / "b.txt", "b\n");

    auto workspace = Workspace {};

    workspace.open(a);
    workspace.open(b);

    check(workspace.activeIndex() == 1);

    check(workspace.open(a));
    check(workspace.count() == 2);
    check(workspace.activeIndex() == 0);

    std::filesystem::remove_all(dir);
};

// The case a plain string comparison gets wrong. "dir/sub/../a.txt" and
// "dir/a.txt" are the same file, and opening one after the other must not make
// a second tab — the two would then diverge and the last save would win.
auto tPathsAreResolved = test("Workspace/twoSpellingsOfOnePathAreOneTab") = []
{
    auto dir = scratch("spellings");
    std::filesystem::create_directories(dir / "sub");

    const auto direct = write(dir / "a.txt", "a\n");
    const auto roundabout = eacp::FilePath {dir / "sub" / ".." / "a.txt"};

    auto workspace = Workspace {};

    check(workspace.open(direct));
    check(workspace.open(roundabout));
    check(workspace.count() == 1);

    std::filesystem::remove_all(dir);
};

// A file that cannot be read has to leave the workspace exactly as it was.
// Opening the tab first and finding out afterwards would leave an empty one.
auto tFailedOpenChangesNothing =
    test("Workspace/openingAMissingFileChangesNothing") = []
{
    auto dir = scratch("missing");
    const auto a = write(dir / "a.txt", "a\n");

    auto workspace = Workspace {};
    workspace.open(a);

    check(!workspace.open(eacp::FilePath {dir / "nope.txt"}));
    check(workspace.count() == 1);
    check(workspace.active().file.name() == "a.txt");

    std::filesystem::remove_all(dir);
};

// Closing a tab before the active one leaves the same file active. This is why
// the index is stepped down rather than only clamped: a clamp keeps the
// *number*, and after a removal the number names a different file.
//
// Four tabs and the third active, because three cannot tell the two apart:
// close the first of three with the last active and the clamp lands on the
// right file by accident, which is exactly the arrangement the first version of
// this test used and the reason it passed against the bug.
auto tClosingBeforeKeepsTheSameFileActive =
    test("Workspace/closingATabBeforeTheActiveOneKeepsIt") = []
{
    auto dir = scratch("closeBefore");
    const auto a = write(dir / "a.txt", "a\n");
    const auto b = write(dir / "b.txt", "b\n");
    const auto c = write(dir / "c.txt", "c\n");
    const auto d = write(dir / "d.txt", "d\n");

    auto workspace = Workspace {};

    workspace.open(a);
    workspace.open(b);
    workspace.open(c);
    workspace.open(d);

    workspace.activate(2);

    check(workspace.active().file.name() == "c.txt");

    check(workspace.close(0) == CloseResult::closed);

    check(workspace.count() == 3);
    check(workspace.activeIndex() == 1);
    check(workspace.active().file.name() == "c.txt");

    std::filesystem::remove_all(dir);
};

// Closing the last tab lands on its neighbour to the left, which is where the
// eye already is.
auto tClosingTheLastTabStepsLeft =
    test("Workspace/closingTheLastTabActivatesTheOneLeft") = []
{
    auto dir = scratch("closeLast");
    const auto a = write(dir / "a.txt", "a\n");
    const auto b = write(dir / "b.txt", "b\n");

    auto workspace = Workspace {};

    workspace.open(a);
    workspace.open(b);

    check(workspace.close(1) == CloseResult::closed);
    check(workspace.count() == 1);
    check(workspace.active().file.name() == "a.txt");

    std::filesystem::remove_all(dir);
};

// Never empty, so there is always a caret and always something to type in.
auto tClosingEverythingLeavesUntitled =
    test("Workspace/closingTheOnlyTabLeavesAnUntitledOne") = []
{
    auto dir = scratch("closeOnly");
    const auto a = write(dir / "a.txt", "a\n");

    auto workspace = Workspace {};
    workspace.open(a);

    check(workspace.close(0) == CloseResult::closed);

    check(workspace.count() == 1);
    check(workspace.active().file.path().empty());
    check(workspace.active().file.document().text().empty());

    std::filesystem::remove_all(dir);
};

// Refused rather than discarded, the same way TextFile::save refuses to clobber
// someone else's write. Aimed at the expensive direction: closing anyway loses
// the work, and refusing costs one more keystroke.
auto tClosingDirtyIsRefused =
    test("Workspace/closingAFileWithUnsavedEditsIsRefused") = []
{
    auto dir = scratch("dirty");
    const auto a = write(dir / "a.txt", "a\n");

    auto workspace = Workspace {};
    workspace.open(a);
    workspace.editor().insert("more");

    check(workspace.close(0) == CloseResult::hasUnsavedChanges);
    check(workspace.count() == 1);
    check(workspace.active().file.isDirty());

    // And the second answer takes it.
    workspace.closeDiscarding(0);

    check(workspace.active().file.path().empty());

    std::filesystem::remove_all(dir);
};

// The buffer left behind when the last tab goes is a place to type, so it needs
// wiring like any other. It had none: closing the last tab called createNew
// where every other route calls connect(), so the file that came back was
// permanently uncoloured — and drawn plain it is indistinguishable from a
// language with no grammar, so nothing about it looks wrong. The same failure
// the constructor's factory argument exists to prevent, one method along.
auto tTheRefilledBufferIsConnected =
    test("Workspace/theBufferLeftByClosingTheLastTabStillGetsAHighlighter") = []
{
    auto dir = scratch("refill");
    const auto a = write(dir / "a.txt", "a\n");

    auto made = 0;

    auto workspace = Workspace {
        [&made]
        {
            return eacp::OwningPointer<Highlighter> {new CountingHighlighter {made}};
        }};

    workspace.open(a);
    workspace.closeDiscarding(0);

    check(workspace.count() == 1);
    check(workspace.active().highlighter.get() != nullptr);

    // And connected, not merely present: an edit has to arrive.
    workspace.editor().insert("typed");

    check(static_cast<CountingHighlighter&>(*workspace.active().highlighter).edits
          == 1);

    std::filesystem::remove_all(dir);
};

auto tNextAndPreviousWrap = test("Workspace/nextAndPreviousWrapAtBothEnds") = []
{
    auto dir = scratch("cycle");
    const auto a = write(dir / "a.txt", "a\n");
    const auto b = write(dir / "b.txt", "b\n");

    auto workspace = Workspace {};

    workspace.open(a);
    workspace.open(b);

    check(workspace.activeIndex() == 1);

    workspace.activateNext();
    check(workspace.activeIndex() == 0);

    workspace.activatePrevious();
    check(workspace.activeIndex() == 1);

    std::filesystem::remove_all(dir);
};

// Each file keeps its own undo history, caret and scroll offset — the whole
// point of a tab rather than a single buffer that gets reloaded.
auto tEachTabKeepsItsOwnState =
    test("Workspace/eachTabKeepsItsOwnEditsAndScroll") = []
{
    auto dir = scratch("perTab");
    const auto a = write(dir / "a.txt", "aaa\n");
    const auto b = write(dir / "b.txt", "bbb\n");

    auto workspace = Workspace {};

    workspace.open(a);
    workspace.editor().insert("one");
    workspace.active().scrollY = -120.f;

    workspace.open(b);
    workspace.editor().insert("two");
    workspace.active().scrollY = -40.f;

    workspace.activate(0);

    check(workspace.active().file.document().text() == "oneaaa\n");
    check(workspace.active().scrollY == -120.f);
    check(workspace.editor().canUndo());

    // Undo here must not reach into the other file.
    workspace.editor().undo();

    check(workspace.at(0).file.document().text() == "aaa\n");
    check(workspace.at(1).file.document().text() == "twobbb\n");
    check(workspace.at(1).scrollY == -40.f);

    std::filesystem::remove_all(dir);
};

// One tree per document rather than one for the workspace. Sharing would mean a
// full reparse on every switch, which is the cold-open cost paid on a keystroke.
auto tOneHighlighterPerFile =
    test("Workspace/everyOpenFileGetsItsOwnHighlighter") = []
{
    auto dir = scratch("highlighters");
    const auto a = write(dir / "a.txt", "a\n");
    const auto b = write(dir / "b.txt", "b\n");

    auto made = 0;

    auto workspace = Workspace {
        [&made]
        {
            return eacp::OwningPointer<Highlighter> {new CountingHighlighter {made}};
        }};

    workspace.open(a);
    workspace.open(b);

    // Three built for two tabs: one for the untitled tab the workspace starts
    // with, and a replacement when opening a file into that tab — the document
    // is different, so the tree the old one holds describes text that is gone.
    check(made == 3);

    check(workspace.at(0).highlighter.get() != nullptr);
    check(workspace.at(1).highlighter.get() != nullptr);
    check(workspace.at(0).highlighter.get() != workspace.at(1).highlighter.get());

    std::filesystem::remove_all(dir);
};

// An edit has to reach the highlighter of the file it was made in, and only
// that one. Wired by the workspace, since the app no longer knows when a
// document is created.
auto tEditsReachTheirOwnHighlighter =
    test("Workspace/anEditReachesOnlyItsOwnFilesHighlighter") = []
{
    auto dir = scratch("edits");
    const auto a = write(dir / "a.txt", "a\n");
    const auto b = write(dir / "b.txt", "b\n");

    auto made = 0;

    auto workspace = Workspace {
        [&made]
        {
            return eacp::OwningPointer<Highlighter> {new CountingHighlighter {made}};
        }};

    workspace.open(a);
    workspace.open(b);

    workspace.activate(0);
    workspace.editor().insert("typed");

    const auto& first =
        static_cast<CountingHighlighter&>(*workspace.at(0).highlighter);
    const auto& second =
        static_cast<CountingHighlighter&>(*workspace.at(1).highlighter);

    check(first.edits == 1);
    check(second.edits == 0);

    std::filesystem::remove_all(dir);
};

// The chrome follows the workspace rather than every command remembering to
// update it, so anything that changes the set of tabs has to say so.
auto tChangesAreAnnounced =
    test("Workspace/openingClosingAndSwitchingAllNotify") = []
{
    auto dir = scratch("notify");
    const auto a = write(dir / "a.txt", "a\n");
    const auto b = write(dir / "b.txt", "b\n");

    auto changes = 0;

    auto workspace = Workspace {};
    workspace.onChanged = [&changes] { ++changes; };

    workspace.open(a);
    check(changes == 1);

    workspace.open(b);
    check(changes == 2);

    workspace.activate(0);
    check(changes == 3);

    // Activating what is already active is not a change, and reporting one
    // would redraw the window on every click on the current tab.
    workspace.activate(0);
    check(changes == 3);

    workspace.closeDiscarding(1);
    check(changes == 4);

    std::filesystem::remove_all(dir);
};

// Untitled buffers are reachable now — closing the last tab leaves one — so
// they have to be saveable, and ⌘S on one has to become "where?".
auto tSaveAsNamesAnUntitledFile =
    test("Workspace/saveAsGivesAnUntitledBufferAPath") = []
{
    auto dir = scratch("saveAs");
    const auto target = eacp::FilePath {dir / "named.txt"};

    auto workspace = Workspace {};
    workspace.editor().insert("written from an untitled buffer");

    check(workspace.active().file.save() == SaveResult::failed);

    check(workspace.active().file.saveAs(target) == SaveResult::saved);
    check(workspace.active().file.name() == "named.txt");
    check(!workspace.active().file.isDirty());

    check(eacp::Files::readFile(target) == "written from an untitled buffer");

    std::filesystem::remove_all(dir);
};

// A failed saveAs must not leave the buffer pointing at a file it is not in, or
// the next ⌘S writes somewhere nobody named.
auto tFailedSaveAsKeepsThePath = test("Workspace/aFailedSaveAsKeepsTheOldPath") = []
{
    auto dir = scratch("saveAsFails");
    const auto a = write(dir / "a.txt", "a\n");

    auto workspace = Workspace {};
    workspace.open(a);
    workspace.editor().insert("x");

    // Under an existing *file*, which cannot be a directory — a merely missing
    // directory is not enough, since writeFileAtomically creates those.
    const auto impossible = eacp::FilePath {dir / "a.txt" / "b.txt"};

    check(workspace.active().file.saveAs(impossible) == SaveResult::failed);
    check(workspace.active().file.path() == a);

    std::filesystem::remove_all(dir);
};
