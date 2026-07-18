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

    // Full re-index. O(file) per edit, which is the price of std::string
    // storage and goes away with the rope: a rope tracks line counts per node,
    // so an edit only touches the path down to it. Correctness first — this is
    // fast enough to type against on any file that fits the current storage.
    indexLines();

    return edit;
}

void Document::apply(const TextEdit& edit)
{
    const auto end = std::min(edit.removedEnd(), contents.size());
    const auto start = std::min(edit.start, end);

    contents.replace(start, end - start, edit.inserted);
    indexLines();
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

std::size_t Document::lineAt(std::size_t offset) const
{
    // The last line whose start is at or before the offset.
    const auto found =
        std::upper_bound(lineStarts.begin(), lineStarts.end(), offset);

    if (found == lineStarts.begin())
        return 0;

    return static_cast<std::size_t>(std::distance(lineStarts.begin(), found) - 1);
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
