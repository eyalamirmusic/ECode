#pragma once

#include "Workspace.h"

namespace ecode
{
// The editor groups: several workspaces side by side, and which of them is
// being worked in.
//
// PLAN.md §7 called this "one Workspace holds one active file, and a split view
// is two of them side by side", and that is exactly what this is — a Workspace
// is a group, unchanged, and this only says how many there are and routes what
// has to be decided above one of them.
//
// The one thing it has to decide is **which group a path opens into**, and that
// is not a detail. §7.8 rejected two tabs over one file because they would be
// two undo histories and two dirty flags over one set of bytes with whichever
// saved last winning silently — and two *groups* over one file is the same
// failure with more room to hit it, since seeing one file beside itself is the
// obvious reason to split at all. So a path lives in exactly one group, and
// opening one already open elsewhere goes to it rather than making a second
// copy. What that costs is the arrangement someone splitting a window usually
// wants; see moveActiveFile for the one this offers instead, and PLAN.md §7.10
// for what showing one document in two groups would actually take.
class EditorGroups
{
public:
    // Passed on to every group, so a group made by a split colours its files
    // exactly like the one it was split from. Taken at construction for the
    // reason Workspace takes it at construction: the first group is built here,
    // and a factory installed afterwards would leave that one permanently
    // uncoloured.
    explicit EditorGroups(
        Workspace::HighlighterFactory factory = Workspace::noHighlighting());

    int count() const { return groups.size(); }
    int activeIndex() const { return current; }

    Workspace& at(int index);
    const Workspace& at(int index) const;

    Workspace& active() { return at(activeIndex()); }
    const Workspace& active() const { return at(activeIndex()); }

    // The file being worked in, wherever it is. Every command that says "the
    // editor" means this one.
    Editor& editor() { return active().editor(); }
    const Editor& editor() const { return active().editor(); }

    // Opens into whichever group already has the file, or into the active one.
    // False when the file cannot be read, in which case nothing changes.
    bool open(const eacp::FilePath& path);

    // The group showing this path, or -1.
    int groupOf(const eacp::FilePath& path) const;

    // A new empty group after the active one, activated. It holds the untitled
    // buffer every Workspace starts with, which is what a file opened or moved
    // into it then replaces.
    Workspace& split();

    // Moves the active file one group along, making a group when there is none
    // that way. False when nothing moved.
    //
    // This is the gesture that fills a split, and it is why split() leaves an
    // empty group rather than duplicating the current file into it: a file can
    // only be in one group, so "put this one over there" is the arrangement
    // that is actually available.
    bool moveActiveFile(int direction);

    // The same pair Workspace has, with the group named — and with one rule of
    // its own: closing the last file in a group closes the group, so a split
    // that is finished with folds back rather than leaving a blank pane with an
    // Untitled in it. The first group never closes; there is always one.
    CloseResult closeFile(int group, int index);
    void closeFileDiscarding(int group, int index);

    void activate(int index);
    void activateNext();
    void activatePrevious();

    // True when any file in any group has edits that were never written.
    bool hasUnsavedChanges() const;

    // Anything the chrome shows has changed: the tabs, the active file, or
    // which group is active.
    std::function<void()> onChanged = [] {};

    // A group was added or removed, so the window has a different number of
    // panes to lay out. Fired *before* onChanged, so whatever rebuilds the
    // views has done it by the time anything asks them to show something.
    std::function<void()> onGroupsChanged = [] {};

private:
    // Inserts a group at `position`, wired to fire onChanged when its own tabs
    // change. Does not activate it or announce it — both callers want to do
    // that after they have finished moving things around.
    Workspace& insertGroup(int position);

    void removeGroup(int index);

    Workspace::HighlighterFactory makeHighlighter;

    eacp::OwnedVector<Workspace> groups;

    int current = 0;
};
} // namespace ecode
