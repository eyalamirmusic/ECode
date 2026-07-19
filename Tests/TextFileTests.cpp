#include "Common.h"

#include <ECodeCore/TextFile.h>

#include <chrono>
#include <filesystem>
#include <fstream>

// The file lifecycle: opening, dirtiness, saving, and noticing that disk moved
// underneath us.
//
// These touch the real filesystem, because that is the whole subject — the
// interesting cases are an external writer, a save that must not lose the
// original, and a dirty flag that has to survive undo. A fake filesystem would
// only test the fake.

using namespace nano;
using namespace ecode;

namespace
{
std::filesystem::path scratch(const std::string& name)
{
    auto dir = std::filesystem::temp_directory_path() / ("ecode-textfile-" + name);

    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    return dir;
}

void writeTo(const std::filesystem::path& path, std::string_view contents)
{
    auto out = std::ofstream {path, std::ios::binary | std::ios::trunc};
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

std::string contentsOf(const std::filesystem::path& path)
{
    return eacp::Files::readFile(eacp::FilePath {path});
}

// An external writer. The timestamp is pushed forward explicitly rather than
// left to the clock: two writes inside one filesystem tick share a modification
// time, and then the change is only visible if the length also moved.
void writeExternally(const std::filesystem::path& path, std::string_view contents)
{
    writeTo(path, contents);

    std::filesystem::last_write_time(
        path, std::filesystem::last_write_time(path) + std::chrono::seconds {2});
}
} // namespace

auto tOpenReadsTheFile = test("TextFile/openReadsTheFile") = []
{
    auto dir = scratch("open");
    auto path = dir / "hello.txt";
    writeTo(path, "one\ntwo\n");

    auto file = TextFile {};

    check(file.open(eacp::FilePath {path}));
    check(file.document().text() == "one\ntwo\n");
    check(file.name() == "hello.txt");
    check(!file.isDirty());

    std::filesystem::remove_all(dir);
};

auto tOpenMissingFails = test("TextFile/openMissingFileFails") = []
{
    auto dir = scratch("missing");

    auto file = TextFile {};
    file.editor().insert("existing work");

    check(!file.open(eacp::FilePath {dir / "nope.txt"}));

    // A failed open must not throw away what was already open.
    check(file.document().text() == "existing work");

    std::filesystem::remove_all(dir);
};

auto tEditingMakesItDirty = test("TextFile/editingMakesItDirty") = []
{
    auto dir = scratch("dirty");
    auto path = dir / "doc.txt";
    writeTo(path, "text");

    auto file = TextFile {};
    file.open(eacp::FilePath {path});

    check(!file.isDirty());

    file.editor().insert("more ");
    check(file.isDirty());

    std::filesystem::remove_all(dir);
};

auto tUndoBackToSavedIsClean = test("TextFile/undoBackToSavedTextIsClean") = []
{
    auto dir = scratch("undo-clean");
    auto path = dir / "doc.txt";
    writeTo(path, "text");

    auto file = TextFile {};
    file.open(eacp::FilePath {path});

    file.editor().insert("more ");
    check(file.isDirty());

    file.editor().undo();

    // Back at the text that was read, so there is nothing to save. A change
    // counter would still say dirty here.
    check(file.document().text() == "text");
    check(!file.isDirty());

    std::filesystem::remove_all(dir);
};

auto tRetypingAfterUndoIsDirty =
    test("TextFile/differentTextAtTheSameDepthIsDirty") = []
{
    auto dir = scratch("undo-branch");
    auto path = dir / "doc.txt";
    writeTo(path, "text");

    auto file = TextFile {};
    file.open(eacp::FilePath {path});

    file.editor().insert("a");
    check(file.save() == SaveResult::saved);

    // Undo off the saved text, then type something else. The history is exactly
    // as deep as it was when the file was written, and the text is not the text
    // that was written -- which is the case a depth comparison gets wrong in the
    // direction that costs you work: it reports clean and the save is skipped.
    file.editor().undo();
    file.editor().insert("b");

    check(file.document().text() != contentsOf(path));
    check(file.isDirty());

    std::filesystem::remove_all(dir);
};

auto tSaveWritesAndCleans = test("TextFile/saveWritesTheBuffer") = []
{
    auto dir = scratch("save");
    auto path = dir / "doc.txt";
    writeTo(path, "before");

    auto file = TextFile {};
    file.open(eacp::FilePath {path});

    file.editor().selectAll();
    file.editor().insert("after");

    check(file.save() == SaveResult::saved);
    check(contentsOf(path) == "after");
    check(!file.isDirty());

    std::filesystem::remove_all(dir);
};

auto tSaveIsAtomic = test("TextFile/saveLeavesNoTemporariesBehind") = []
{
    auto dir = scratch("atomic");
    auto path = dir / "doc.txt";
    writeTo(path, "x");

    auto file = TextFile {};
    file.open(eacp::FilePath {path});

    for (auto pass = 0; pass < 3; ++pass)
    {
        file.editor().insert("y");
        check(file.save() == SaveResult::saved);
    }

    auto entries = std::size_t {0};

    for (const auto& entry: std::filesystem::directory_iterator {dir})
    {
        (void) entry;
        ++entries;
    }

    check(entries == 1);

    std::filesystem::remove_all(dir);
};

auto tSaveWhenCleanDoesNothing = test("TextFile/saveWhenCleanDoesNotTouchDisk") = []
{
    auto dir = scratch("clean-save");
    auto path = dir / "doc.txt";
    writeTo(path, "unchanged");

    auto file = TextFile {};
    file.open(eacp::FilePath {path});

    const auto stampBefore = eacp::File {eacp::FilePath {path}}.modificationTime();

    check(file.save() == SaveResult::upToDate);
    check(eacp::File {eacp::FilePath {path}}.modificationTime() == stampBefore);

    std::filesystem::remove_all(dir);
};

auto tSaveAfterSaveIsUpToDate = test("TextFile/savingTwiceIsUpToDate") = []
{
    auto dir = scratch("twice");
    auto path = dir / "doc.txt";
    writeTo(path, "a");

    auto file = TextFile {};
    file.open(eacp::FilePath {path});

    file.editor().insert("b");

    check(file.save() == SaveResult::saved);
    check(file.save() == SaveResult::upToDate);

    std::filesystem::remove_all(dir);
};

auto tExternalChangeIsSeen = test("TextFile/externalWriteIsDetected") = []
{
    auto dir = scratch("external");
    auto path = dir / "doc.txt";
    writeTo(path, "ours");

    auto file = TextFile {};
    file.open(eacp::FilePath {path});

    check(!file.hasChangedOnDisk());

    writeExternally(path, "theirs");

    check(file.hasChangedOnDisk());

    std::filesystem::remove_all(dir);
};

auto tSaveRefusesOverExternalChange = test("TextFile/saveRefusesToClobber") = []
{
    auto dir = scratch("clobber");
    auto path = dir / "doc.txt";
    writeTo(path, "ours");

    auto file = TextFile {};
    file.open(eacp::FilePath {path});
    file.editor().insert("edited ");

    writeExternally(path, "someone else's work");

    check(file.save() == SaveResult::changedOnDisk);

    // The other write survives untouched, and the buffer is still dirty so the
    // text is not lost either.
    check(contentsOf(path) == "someone else's work");
    check(file.isDirty());

    std::filesystem::remove_all(dir);
};

auto tOverwriteWins = test("TextFile/saveOverwritingTakesTheConflict") = []
{
    auto dir = scratch("overwrite");
    auto path = dir / "doc.txt";
    writeTo(path, "ours");

    auto file = TextFile {};
    file.open(eacp::FilePath {path});
    file.editor().selectAll();
    file.editor().insert("mine");

    writeExternally(path, "theirs");

    check(file.saveOverwriting() == SaveResult::saved);
    check(contentsOf(path) == "mine");

    // And the conflict is resolved: an ordinary save works again afterwards.
    check(!file.isDirty());
    check(!file.hasChangedOnDisk());

    std::filesystem::remove_all(dir);
};

auto tDeletedFileIsNotAConflict =
    test("TextFile/aDeletedFileSavesRatherThanRefusing") = []
{
    auto dir = scratch("deleted");
    auto path = dir / "doc.txt";
    writeTo(path, "here");

    auto file = TextFile {};
    file.open(eacp::FilePath {path});
    file.editor().insert("still ");

    std::filesystem::remove(path);

    // Nothing can be clobbered, so refusing would only trap the text in the
    // buffer.
    check(!file.hasChangedOnDisk());
    check(file.save() == SaveResult::saved);
    check(contentsOf(path) == "still here");

    std::filesystem::remove_all(dir);
};

auto tReloadTakesDiskVersion = test("TextFile/reloadTakesTheDiskVersion") = []
{
    auto dir = scratch("reload");
    auto path = dir / "doc.txt";
    writeTo(path, "first\nsecond\n");

    auto file = TextFile {};
    file.open(eacp::FilePath {path});
    file.editor().insert("local ");

    writeExternally(path, "replaced\n");

    check(file.reload());
    check(file.document().text() == "replaced\n");
    check(!file.isDirty());
    check(!file.hasChangedOnDisk());

    std::filesystem::remove_all(dir);
};

auto tReloadClampsTheCaret = test("TextFile/reloadKeepsTheCaretInBounds") = []
{
    auto dir = scratch("reload-caret");
    auto path = dir / "doc.txt";
    writeTo(path, "a long first line\nand a second\n");

    auto file = TextFile {};
    file.open(eacp::FilePath {path});
    file.editor().moveToDocumentEnd();

    writeExternally(path, "hi\n");

    check(file.reload());

    // The offset the caret was at no longer exists in the shorter file.
    check(file.editor().cursor().head <= file.document().length());

    std::filesystem::remove_all(dir);
};

auto tSaveWithNoPathFails = test("TextFile/savingWithNoPathFails") = []
{
    auto file = TextFile {};
    file.editor().insert("untitled work");

    check(file.save() == SaveResult::failed);
};

auto tSavePreservesCrlf = test("TextFile/saveKeepsLineEndingsVerbatim") = []
{
    auto dir = scratch("crlf");
    auto path = dir / "windows.txt";
    writeTo(path, "one\r\ntwo\r\n");

    auto file = TextFile {};
    file.open(eacp::FilePath {path});

    // The document strips the carriage return when handing out a line, but it
    // must not strip it from the bytes it saves back.
    check(file.document().line(0) == "one");

    file.editor().moveToDocumentEnd();
    file.editor().insert("three\r\n");

    check(file.save() == SaveResult::saved);
    check(contentsOf(path) == "one\r\ntwo\r\nthree\r\n");

    std::filesystem::remove_all(dir);
};
