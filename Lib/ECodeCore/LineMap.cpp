#include "LineMap.h"

#include "Utf8.h"

#include <algorithm>

namespace ecode
{
namespace
{
std::size_t
    advanceColumn(char32_t codepoint, std::size_t column, std::size_t tabWidth)
{
    if (codepoint != U'\t' || tabWidth == 0)
        return column + 1;

    // To the next stop, not by a fixed amount, so indentation lines up the way
    // the file's author saw it.
    return (column / tabWidth + 1) * tabWidth;
}

// Whether a row may begin at `index` — the start of a word that follows a run
// of blanks. Breaking *after* the blanks rather than before them keeps trailing
// spaces on the row they were typed on, which is what stops a wrapped sentence
// from starting with one.
bool isBreakOpportunity(std::string_view text, std::size_t index)
{
    if (index == 0 || index >= text.size())
        return false;

    const auto previous = text[index - 1];
    const auto here = text[index];

    const auto blank = [](char c) { return c == ' ' || c == '\t'; };

    return blank(previous) && !blank(here);
}
} // namespace

std::size_t displayWidth(std::string_view text, std::size_t tabWidth)
{
    return displayColumnAt(text, text.size(), tabWidth);
}

std::size_t
    displayColumnAt(std::string_view text, std::size_t offset, std::size_t tabWidth)
{
    auto column = std::size_t {0};

    for (std::size_t index = 0; index < text.size() && index < offset;)
        column = advanceColumn(Utf8::next(text, index), column, tabWidth);

    return column;
}

std::size_t offsetAtDisplayColumn(std::string_view text,
                                  std::size_t column,
                                  std::size_t tabWidth)
{
    auto at = std::size_t {0};

    for (std::size_t index = 0; index < text.size();)
    {
        const auto start = index;
        const auto next = advanceColumn(Utf8::next(text, index), at, tabWidth);

        // Past the column: stay on this character rather than stepping over it,
        // so a column that falls inside a tab lands before the tab and never
        // between two of its bytes.
        if (next > column)
            return start;

        at = next;
    }

    return text.size();
}

void LineMap::setWrapColumns(const Document& document, std::size_t newColumns)
{
    if (newColumns == columns)
        return;

    columns = newColumns;
    rebuild(document);
}

void LineMap::setTabWidth(const Document& document, std::size_t width)
{
    if (width == tabColumns)
        return;

    tabColumns = width;
    rebuild(document);
}

void LineMap::breaksOf(std::string_view text, std::vector<std::size_t>& out) const
{
    out.clear();
    out.push_back(0);

    if (!wraps())
        return;

    auto rowStart = std::size_t {0};
    auto column = std::size_t {0};
    auto opportunity = std::size_t {0};

    for (std::size_t index = 0; index < text.size();)
    {
        if (isBreakOpportunity(text, index))
            opportunity = index;

        const auto start = index;
        const auto next = advanceColumn(Utf8::next(text, index), column, tabColumns);

        if (next <= columns || start == rowStart)
        {
            column = next;
            continue;
        }

        // A word longer than the whole width has nowhere to break, so it breaks
        // mid-word rather than overflowing. Anything else would put a single
        // long token — a URL, a base64 blob — off the right edge of a view
        // whose whole point is that nothing goes off it.
        rowStart = opportunity > rowStart ? opportunity : start;

        out.push_back(rowStart);
        opportunity = rowStart;

        // The new row starts its own tab stops, because it starts at the left
        // margin. Measuring them from the logical line's start would indent a
        // continuation row by whatever happened to precede the break.
        index = rowStart;
        column = 0;
    }
}

const std::vector<std::size_t>& LineMap::breaksOfLine(const Document& document,
                                                      std::size_t line) const
{
    if (cachedLine != line)
    {
        breaksOf(document.line(line), cachedBreaks);
        cachedLine = line;
    }

    return cachedBreaks;
}

std::size_t LineMap::rowsIn(std::string_view text,
                            std::vector<std::size_t>& scratch) const
{
    breaksOf(text, scratch);

    return scratch.size();
}

void LineMap::rebuild(const Document& document)
{
    ++rebuilds;

    invalidateCache();
    rowStarts.clear();

    if (!wraps())
        return;

    const auto lines = document.lineCount();

    rowStarts.reserve(lines + 1);

    auto scratch = std::vector<std::size_t> {};
    auto row = std::size_t {0};

    for (std::size_t line = 0; line < lines; ++line)
    {
        rowStarts.push_back(row);
        row += rowsIn(document.line(line), scratch);
    }

    rowStarts.push_back(row);
}

// Repairs the map around an edit instead of re-wrapping the file.
//
// Only the lines the edit landed on can have changed shape; every line after
// them keeps its row count and simply starts that many rows earlier or later.
// So the work is proportional to the edited lines plus the number of lines
// after them, rather than to the document's bytes — which for a large file is
// the difference between an integer pass over the line index per keystroke and
// re-measuring megabytes of text.
//
// Still linear in line count, for exactly the reason Document::reindexAfterEdit
// is: a flat vector of absolute positions. Both become logarithmic together
// when the rope lands, and neither is worth a Fenwick tree before then.
void LineMap::applyEdit(const Document& document, const TextEdit& edit)
{
    if (!wraps())
        return;

    const auto newLines = document.lineCount();
    const auto oldLines = rowStarts.empty() ? 0 : rowStarts.size() - 1;

    // The map was built for a different document, so there is nothing to
    // repair against. Reachable only if a caller edits behind the map's back.
    if (oldLines == 0 || newLines == 0)
    {
        rebuild(document);
        return;
    }

    invalidateCache();

    const auto delta = static_cast<std::ptrdiff_t>(newLines)
                       - static_cast<std::ptrdiff_t>(oldLines);

    // Clamped to the old document's last line: an insertion at the very end of
    // the file starts on a line that did not exist before it, and the line it
    // grew out of is the one that has to be re-measured.
    const auto first = std::min(document.lineAt(edit.start), oldLines - 1);
    const auto last = std::min(document.lineAt(edit.insertedEnd()), newLines - 1);

    const auto oldLast = static_cast<std::ptrdiff_t>(last) - delta;

    if (oldLast < static_cast<std::ptrdiff_t>(first)
        || oldLast >= static_cast<std::ptrdiff_t>(oldLines))
    {
        rebuild(document);
        return;
    }

    const auto oldEnd = static_cast<std::size_t>(oldLast);

    auto replacement = std::vector<std::size_t> {};
    replacement.reserve(last - first + 1);

    auto scratch = std::vector<std::size_t> {};
    auto row = rowStarts[first];

    for (auto line = first; line <= last; ++line)
    {
        row += rowsIn(document.line(line), scratch);
        replacement.push_back(row);
    }

    // What the replaced lines used to end at, so everything after them can be
    // shifted by the difference rather than recomputed.
    const auto before = rowStarts[oldEnd + 1];
    const auto rowDelta =
        static_cast<std::ptrdiff_t>(row) - static_cast<std::ptrdiff_t>(before);

    const auto begin = rowStarts.begin() + static_cast<std::ptrdiff_t>(first) + 1;
    const auto end = rowStarts.begin() + static_cast<std::ptrdiff_t>(oldEnd) + 2;

    const auto tail = rowStarts.erase(begin, end);
    const auto inserted =
        rowStarts.insert(tail, replacement.begin(), replacement.end());

    for (auto it = inserted + static_cast<std::ptrdiff_t>(replacement.size());
         it != rowStarts.end();
         ++it)
        *it = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(*it) + rowDelta);
}

