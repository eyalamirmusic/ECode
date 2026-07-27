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

    // Replaces the buffer with text that did not come from a file — a host
    // handing over a string it already has, a generated preview, a scratch
    // buffer. The path is left alone, so text set into an opened file and then
    // saved goes back where the file came from.
    //
    // The undo history does not survive, for the reason reload()'s does not.
    void setText(std::string text);

    const eacp::FilePath& path() const { return filePath; }

    // The filename alone, for a title bar or a tab.
    std::string name() const;

    // True when the buffer differs from the text last read or written.
    //
    // Follows undo rather than counting edits, so typing and then undoing back
    // to the saved text reads as clean again — see EditHistory::stateId.
    //
    // The flag is what setText needs and undo cannot express. Clearing the
    // history puts stateId() back to the zero a never-edited buffer reports, so
    // a file whose text was replaced wholesale would compare equal to the state
    // it was opened in — reading as "matches disk" over text nothing has
    // written, and making the next save() report itself up to date.
    bool isDirty() const { return replaced || ed.stateId() != savedState; }

    // True when the file on disk is no longer the one we read: another editor,
    // a git checkout, a code generator. Cheap enough to call on every window
    // activation, which is the closest thing to file watching we have.
    //
    // A file that has been *deleted* is not a change by this definition. There
    // is nothing left to overwrite, so saving it back is safe and a refusal
    // would only trap the text in the buffer.
    bool hasChangedOnDisk() const;

    // True once a save was refused because the file moved underneath us, and
    // until something settles it — a save that takes the conflict, a reload, or
    // opening something else.
    //
    // Kept with the file rather than with the window because the question is
    // per file: with several open, the one being looked at is not necessarily
    // the one whose save was refused.
    bool isConflicted() const { return conflict; }

    // One stat, and whatever the answer implies. A clean buffer simply takes
    // the new version, which is what makes a git checkout or a formatter run
    // appear on its own; a dirty one raises the conflict for a person to
    // settle, because merging is not something to guess at.
    //
    // True when anything changed, so the caller knows to redraw. Standing in
    // for file watching, which eacp does not have — FSEvents replaces the
    // hasChangedOnDisk inside this, not this.
    bool pollDisk();

    // Writes the buffer to disk atomically, so an interrupted save cannot
    // truncate the original.
    //
    // Refuses with `changedOnDisk` rather than clobbering someone else's write:
    // which version wins is a question only a person can answer. The caller
    // either reloads or asks again through saveOverwriting.
    SaveResult save();
    SaveResult saveOverwriting();

    // Writes to a new path and adopts it, which is what makes an untitled
    // buffer saveable at all — there is nothing else to write to.
    //
    // Never refuses on a conflict: the person has just been shown a save panel
    // naming this file and has already answered the overwrite question there,
    // so asking again through the title bar would be asking twice.
    SaveResult saveAs(const eacp::FilePath& newPath);

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

    bool conflict = false;

    // The buffer was replaced by setText and nothing has written it since. See
    // isDirty; cleared by markSaved along with everything else that says the
    // buffer and the disk agree.
    bool replaced = false;
};
} // namespace ecode
