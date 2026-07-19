#pragma once

#include "TextEdit.h"

#include <eacp/Core/Core.h>

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
    Document() = default;

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
    std::size_t widestLine() const { return widest; }

private:
    void indexLines();
    void reindexAfterEdit(std::size_t start,
                          std::size_t removedLength,
                          std::string_view inserted);

    static std::size_t lineAtIn(const std::vector<std::size_t>& starts,
                                std::size_t offset);

    std::string contents;

    // Byte offset of each line's first character.
    std::vector<std::size_t> lineStarts;

    std::size_t widest = 0;
};
} // namespace ecode
