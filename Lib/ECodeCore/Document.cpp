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
