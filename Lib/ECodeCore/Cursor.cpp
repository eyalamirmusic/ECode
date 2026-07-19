#include "Cursor.h"

#include <algorithm>

namespace ecode
{
namespace
{
// A UTF-8 continuation byte, which is never a character boundary.
bool isContinuation(char byte)
{
    return (static_cast<unsigned char>(byte) & 0xc0) == 0x80;
}

bool isWordByte(char byte)
{
    const auto value = static_cast<unsigned char>(byte);

    // Anything non-ASCII counts as a word byte: without proper Unicode
    // categories, treating accented letters and CJK as punctuation would make
    // word movement stop between every character of them.
    if (value >= 0x80)
        return true;

    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
           || (value >= '0' && value <= '9') || value == '_';
}

bool isBlank(char byte)
{
    return byte == ' ' || byte == '\t';
}
} // namespace

namespace Motion
{
std::size_t left(const Document& document, std::size_t offset)
{
    const auto& text = document.text();

    if (offset == 0 || text.empty())
        return 0;

    auto index = std::min(offset, text.size()) - 1;

    // Walk back over continuation bytes so the caret lands on a character
    // boundary rather than inside one.
    while (index > 0 && isContinuation(text[index]))
        --index;

    return index;
}

std::size_t right(const Document& document, std::size_t offset)
{
    const auto& text = document.text();

    if (offset >= text.size())
        return text.size();

    auto index = offset + 1;

    while (index < text.size() && isContinuation(text[index]))
        ++index;

    return index;
}

std::size_t wordLeft(const Document& document, std::size_t offset)
{
    const auto& text = document.text();
    auto index = std::min(offset, text.size());

    // Skip whatever separates us from the previous word, then the word itself.
    while (index > 0 && !isWordByte(text[index - 1]))
        --index;

    while (index > 0 && isWordByte(text[index - 1]))
        --index;

    return index;
}

std::size_t wordRight(const Document& document, std::size_t offset)
{
    const auto& text = document.text();
    auto index = std::min(offset, text.size());

    while (index < text.size() && !isWordByte(text[index]))
        ++index;

    while (index < text.size() && isWordByte(text[index]))
        ++index;

    return index;
}

std::size_t lineStart(const Document& document, std::size_t offset)
{
    const auto line = document.lineAt(offset);
    const auto text = document.line(line);
    const auto begin = document.offsetAt(line, 0);

    // The first non-blank, unless the caret is already there or before it, in
    // which case go to the true start. That is the toggle a repeated Home press
    // gives on an indented line.
    auto indent = std::size_t {0};

    while (indent < text.size() && isBlank(text[indent]))
        ++indent;

    if (indent == text.size())
        return begin;

    return offset > begin + indent ? begin + indent : begin;
}

std::size_t lineEnd(const Document& document, std::size_t offset)
{
    const auto line = document.lineAt(offset);

    return document.offsetAt(line, document.line(line).size());
}

std::size_t vertical(const Document& document, Cursor& cursor, int lines)
{
    const auto line = document.lineAt(cursor.head);

    // The first vertical move of a run captures the column to hold; later ones
    // reuse it, so passing through a short line does not narrow the target.
    if (!cursor.holdsColumn)
    {
        cursor.desiredColumn = document.columnAt(cursor.head);
        cursor.holdsColumn = true;
    }

    const auto count = static_cast<std::ptrdiff_t>(document.lineCount());
    auto target = static_cast<std::ptrdiff_t>(line) + lines;

    target = std::clamp(target, std::ptrdiff_t {0}, count - 1);

    // offsetAt clamps the column to the target line's length, which is what
    // makes a short line stop short without losing the held column.
    return document.offsetAt(static_cast<std::size_t>(target), cursor.desiredColumn);
}

std::size_t documentStart(const Document&)
{
    return 0;
}

std::size_t documentEnd(const Document& document)
{
    return document.length();
}
} // namespace Motion
} // namespace ecode
