#pragma once

#include <eacp/Core/Core.h>

#include <set>
#include <string>
#include <vector>

namespace ecode
{
// One visible row of the tree.
struct FileTreeRow
{
    eacp::FilePath path;

    // The last component, which is what gets drawn. Stored rather than derived
    // per frame because a virtualised list asks for it on every paint.
    std::string name;

    int depth = 0;
    bool isDirectory = false;
    bool expanded = false;
};

// A directory listing flattened into a list of rows.
//
// Flattened rather than held as a node tree because that is what a virtualised
// list needs: row 4,000 has to be answerable without walking 4,000 nodes. The
// flattening is rebuilt when something is expanded or collapsed — a click, not
// a frame — and only expanded directories are ever read, so an unexpanded
// node_modules or .git costs nothing.
//
// No file watching: eacp has none (gap 10 in PLAN.md), so a tree goes stale
// until something calls refresh(). The seam is deliberate — that is the call
// FSEvents will drive later.
class FileTreeModel
{
public:
    // Reads the directory and expands it. A path that cannot be read leaves
    // the model empty rather than throwing: an unreadable directory is a
    // normal thing to point an editor at, not an error to propagate.
    void setRoot(const eacp::FilePath& path);
    const eacp::FilePath& root() const { return rootPath; }

    std::size_t rowCount() const { return rows.size(); }
    const FileTreeRow& row(std::size_t index) const;

    // Expands a collapsed directory or collapses an expanded one. A file row
    // is left alone. Returns true when the flattening changed, so a caller can
    // avoid a repaint it does not need.
    bool toggle(std::size_t index);

    // Re-reads every expanded directory, keeping expansion state. What a file
    // watcher will call.
    void refresh();

    // Expansion is remembered by path, so a directory that disappears and
    // comes back — a branch switch, a build directory — returns expanded.
    bool isExpanded(const eacp::FilePath& path) const;

private:
    void appendChildren(const eacp::FilePath& directory, int depth);

    eacp::FilePath rootPath;
    std::vector<FileTreeRow> rows;
    std::set<std::string> expanded;
};
} // namespace ecode
