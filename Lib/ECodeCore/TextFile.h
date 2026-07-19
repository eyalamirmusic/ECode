#pragma once

#include "Editor.h"

#include <eacp/Core/Core.h>

#include <cstdint>
#include <string>

namespace ecode
{
// Whether a save happened, and if not, why not.
enum class SaveResult
{
    saved,

    // The buffer already matches what is on disk, so nothing was written.
    upToDate,

    // Someone else wrote the file since we last read it. Nothing was written;
    // see TextFile::save.
    changedOnDisk,

    // No path to write to, or the write itself failed.
    failed,
};

// An Editor together with the file its text came from.
//
// The Editor deliberately knows nothing about files — that is what makes it
// testable by driving it and reading the text back — so the rest of "having a
// file open" lives here: where the text came from, whether it still matches
// what is on disk, and whether disk still holds what we last read. An editor
// tab will own one of these.
class TextFile
{
public:
    TextFile() = default;

    Editor& editor() { return ed; }
    const Editor& editor() const { return ed; }
    const Document& document() const { return ed.document(); }

    // Reads the file and hands its text to the editor, discarding whatever was
    // open. False if it cannot be read, in which case nothing changes.
    bool open(const eacp::FilePath& pathToOpen);

    // Re-reads from disk, keeping the caret roughly where it was. The undo
    // history does not survive: its edits describe text that is no longer there.
    bool reload();

    const eacp::FilePath& path() const { return filePath; }

    // The filename alone, for a title bar or a tab.
    std::string name() const;

    // True when the buffer differs from the text last read or written.
    //
    // Follows undo rather than counting edits, so typing and then undoing back
    // to the saved text reads as clean again — see EditHistory::stateId.
    bool isDirty() const { return ed.stateId() != savedState; }

    // True when the file on disk is no longer the one we read: another editor,
    // a git checkout, a code generator. Cheap enough to call on every window
    // activation, which is the closest thing to file watching we have.
    //
    // A file that has been *deleted* is not a change by this definition. There
    // is nothing left to overwrite, so saving it back is safe and a refusal
    // would only trap the text in the buffer.
    bool hasChangedOnDisk() const;

    // Writes the buffer to disk atomically, so an interrupted save cannot
    // truncate the original.
    //
    // Refuses with `changedOnDisk` rather than clobbering someone else's write:
    // which version wins is a question only a person can answer. The caller
    // either reloads or asks again through saveOverwriting.
    SaveResult save();
    SaveResult saveOverwriting();

private:
    // What we last saw on disk. Modification time plus size, the pair every
    // editor uses: neither is reliable alone — timestamp granularity is a whole
    // second on some filesystems, and an edit that keeps the length is common —
    // but a change that moves neither is rare enough to live with until there
    // is a real file watcher.
    struct DiskState
    {
        std::int64_t modified = 0;
        std::uint64_t size = 0;
        bool exists = false;

        bool operator==(const DiskState&) const = default;
    };

    static DiskState stateOf(const eacp::FilePath& path);

    void markSaved();

    Editor ed;
    eacp::FilePath filePath;

    // The history position and the disk contents that agree with each other.
    std::uint64_t savedState = 0;
    DiskState onDisk;
};
} // namespace ecode
