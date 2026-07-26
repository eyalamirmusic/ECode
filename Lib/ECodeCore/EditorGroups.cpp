#include "EditorGroups.h"

#include <algorithm>

namespace ecode
{
using namespace eacp;

namespace
{
// Handed back for an index that names no group, for the reason Workspace hands
// back an empty file for an index that names no tab: a stale group number from
// a click that raced a close should read as an empty workspace rather than as
// undefined behaviour.
Workspace& nowhere()
{
    static auto empty = Workspace {};

    return empty;
}
} // namespace

EditorGroups::EditorGroups(Workspace::HighlighterFactory factory)
    : makeHighlighter(std::move(factory))
{
    insertGroup(0);
}

Workspace& EditorGroups::at(int index)
{
    if (index < 0 || index >= groups.size())
        return nowhere();

    return *groups[index];
}

const Workspace& EditorGroups::at(int index) const
{
    return const_cast<EditorGroups&>(*this).at(index);
}

Workspace& EditorGroups::insertGroup(int position)
{
    auto& group =
        groups.insertNew(std::clamp(position, 0, groups.size()), makeHighlighter);

    // A group's own tab changes are the window's tab changes. Forwarded rather
    // than the window subscribing to each group, because the set of groups is
    // not fixed and a subscription per group is one more thing a split has to
    // remember to do.
    group.onChanged = [this] { onChanged(); };

    return group;
}

void EditorGroups::removeGroup(int index)
{
    // There is always a group, for the reason there is always a tab: everything
    // downstream is written against "the active file".
    if (groups.size() <= 1 || index < 0 || index >= groups.size())
        return;

    groups.removeAt(index);

    if (index < current)
        --current;

    current = std::clamp(current, 0, groups.size() - 1);

    onGroupsChanged();
    onChanged();
}

int EditorGroups::groupOf(const FilePath& path) const
{
    for (auto index = 0; index < groups.size(); ++index)
        if (groups[index]->indexOf(path) >= 0)
            return index;

    return -1;
}

bool EditorGroups::open(const FilePath& path)
{
    // Already open somewhere: go to it rather than making a second copy. The
    // jump to another pane is the visible cost of one path meaning one
    // document, and it is the cheaper half of that trade — the alternative is
    // two undo histories over one file.
    if (const auto existing = groupOf(path); existing >= 0)
        activate(existing);

    return active().open(path);
}

Workspace& EditorGroups::split()
{
    auto& group = insertGroup(current + 1);

    current = std::clamp(current + 1, 0, groups.size() - 1);

    onGroupsChanged();
    onChanged();

    return group;
}

bool EditorGroups::moveActiveFile(int direction)
{
    if (direction != 1 && direction != -1)
        return false;

    auto target = current + direction;

    if (target < 0 || target >= groups.size())
    {
        // The only file in its group has nowhere to go: it would make a group,
        // empty the one it left, and close that — arriving back at exactly the
        // arrangement it started from, one group along.
        if (active().count() <= 1)
            return false;

        target = direction > 0 ? groups.size() : 0;

        insertGroup(target);

        // A group inserted ahead of the active one moved it along. Fixed up
        // before anything is announced, or the window would rebuild its panes
        // around an active index naming a different group than it did a
        // statement ago.
        if (target <= current)
            ++current;

        onGroupsChanged();
        onChanged();
    }

    const auto source = current;

    auto& from = at(source);

    // Whether the group is about to be left with nothing but the untitled
    // buffer take() puts back. Asked before the move, because afterwards the
    // replacement is indistinguishable from a buffer someone was using.
    const auto emptiesSource = from.count() <= 1;

    auto moved = from.take(from.activeIndex());

    if (moved.get() == nullptr)
        return false;

    at(target).adopt(std::move(moved));

    if (emptiesSource)
    {
        removeGroup(source);

        if (source < target)
            --target;
    }

    activate(target);

    return true;
}

CloseResult EditorGroups::closeFile(int group, int index)
{
    auto& workspace = at(group);

    if (index < 0 || index >= workspace.count())
        return CloseResult::closed;

    if (workspace.at(index).file.isDirty())
        return CloseResult::hasUnsavedChanges;

    closeFileDiscarding(group, index);

    return CloseResult::closed;
}

void EditorGroups::closeFileDiscarding(int group, int index)
{
    auto& workspace = at(group);

    if (index < 0 || index >= workspace.count())
        return;

    // The last file in a group takes the group with it, which is what makes a
    // split undoable with the chord that made it — ⌘W until the pane is gone.
    // Only when there is another group to fall back to: the last group keeps
    // the untitled buffer, exactly as a lone Workspace does.
    const auto closesGroup = workspace.count() <= 1 && groups.size() > 1;

    workspace.closeDiscarding(index);

    if (closesGroup)
        removeGroup(group);
}

void EditorGroups::activate(int index)
{
    if (index < 0 || index >= groups.size() || index == current)
        return;

    current = index;

    onChanged();
}

void EditorGroups::activateNext()
{
    activate(groups.size() > 0 ? (current + 1) % groups.size() : 0);
}

void EditorGroups::activatePrevious()
{
    activate(groups.size() > 0 ? (current + groups.size() - 1) % groups.size() : 0);
}

bool EditorGroups::hasUnsavedChanges() const
{
    for (auto index = 0; index < groups.size(); ++index)
        if (groups[index]->hasUnsavedChanges())
            return true;

    return false;
}
} // namespace ecode
