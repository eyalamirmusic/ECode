#pragma once

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

    std::string contents;

    // Byte offset of each line's first character.
    std::vector<std::size_t> lineStarts;

    std::size_t widest = 0;
};
} // namespace ecode
