#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ecode
{
// One replacement, and everything needed to reverse it.
//
// Every mutation is expressed this way — insertion is an edit with nothing
// removed, deletion one with nothing inserted — so undo is not a special case
// with its own bookkeeping. It is the same edit with the two strings swapped.
struct TextEdit
{
    std::size_t start = 0;
    std::string removed;
    std::string inserted;

    std::size_t removedEnd() const { return start + removed.size(); }
    std::size_t insertedEnd() const { return start + inserted.size(); }

    // How much the text after this edit shifts.
    std::ptrdiff_t delta() const
    {
        return static_cast<std::ptrdiff_t>(inserted.size())
               - static_cast<std::ptrdiff_t>(removed.size());
    }

    bool isEmpty() const { return removed.empty() && inserted.empty(); }

    TextEdit inverted() const { return {start, inserted, removed}; }
};

// Undo and redo, as stacks of edits.
//
// The unit of undo is a *step*, not an edit, because typing a word should undo
// as a word rather than a letter at a time. Consecutive insertions that
// continue where the last one ended are merged into the step already on the
// stack; anything else — a deletion, a jump elsewhere, a newline — starts a new
// one. That is roughly what VSCode does, and the rule is deliberately about
// adjacency rather than elapsed time so it stays deterministic and testable.
class EditHistory
{
public:
    // Records an edit that has already been applied.
    void record(const TextEdit& edit);

    // Ends the current step, so the next edit cannot merge into it. Called when
    // something happens that should be undoable separately: a cursor move, a
    // save, a paste.
    void breakStep() { open = false; }

    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }

    // The edits to apply, in order, to move one step back or forward. Empty
    // when there is nothing to do. Applying them is the caller's job — the
    // history does not own the text.
    std::vector<TextEdit> undo();
    std::vector<TextEdit> redo();

    void clear();

    std::size_t undoDepth() const { return undoStack.size(); }
    std::size_t redoDepth() const { return redoStack.size(); }

    // Which text the history is currently sitting on, rather than how many
    // changes it has seen.
    //
    // This is what a dirty flag needs and a counter cannot give it. Depth
    // cannot tell "undone back to the text that was saved" apart from "undone
    // once and then typed something else", because both leave the same number
    // of steps on the stack. So each step is minted an id that travels with it
    // onto the redo stack and back: undoing to a saved state restores that
    // state's id, while a new step recorded after an undo gets a fresh id that
    // can never collide with one already spent.
    std::uint64_t stateId() const;

private:
    // A step is the edits it is made of, in the order they were applied.
    struct Step
    {
        std::vector<TextEdit> edits;
        std::uint64_t id = 0;
    };

    bool canMergeInto(const Step& step, const TextEdit& edit) const;

    std::vector<Step> undoStack;
    std::vector<Step> redoStack;

    std::uint64_t nextStateId = 1;

    // Whether the newest step is still accepting edits.
    bool open = false;
};
} // namespace ecode
