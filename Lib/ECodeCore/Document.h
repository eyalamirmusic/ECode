#pragma once

#include "TextEdit.h"

#include <eacp/Core/Core.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ecode
{
// A file held in memory with an index of where its lines start.
//
// Read-only for now, and a plain std::string underneath. That is the right
// shape for the viewer milestone and the wrong one for editing: every insert
// would move the tail of the file. The rope or piece table replaces this
// storage once editing lands, but the line index and the accessors below are
// what the renderer talks to, so that swap does not reach the renderer.
class Document
{
public:
    // An empty document — and one that has been indexed, so it answers
    // lineCount(), lineAt() and columnAt() like any other.
    //
    // Not `= default`, which left the index empty and lineCount() reporting
    // zero, contradicting the invariant three lines below. It went unnoticed
    // for as long as nothing ever asked an unopened document anything: the app
    // opened a file into its one buffer before drawing a frame. Untitled
    // buffers made it reachable, and the symptom was columnAt() indexing an
    // empty vector while drawing the status bar.
    Document();

    static Document fromText(std::string text);
    static Document fromFile(const eacp::FilePath& path);

    // Applies an edit and returns it filled in: `removed` is populated with
    // whatever was actually there, which is what makes the result invertible
    // and therefore undoable. The caller passes what to remove as a range.
    //
    // Out-of-range offsets are clamped rather than rejected — a stale cursor
    // after an external reload should land somewhere sensible, not corrupt the
    // buffer or throw.
    TextEdit replace(std::size_t start, std::size_t end, std::string_view text);

    // Re-applies a recorded edit verbatim, for undo and redo. The edit must
    // have come from this document's history.
    void apply(const TextEdit& edit);

    // Byte offset of a line/column position, and the reverse. Column is a byte
    // offset within the line, matching tree-sitter's TSPoint and the renderer's
    // per-byte span walk.
    std::size_t offsetAt(std::size_t line, std::size_t column) const;
    std::size_t lineAt(std::size_t offset) const;
    std::size_t columnAt(std::size_t offset) const;

    std::size_t length() const { return contents.size(); }

    // Lines are counted the way an editor counts them: a trailing newline ends
    // the last line rather than starting an empty one, but a genuinely empty
    // document still has a single line to put the caret on.
    std::size_t lineCount() const { return lineStarts.size(); }

    // Excludes the line terminator. Out-of-range returns empty rather than
    // asserting, so a renderer racing a reload draws nothing instead of
    // reading past the end.
    std::string_view line(std::size_t index) const;

    const std::string& text() const { return contents; }
    bool isEmpty() const { return contents.empty(); }

    // Longest line in characters, for sizing a horizontal scroll range. Counted
    // in bytes, so it over-estimates for non-ASCII — good enough to scroll
    // with, and cheaper than a full UTF-8 pass over the file.
    //
    // Derived on demand rather than maintained on every edit. Recomputing it
    // eagerly is a scan of the whole line index, which measured at 4.7 ms of a
    // 6.7 ms keystroke on a 100 MB file — more than the flat string and the flat
    // index cost put together, and paid by a document nothing was asking. See
    // PLAN.md §7.6.
    std::size_t widestLine() const;

    // The line that count was measured on, for turning the range into points.
    //
    // Bytes cannot do that on their own: a tab is one byte and up to tabWidth
    // columns, so only the characters themselves say how far a line reaches.
    // The measuring is the renderer's — a tab width is a drawing decision this
    // class deliberately knows nothing about — so what is handed over is the
    // text.
    //
    // The widest line in *bytes*, which is not always the one that reaches
    // furthest across the screen: a shorter line with more tabs in it can
    // out-reach it. PLAN.md §5.3 records what that costs and what fixing it
    // would take.
    std::string_view widestLineText() const;

    // How many times the answer above had to be rebuilt from scratch.
    //
    // Exposed for tests, in the shape LineMap::rebuildCount() established: the
    // oracle that compares this document's index against a freshly built one
    // agrees whether the maximum was carried across the edit or rescanned, so
    // the counter is the only thing that can tell them apart. PLAN.md §9 — an
    // oracle proves the answer, never the path.
    std::uint64_t widestRescans() const { return rescans; }

    // Changes whenever the text does, so anything caching work derived from it
    // can tell in one comparison rather than by re-reading the file.
    //
    // Unique across documents as well as across edits, which is the part that
    // is easy to get wrong: a renderer caching by revision must not mistake a
    // freshly opened file for the one it replaced, and two documents both
    // starting at zero is exactly that mistake. So the counter is global and a
    // new document draws from it too.
    std::uint64_t revision() const { return currentRevision; }

private:
    static std::uint64_t nextRevision();

    void indexLines();
    void reindexAfterEdit(std::size_t start,
                          std::size_t removedLength,
                          std::string_view inserted);

    // Carries the longest line across an edit when it can be done without
    // touching every line, and gives up otherwise. Called with the index
    // already repaired.
    void updateWidestAfterEdit(std::size_t start,
                               std::size_t oldEnd,
                               std::string_view inserted);

    void rescanWidest() const;

    static std::size_t lineAtIn(const std::vector<std::size_t>& starts,
                                std::size_t offset);

    std::string contents;

    // Byte offset of each line's first character.
    std::vector<std::size_t> lineStarts;

    // The longest line, and where it begins.
    //
    // Held as an offset rather than a line index because an offset shifts the
    // same way the line index around it does — one addition, no reasoning about
    // how many entries the edit erased or inserted. It is then *checked* against
    // the repaired index, so a mistake in that arithmetic makes the record stale
    // rather than wrong: the fallback is a full rescan, which is the answer this
    // is an optimisation of.
    mutable std::size_t widest = 0;
    mutable std::size_t widestStart = 0;

    // Whether the two above still describe this text.
    mutable bool widestKnown = true;

    mutable std::uint64_t rescans = 0;

    std::uint64_t currentRevision = nextRevision();
};
} // namespace ecode
