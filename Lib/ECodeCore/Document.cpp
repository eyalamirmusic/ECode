#include "Document.h"

#include <algorithm>

namespace ecode
{
Document Document::fromText(std::string text)
{
    auto document = Document {};
    document.contents = std::move(text);
    document.indexLines();

    return document;
}

Document Document::fromFile(const eacp::FilePath& path)
{
    return fromText(eacp::Files::readFile(path));
}

void Document::indexLines()
{
    lineStarts.clear();
    widest = 0;

    // Even an empty document has one line, so there is somewhere to put a caret.
    lineStarts.push_back(0);

    for (std::size_t index = 0; index < contents.size(); ++index)
    {
        if (contents[index] != '\n')
            continue;

        const auto lineLength = index - lineStarts.back();
        widest = std::max(widest, lineLength);

        // A newline at the very end terminates the last line rather than
        // starting an empty one after it.
        if (index + 1 < contents.size())
            lineStarts.push_back(index + 1);
    }

    if (!contents.empty() && contents.back() != '\n')
        widest = std::max(widest, contents.size() - lineStarts.back());
}

TextEdit Document::replace(std::size_t start, std::size_t end, std::string_view text)
{
    start = std::min(start, contents.size());
    end = std::clamp(end, start, contents.size());

    auto edit = TextEdit {};
    edit.start = start;
    edit.removed = contents.substr(start, end - start);
    edit.inserted = std::string {text};

    contents.replace(start, end - start, text);
    reindexAfterEdit(start, end - start, text);

    return edit;
}

void Document::apply(const TextEdit& edit)
{
    const auto end = std::min(edit.removedEnd(), contents.size());
    const auto start = std::min(edit.start, end);

    contents.replace(start, end - start, edit.inserted);
    reindexAfterEdit(start, end - start, edit.inserted);
}

// Repairs the line index around an edit instead of rescanning the file.
//
// Only the replaced span can have gained or lost newlines, and everything after
// it simply shifts by the edit's delta. So the work is proportional to the edit
// plus the number of lines after it, rather than to the file's bytes -- for a
// large file that is a few hundred thousand integer adds per keystroke instead
// of scanning megabytes.
//
// Still linear in line count, because the index is a flat vector of absolute
// offsets. Making it properly logarithmic is the rope's job, where each node
// carries its own line count and an edit only touches the path down to it.
void Document::reindexAfterEdit(std::size_t start,
                                std::size_t removedLength,
                                std::string_view inserted)
{
    if (lineStarts.empty())
    {
        indexLines();
        return;
    }

    const auto delta = static_cast<std::ptrdiff_t>(inserted.size())
                       - static_cast<std::ptrdiff_t>(removedLength);
    const auto oldEnd = start + removedLength;

    // The line the edit begins on keeps its start; every line that began inside
    // the replaced span is gone.
    const auto firstAffected = lineAtIn(lineStarts, start);

    auto after = lineStarts.begin() + static_cast<std::ptrdiff_t>(firstAffected) + 1;

    while (after != lineStarts.end() && *after <= oldEnd)
        after = lineStarts.erase(after);

    // Everything past the edit shifts.
    for (auto it = after; it != lineStarts.end(); ++it)
        *it = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(*it) + delta);

    // Newlines the replacement introduced start new lines.
    auto added = std::vector<std::size_t> {};

    for (std::size_t offset = 0; offset < inserted.size(); ++offset)
        if (inserted[offset] == '\n')
            added.push_back(start + offset + 1);

    // A newline at the very end of the document terminates the last line rather
    // than starting an empty one, matching indexLines.
    if (!added.empty() && added.back() >= contents.size())
        added.pop_back();

    lineStarts.insert(after, added.begin(), added.end());

    // Whether a newline starts a line depends on whether anything follows it,
    // and an edit can flip that for the newline immediately before it: typing
    // "\n" at the end of the document creates no line, but typing anything
    // after it must. Only this one newline can change status, so it is the only
    // one worth re-examining.
    if (start > 0 && start <= contents.size() && contents[start - 1] == '\n')
    {
        const auto at =
            std::lower_bound(lineStarts.begin(), lineStarts.end(), start);
        const auto present = at != lineStarts.end() && *at == start;

        if (start < contents.size() && !present)
            lineStarts.insert(at, start);
        else if (start >= contents.size() && present)
            lineStarts.erase(at);
    }

    // A deletion can leave line starts at or past the new end.
    while (lineStarts.size() > 1 && lineStarts.back() >= contents.size())
        lineStarts.pop_back();

    // Line lengths come from the index alone, so this costs no byte scanning.
    widest = 0;

    for (std::size_t index = 0; index < lineStarts.size(); ++index)
        widest = std::max(widest, line(index).size());
}

std::size_t Document::offsetAt(std::size_t line, std::size_t column) const
{
    if (lineStarts.empty())
        return 0;

    const auto index = std::min(line, lineStarts.size() - 1);
    const auto start = lineStarts[index];

    // Clamped to the line's own length so a column carried over from a longer
    // line cannot walk into the next one.
    return start + std::min(column, this->line(index).size());
}

std::size_t Document::lineAtIn(const std::vector<std::size_t>& starts,
                               std::size_t offset)
{
    const auto found = std::upper_bound(starts.begin(), starts.end(), offset);

    if (found == starts.begin())
        return 0;

    return static_cast<std::size_t>(std::distance(starts.begin(), found) - 1);
}

std::size_t Document::lineAt(std::size_t offset) const
{
    // The last line whose start is at or before the offset.
    return lineAtIn(lineStarts, offset);
}

std::size_t Document::columnAt(std::size_t offset) const
{
    const auto index = lineAt(offset);
    const auto start = lineStarts[index];

    return offset >= start ? offset - start : 0;
}

std::string_view Document::line(std::size_t index) const
{
    if (index >= lineStarts.size())
        return {};

    const auto start = lineStarts[index];
    const auto end =
        index + 1 < lineStarts.size() ? lineStarts[index + 1] : contents.size();

    auto text = std::string_view {contents}.substr(start, end - start);

    // Drop the terminator, and the CR of a CRLF pair with it.
    if (!text.empty() && text.back() == '\n')
        text.remove_suffix(1);

    if (!text.empty() && text.back() == '\r')
        text.remove_suffix(1);

    return text;
}
} // namespace ecode
