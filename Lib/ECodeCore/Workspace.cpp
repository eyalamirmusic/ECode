#include "Workspace.h"

#include <eacp/Core/Utils/StdPath.h>

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace ecode
{
using namespace eacp;

namespace
{
// The path two spellings of one file agree on. weakly_canonical rather than
// canonical because it answers for a path that does not exist yet, which is
// what a save-as target is — and it resolves `..` and symlinks on the part that
// does exist, which is the whole point of asking.
//
// Falls back to the text as given when the filesystem cannot answer, so a path
// on an unreadable volume still compares equal to itself.
std::string resolved(const FilePath& path)
{
    if (path.empty())
        return {};

    auto error = std::error_code {};
    const auto canonical = std::filesystem::weakly_canonical(toStdPath(path), error);

    if (error)
        return path.str();

    return FilePath {canonical}.str();
}

// Handed back for an index that names no tab, so a stale tab number from a
// click that raced a close reads as an empty untitled file rather than as UB.
OpenFile& nowhere()
{
    static auto empty = OpenFile {};

    return empty;
}
} // namespace

Workspace::Workspace(HighlighterFactory factory)
    : makeHighlighter(std::move(factory))
{
    // One tab from the start, so active() is answerable before anything has
    // been opened — and connected like any other, which is the whole reason the
    // factory is a constructor argument.
    connect(files.createNew());
}

OpenFile& Workspace::at(int index)
{
    if (index < 0 || index >= files.size())
        return nowhere();

    return *files[index];
}

const OpenFile& Workspace::at(int index) const
{
    return const_cast<Workspace&>(*this).at(index);
}

bool Workspace::isScratch(const OpenFile& entry) const
{
    return entry.file.path().empty() && !entry.file.isDirty();
}

void Workspace::connect(OpenFile& entry)
{
    entry.highlighter = makeHighlighter();

    auto& editor = entry.file.editor();

    // Captured by pointer rather than by reference to the entry, because the
    // entry outlives neither of these: both are cleared when it is destroyed.
    auto* target = entry.highlighter.get();

    if (target == nullptr)
        return;

    editor.onEdit = [target, &editor](const TextEdit& edit)
    { target->applyEdit(editor.document(), edit); };

    editor.onDocumentReplaced = [target] { target->reset(); };
}

void Workspace::refillIfEmpty()
{
    if (files.size() == 0)
        connect(files.createNew());
}

int Workspace::indexOf(const FilePath& path) const
{
    if (path.empty())
        return -1;

    const auto wanted = resolved(path);

    for (auto index = 0; index < files.size(); ++index)
        if (resolved(files[index]->file.path()) == wanted)
            return index;

    return -1;
}

OpenFile& Workspace::insertAfterActive()
{
    // Beside the file it was opened from rather than at the end, which is where
    // VSCode puts it and what keeps a file opened from the tree next to the one
    // that led to it.
    const auto position = std::clamp(current + 1, 0, files.size());

    auto& entry = files.insertNew(position);

    current = position;

    return entry;
}

bool Workspace::open(const FilePath& path)
{
    if (const auto existing = indexOf(path); existing >= 0)
    {
        activate(existing);
        return true;
    }

    // Read into a throwaway first: a file that cannot be read must leave the
    // workspace exactly as it was, and opening a tab before finding out would
    // leave an empty one behind.
    auto opened = OpenFile {};

    if (!opened.file.open(path))
        return false;

    auto& entry = isScratch(active()) ? active() : insertAfterActive();

    entry.file = std::move(opened.file);
    connect(entry);

    onChanged();

    return true;
}

OpenFile& Workspace::addUntitled()
{
    auto& entry = insertAfterActive();

    connect(entry);
    onChanged();

    return entry;
}

CloseResult Workspace::close(int index)
{
    if (index < 0 || index >= files.size())
        return CloseResult::closed;

    if (files[index]->file.isDirty())
        return CloseResult::hasUnsavedChanges;

    closeDiscarding(index);

    return CloseResult::closed;
}

void Workspace::closeDiscarding(int index)
{
    if (index < 0 || index >= files.size())
        return;

    files.removeAt(index);

    refillIfEmpty();

    // Closing the tab you were on lands on the one that took its place, and
    // closing the last tab lands on its neighbour to the left — which is where
    // the eye already is. Closing a tab *before* the active one leaves the same
    // file active, which is why this is a comparison rather than a clamp.
    if (index < current)
        --current;

    current = std::clamp(current, 0, files.size() - 1);

    onChanged();
}

OwningPointer<OpenFile> Workspace::take(int index)
{
    if (index < 0 || index >= files.size())
        return {};

    auto taken = std::move(files[index]);

    files.removeAt(index);
    refillIfEmpty();

    // The same arithmetic closeDiscarding does, and for the same reason: taking
    // a tab before the active one leaves the same file active rather than
    // sliding the selection along with the list.
    if (index < current)
        --current;

    current = std::clamp(current, 0, files.size() - 1);

    onChanged();

    return taken;
}

void Workspace::adopt(OwningPointer<OpenFile> incoming)
{
    if (incoming.get() == nullptr)
        return;

    if (isScratch(active()))
    {
        files[current] = std::move(incoming);
    }
    else
    {
        const auto position = std::clamp(current + 1, 0, files.size());

        files.insertAt(position) = std::move(incoming);
        current = position;
    }

    onChanged();
}

void Workspace::activate(int index)
{
    if (index < 0 || index >= files.size() || index == current)
        return;

    current = index;
    onChanged();
}

void Workspace::activateNext()
{
    activate(files.size() > 0 ? (current + 1) % files.size() : 0);
}

void Workspace::activatePrevious()
{
    activate(files.size() > 0 ? (current + files.size() - 1) % files.size() : 0);
}

bool Workspace::hasUnsavedChanges() const
{
    for (auto index = 0; index < files.size(); ++index)
        if (files[index]->file.isDirty())
            return true;

    return false;
}
} // namespace ecode
