#include "Document.h"

#include <algorithm>
#include <cstring>

namespace ecode
{
// One counter for every document in the process, so no two states ever share a
// revision — see the comment on revision(). Not atomic, because a Document is
// touched from the main thread only: edits arrive from key events and the disk
// poll is an NSTimer on the main run loop.
std::uint64_t Document::nextRevision()
{
    static auto counter = std::uint64_t {0};

    return ++counter;
}

Document Document::fromText(std::string text)
{
    auto document = Document {};
    document.contents = std::move(text);
    document.indexLines();

    return document;
}

Document::Document()
{
    indexLines();
}

Document Document::fromFile(const eacp::FilePath& path)
{
    return fromText(eacp::Files::readFile(path));
}

void Document::indexLines()
{
    lineStarts.clear();

    // Deliberately not reserved from an estimated bytes-per-line. Measured, it
    // saves nothing — the vector's doubling is memcpy over trivially-copyable
    // integers, and next to the scan below it does not register — while a guess
    // of one line per 32 bytes over-allocates 25 MB on a 100 MB file that turns
    // out to be one long line.

    // Even an empty document has one line, so there is somewhere to put a caret.
    lineStarts.push_back(0);

    // memchr rather than a byte loop: it is the one part of opening a file that
    // is proportional to its bytes rather than to its lines, and the library's
    // version is vectorised where a `!= '\n'` loop is not.
    const auto* const base = contents.data();
    const auto* at = base;
    const auto* const end = base + contents.size();

    while (at != end)
    {
        const auto* const newline =
            static_cast<const char*>(std::memchr(at, '\n', std::size_t(end - at)));

        if (newline == nullptr)
            break;

        const auto offset = std::size_t(newline - base);

        // A newline at the very end terminates the last line rather than
        // starting an empty one after it.
        if (offset + 1 < contents.size())
            lineStarts.push_back(offset + 1);

        at = newline + 1;
    }

    // Not computed here, though the scan above passes every line and could.
    // Opening a file is the one moment a large document is definitely going to
    // be looked at, and the longest line is wanted by a horizontal scroll range
    // that may never be asked for. Leaving it unknown makes the open pay for
    // what it is actually doing.
    widestKnown = false;
}

// The whole answer, from the index alone — no byte scanning, since a line's
// length is the gap between two adjacent starts.
void Document::rescanWidest() const
{
    ++rescans;

    widest = 0;
    widestStart = 0;

    for (std::size_t index = 0; index < lineStarts.size(); ++index)
    {
        const auto length = line(index).size();

        if (length > widest)
        {
            widest = length;
            widestStart = lineStarts[index];
        }
    }

    widestKnown = true;
}

std::size_t Document::widestLine() const
{
    if (!widestKnown)
        rescanWidest();

    return widest;
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
    // Here rather than in replace() and apply(): every mutation reaches this
    // function, so nothing can change the text without the revision following.
    currentRevision = nextRevision();

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

    updateWidestAfterEdit(start, oldEnd, inserted);
}

// Keeps the longest line across an edit, in work proportional to the edit rather
// than to the file.
//
// Two facts do all of it. Every line the edit did not touch still has the length
// it had, so a maximum found among the touched ones that beats the old record is
// the new record outright. And if it does not beat it, the old record still
// stands provided its own line survived the edit unchanged — which is a question
// about one line, not about all of them.
//
// The pessimistic answer is "stale", and stale is correct: widestLine() rescans.
// So every branch here may only ever be too cautious.
void Document::updateWidestAfterEdit(std::size_t start,
                                     std::size_t oldEnd,
                                     std::string_view inserted)
{
    if (!widestKnown)
        return;

    // The lines the edit's text now occupies. Derived from offsets against the
    // repaired index rather than from how many entries were erased and inserted
    // above: a deletion that merges two lines, an insertion of a thousand, and a
    // replacement that does both are all just "the lines spanning this range".
    const auto firstTouched = lineAt(start);
    const auto lastTouched = lineAt(start + inserted.size());

    auto touchedMax = line(firstTouched).size();
    auto touchedStart = lineStarts[firstTouched];

    for (auto index = firstTouched + 1; index <= lastTouched; ++index)
    {
        const auto length = line(index).size();

        if (length > touchedMax)
        {
            touchedMax = length;
            touchedStart = lineStarts[index];
        }
    }

    if (touchedMax >= widest)
    {
        widest = touchedMax;
        widestStart = touchedStart;

        return;
    }

    // The record is elsewhere in the file, so it survives if its line does.
    // Shifting its offset is the same arithmetic the line starts after the edit
    // just got; an offset inside the replaced span has no answer.
    if (widestStart >= oldEnd)
        widestStart =
            static_cast<std::size_t>(static_cast<std::ptrdiff_t>(widestStart)
                                     + static_cast<std::ptrdiff_t>(inserted.size())
                                     - static_cast<std::ptrdiff_t>(oldEnd - start));
    else if (widestStart > start)
        widestKnown = false;

    if (!widestKnown)
        return;

    // And then it is checked rather than trusted. The shift above reasons about
    // an edit's geometry, which is where this class has been wrong before; a
    // slip there must not be able to report a maximum that no line has. Dropping
    // the record is always available and always correct, so the check is free to
    // be strict.
    //
    // Only the last of the three conditions can change the answer, and that is
    // worth writing down rather than leaving for the next reader to discover
    // from a green mutation (PLAN.md §9). What makes `widest` right is that some
    // line still has that length — nothing above depends on *which* line. The
    // first two keep the anchor honest so that a later edit shifts a real line
    // start rather than drifting; they are a statement about the next edit, not
    // about this answer.
    const auto at =
        std::lower_bound(lineStarts.begin(), lineStarts.end(), widestStart);
    const auto index =
        static_cast<std::size_t>(std::distance(lineStarts.begin(), at));

    if (at == lineStarts.end() || *at != widestStart || line(index).size() != widest)
        widestKnown = false;
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
