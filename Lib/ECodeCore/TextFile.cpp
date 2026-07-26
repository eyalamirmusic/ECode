#include "TextFile.h"

#include <span>
#include <stdexcept>

namespace ecode
{
TextFile::DiskState TextFile::stateOf(const eacp::FilePath& path)
{
    const auto file = eacp::File {path};

    if (!file.exists())
        return {};

    return {file.modificationTime(), file.size(), true};
}

bool TextFile::open(const eacp::FilePath& pathToOpen)
{
    // readFile returns an empty string for a file that is missing and for one
    // that is genuinely empty, so existence has to be asked separately.
    if (!eacp::File {pathToOpen}.exists())
        return false;

    filePath = pathToOpen;
    ed.setDocument(Document::fromFile(filePath));

    markSaved();

    return true;
}

bool TextFile::reload()
{
    if (!eacp::File {filePath}.exists())
        return false;

    // setDocument resets the caret, so the offset has to be carried across by
    // hand. It is clamped on the way back in, which is the point: the line it
    // used to be on may not exist any more.
    const auto caret = ed.cursor().head;

    ed.setDocument(Document::fromFile(filePath));
    ed.placeCaret(caret);

    markSaved();

    return true;
}

std::string TextFile::name() const
{
    return eacp::Files::filenameFromPath(filePath.str());
}

bool TextFile::hasChangedOnDisk() const
{
    const auto now = stateOf(filePath);

    return now.exists && now != onDisk;
}

bool TextFile::pollDisk()
{
    if (!hasChangedOnDisk())
        return false;

    if (isDirty())
        conflict = true;
    else
        reload();

    return true;
}

SaveResult TextFile::save()
{
    if (hasChangedOnDisk())
    {
        conflict = true;
        return SaveResult::changedOnDisk;
    }

    if (!isDirty())
        return SaveResult::upToDate;

    return saveOverwriting();
}

SaveResult TextFile::saveOverwriting()
{
    if (filePath.empty())
        return SaveResult::failed;

    const auto& text = ed.document().text();

    try
    {
        eacp::Files::writeFileAtomically(
            filePath,
            std::span {reinterpret_cast<const std::uint8_t*>(text.data()),
                       text.size()});
    }
    catch (const std::runtime_error&)
    {
        return SaveResult::failed;
    }

    markSaved();

    return SaveResult::saved;
}

SaveResult TextFile::saveAs(const eacp::FilePath& newPath)
{
    if (newPath.empty())
        return SaveResult::failed;

    const auto previous = filePath;

    filePath = newPath;

    const auto result = saveOverwriting();

    // A failed write must not leave the buffer pointing at a file it is not in,
    // or the next ⌘S would silently write somewhere nobody named.
    if (result != SaveResult::saved)
        filePath = previous;

    return result;
}

void TextFile::markSaved()
{
    savedState = ed.stateId();
    onDisk = stateOf(filePath);

    // Whatever the conflict was about, this is now the version on disk — so the
    // question the title was asking has been answered and must stop being
    // asked. Every route back to agreement passes through here: a save that
    // took the conflict, a reload, a saveAs, opening something else.
    conflict = false;

    // Typing that continues after a save starts a new undo step, so undoing
    // once from there lands exactly on the text that was written rather than
    // somewhere in the middle of it — which is what makes the saved state
    // reachable again, and the dirty flag able to clear.
    ed.breakUndoStep();
}
} // namespace ecode
