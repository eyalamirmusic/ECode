#include "TextEdit.h"

#include <algorithm>

namespace ecode
{
namespace
{
bool isPlainInsertion(const TextEdit& edit)
{
    return edit.removed.empty() && !edit.inserted.empty();
}

// A newline ends the step, so undo after typing several lines goes back a line
// at a time rather than wiping the lot.
bool containsNewline(const std::string& text)
{
    return text.find('\n') != std::string::npos;
}
} // namespace

bool EditHistory::canMergeInto(const Step& step, const TextEdit& edit) const
{
    if (!open || step.empty())
        return false;

    if (!isPlainInsertion(edit) || containsNewline(edit.inserted))
        return false;

    const auto& last = step.back();

    if (!isPlainInsertion(last))
        return false;

    // Only when this insertion continues exactly where the last one ended.
    // Typing forward merges; moving the caret and typing elsewhere does not.
    return edit.start == last.insertedEnd();
}

void EditHistory::record(const TextEdit& edit)
{
    if (edit.isEmpty())
        return;

    // Any new edit invalidates the redo branch: the future it described no
    // longer follows from the present.
    redoStack.clear();

    if (!undoStack.empty() && canMergeInto(undoStack.back(), edit))
    {
        undoStack.back().push_back(edit);
        return;
    }

    undoStack.push_back({edit});

    // A deletion or a newline closes the step immediately, so the *next* edit
    // starts a fresh one rather than merging into this.
    open = isPlainInsertion(edit) && !containsNewline(edit.inserted);
}

std::vector<TextEdit> EditHistory::undo()
{
    if (undoStack.empty())
        return {};

    auto step = std::move(undoStack.back());
    undoStack.pop_back();

    // Reverse each edit, and apply them back to front: the later edits in a
    // step were applied to text the earlier ones had already shifted.
    auto inverses = std::vector<TextEdit> {};
    inverses.reserve(step.size());

    for (auto edit = step.rbegin(); edit != step.rend(); ++edit)
        inverses.push_back(edit->inverted());

    redoStack.push_back(std::move(step));

    // An undo always ends the step; typing after it starts a new one rather
    // than reopening what was just undone.
    open = false;

    return inverses;
}

std::vector<TextEdit> EditHistory::redo()
{
    if (redoStack.empty())
        return {};

    auto step = std::move(redoStack.back());
    redoStack.pop_back();

    auto edits = step;
    undoStack.push_back(std::move(step));

    open = false;

    return edits;
}

void EditHistory::clear()
{
    undoStack.clear();
    redoStack.clear();
    open = false;
}
} // namespace ecode