std::size_t LineMap::rowCount(const Document& document) const
{
    if (!wraps() || rowStarts.empty())
        return document.lineCount();

    return rowStarts.back();
}

std::size_t LineMap::firstRowOfLine(std::size_t line) const
{
    if (!wraps() || rowStarts.empty())
        return line;

    return rowStarts[std::min(line, rowStarts.size() - 1)];
}

std::size_t LineMap::rowsInLine(std::size_t line) const
{
    if (!wraps() || rowStarts.empty() || line + 1 >= rowStarts.size())
        return 1;

    return rowStarts[line + 1] - rowStarts[line];
}

std::size_t LineMap::lineOfRow(const Document& document, std::size_t row) const
{
    const auto lines = document.lineCount();

    if (lines == 0)
        return 0;

    if (!wraps() || rowStarts.empty())
        return std::min(row, lines - 1);

    // The last line whose first row is at or before this one.
    const auto found = std::upper_bound(rowStarts.begin(), rowStarts.end(), row);
    const auto index = static_cast<std::size_t>(
        std::distance(rowStarts.begin(),
                      found == rowStarts.begin() ? found + 1 : found)
        - 1);

    return std::min(index, lines - 1);
}

VisualRow LineMap::row(const Document& document, std::size_t index) const
{
    auto result = VisualRow {};

    result.line = lineOfRow(document, index);

    const auto text = document.line(result.line);

    if (!wraps() || rowStarts.empty())
    {
        result.end = text.size();
        return result;
    }

    const auto& breaks = breaksOfLine(document, result.line);
    const auto first = firstRowOfLine(result.line);
    const auto within =
        std::min(index > first ? index - first : 0, breaks.size() - 1);

    result.start = breaks[within];
    result.end = within + 1 < breaks.size() ? breaks[within + 1] : text.size();

    return result;
}

std::size_t LineMap::rowOfOffset(const Document& document, std::size_t offset) const
{
    const auto line = document.lineAt(offset);

    if (!wraps() || rowStarts.empty())
        return line;

    const auto column = offset - document.offsetAt(line, 0);
    const auto& breaks = breaksOfLine(document, line);

    // The last row starting at or before the offset. An offset exactly on a
    // break belongs to the row it starts, which is where the caret sits after
    // moving right past the last character of the row above.
    const auto found = std::upper_bound(breaks.begin(), breaks.end(), column);
    const auto within = static_cast<std::size_t>(
        std::distance(breaks.begin(), found == breaks.begin() ? found + 1 : found)
        - 1);

    return firstRowOfLine(line) + within;
}

std::size_t LineMap::columnOfOffset(const Document& document,
                                    std::size_t offset) const
{
    const auto visual = row(document, rowOfOffset(document, offset));
    const auto rowStart = document.offsetAt(visual.line, visual.start);

    return displayColumnAt(visual.textIn(document),
                           offset > rowStart ? offset - rowStart : 0,
                           tabColumns);
}

std::size_t LineMap::offsetAtColumn(const Document& document,
                                    std::size_t rowIndex,
                                    std::size_t column) const
{
    const auto visual = row(document, rowIndex);
    const auto text = visual.textIn(document);

    auto within = offsetAtDisplayColumn(text, column, tabColumns);

    // A row that continues below ends exactly where the next one begins, so an
    // offset at its end names the same position as the start of the row under
    // it — and nothing here models caret affinity, so it would be drawn there.
    // Backing up one character keeps the caret on the row it was aimed at,
    // which matters more than the column being exact at the extreme right edge.
    if (within >= visual.length() && visual.end < document.line(visual.line).size())
        within = Utf8::previousBoundary(text, visual.length());

    return document.offsetAt(visual.line, visual.start + within);
}
} // namespace ecode
