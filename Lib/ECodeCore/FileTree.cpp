#include "FileTree.h"

#include <eacp/Core/Utils/StdPath.h>

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace ecode
{
using namespace eacp;

namespace
{
// An empty row to hand back for an out-of-range index, so a stale row number
// from a click that raced a refresh reads as nothing rather than as UB.
const auto emptyRow = FileTreeRow {};

struct Entry
{
    std::string name;
    bool isDirectory = false;
};

// Directories first, then files, each alphabetically and case-insensitively —
// which is what every file tree does and what makes one scannable.
bool orderedBefore(const Entry& a, const Entry& b)
{
    if (a.isDirectory != b.isDirectory)
        return a.isDirectory;

    const auto fold = [](std::string text)
    {
        std::transform(text.begin(),
                       text.end(),
                       text.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        return text;
    };

    return fold(a.name) < fold(b.name);
}

std::vector<Entry> readDirectory(const FilePath& directory)
{
    auto entries = std::vector<Entry> {};
    auto error = std::error_code {};

    // The non-throwing overloads throughout: a directory can vanish or become
    // unreadable between the listing and the walk, and neither is exceptional
    // enough to unwind through a paint.
    auto iterator = std::filesystem::directory_iterator {toStdPath(directory), error};

    if (error)
        return entries;

    for (const auto& entry: iterator)
    {
        auto entryError = std::error_code {};
        const auto directoryFlag = entry.is_directory(entryError);

        auto name = entry.path().filename().string();

        if (name.empty())
            continue;

        entries.push_back({std::move(name), !entryError && directoryFlag});
    }

    std::sort(entries.begin(), entries.end(), orderedBefore);

    return entries;
}
} // namespace

const FileTreeRow& FileTreeModel::row(std::size_t index) const
{
    return index < rows.size() ? rows[index] : emptyRow;
}

bool FileTreeModel::isExpanded(const FilePath& path) const
{
    return expanded.count(path.str()) > 0;
}

void FileTreeModel::setRoot(const FilePath& path)
{
    rootPath = path;

    // The root itself is always open; a tree whose one row is a closed folder
    // asks for a click to show anything at all.
    expanded.insert(path.str());

    refresh();
}

void FileTreeModel::refresh()
{
    rows.clear();

    if (rootPath.str().empty())
        return;

    appendChildren(rootPath, 0);
}

void FileTreeModel::appendChildren(const FilePath& directory, int depth)
{
    for (const auto& entry: readDirectory(directory))
    {
        const auto path = directory / entry.name;

        auto row = FileTreeRow {};

        row.path = path;
        row.name = entry.name;
        row.depth = depth;
        row.isDirectory = entry.isDirectory;
        row.expanded = entry.isDirectory && isExpanded(path);

        rows.push_back(std::move(row));

        // Depth-first, so a directory's contents follow it directly — which is
        // what makes the flattened order match what the indentation shows.
        // Only expanded directories are read, so the cost is proportional to
        // what is on screen rather than to the size of the tree.
        if (entry.isDirectory && isExpanded(path))
            appendChildren(path, depth + 1);
    }
}

bool FileTreeModel::toggle(std::size_t index)
{
    if (index >= rows.size())
        return false;

    const auto& target = rows[index];

    if (!target.isDirectory)
        return false;

    // Copied rather than referenced: refresh() clears the vector this row
    // lives in, so the key has to outlive it.
    const auto key = target.path.str();

    if (expanded.count(key) > 0)
        expanded.erase(key);
    else
        expanded.insert(key);

    refresh();

    return true;
}
} // namespace ecode
