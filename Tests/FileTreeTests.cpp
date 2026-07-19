#include "Common.h"

#include <ECodeCore/FileTree.h>

#include <filesystem>
#include <fstream>

// Flattening a directory into rows, and what expanding one does to that.
//
// Against a real directory rather than a stubbed filesystem: the things most
// likely to be wrong here are what the platform does — ordering, what counts as
// a directory, what happens to an unreadable one — and a stub would only
// confirm my assumptions about those rather than test them.

using namespace nano;
using namespace ecode;

namespace
{
std::filesystem::path scratchTree(const std::string& name)
{
    auto root = std::filesystem::temp_directory_path() / ("ecode-tree-" + name);

    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    return root;
}

void touch(const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());

    auto out = std::ofstream {path};
    out << "x";
}

// Row names in order, which is what a reader of the sidebar actually sees.
std::vector<std::string> names(const FileTreeModel& model)
{
    auto found = std::vector<std::string> {};

    for (std::size_t i = 0; i < model.rowCount(); ++i)
        found.push_back(model.row(i).name);

    return found;
}

int indexOf(const FileTreeModel& model, const std::string& name)
{
    for (std::size_t i = 0; i < model.rowCount(); ++i)
        if (model.row(i).name == name)
            return static_cast<int>(i);

    return -1;
}
} // namespace

// Directories before files, each alphabetically. The order every file tree
// uses, and the one that makes a long listing scannable.
auto tOrdering = test("FileTree/directoriesComeBeforeFiles") = []
{
    auto root = scratchTree("order");

    touch(root / "zebra.txt");
    touch(root / "apple.txt");
    std::filesystem::create_directories(root / "src");
    std::filesystem::create_directories(root / "Assets");

    auto model = FileTreeModel {};
    model.setRoot(eacp::FilePath {root});

    check(names(model)
          == std::vector<std::string> {"Assets", "src", "apple.txt", "zebra.txt"});

    std::filesystem::remove_all(root);
};

// A collapsed directory's contents are not read at all, which is what keeps a
// .git or a build directory from costing anything until asked for.
auto tCollapsedIsNotRead = test("FileTree/collapsedDirectoriesContributeNoRows") = []
{
    auto root = scratchTree("collapsed");

    touch(root / "deep" / "one.txt");
    touch(root / "deep" / "two.txt");
    touch(root / "top.txt");

    auto model = FileTreeModel {};
    model.setRoot(eacp::FilePath {root});

    check(names(model) == std::vector<std::string> {"deep", "top.txt"});

    std::filesystem::remove_all(root);
};

// Expanding splices the children in directly after their parent, so the
// flattened order matches what the indentation claims.
auto tExpandInsertsAfterTheParent = test("FileTree/expandingInsertsChildrenAfterIt") = []
{
    auto root = scratchTree("expand");

    touch(root / "deep" / "one.txt");
    touch(root / "deep" / "two.txt");
    touch(root / "top.txt");

    auto model = FileTreeModel {};
    model.setRoot(eacp::FilePath {root});

    check(model.toggle(static_cast<std::size_t>(indexOf(model, "deep"))));

    check(names(model)
          == std::vector<std::string> {"deep", "one.txt", "two.txt", "top.txt"});

    // And the children are one level deeper, which is what the view indents by.
    check(model.row(0).depth == 0);
    check(model.row(1).depth == 1);
    check(model.row(3).depth == 0);

    std::filesystem::remove_all(root);
};

auto tCollapseRemovesThem = test("FileTree/collapsingRemovesTheChildrenAgain") = []
{
    auto root = scratchTree("collapse");

    touch(root / "deep" / "one.txt");

    auto model = FileTreeModel {};
    model.setRoot(eacp::FilePath {root});

    const auto deep = static_cast<std::size_t>(indexOf(model, "deep"));

    model.toggle(deep);
    check(model.rowCount() == 2);

    model.toggle(deep);
    check(model.rowCount() == 1);
    check(!model.row(0).expanded);

    std::filesystem::remove_all(root);
};

// Nested expansion: a grandchild only appears when both levels are open.
auto tNestedExpansion = test("FileTree/nestedDirectoriesExpandIndependently") = []
{
    auto root = scratchTree("nested");

    touch(root / "a" / "b" / "leaf.txt");

    auto model = FileTreeModel {};
    model.setRoot(eacp::FilePath {root});

    model.toggle(static_cast<std::size_t>(indexOf(model, "a")));
    check(names(model) == std::vector<std::string> {"a", "b"});

    model.toggle(static_cast<std::size_t>(indexOf(model, "b")));
    check(names(model) == std::vector<std::string> {"a", "b", "leaf.txt"});
    check(model.row(2).depth == 2);

    std::filesystem::remove_all(root);
};

// Toggling a file does nothing and says so, so a click on one falls through to
// opening it rather than being swallowed as an expansion.
auto tTogglingAFileDoesNothing = test("FileTree/togglingAFileChangesNothing") = []
{
    auto root = scratchTree("file-toggle");

    touch(root / "top.txt");

    auto model = FileTreeModel {};
    model.setRoot(eacp::FilePath {root});

    check(!model.toggle(0));
    check(model.rowCount() == 1);

    std::filesystem::remove_all(root);
};

// Expansion is remembered by path, so a directory that disappears and returns —
// a branch switch, a build directory — comes back open rather than collapsed.
auto tExpansionSurvivesRefresh = test("FileTree/expansionSurvivesADirectoryComingBack") = []
{
    auto root = scratchTree("survive");

    touch(root / "gone" / "one.txt");

    auto model = FileTreeModel {};
    model.setRoot(eacp::FilePath {root});

    model.toggle(static_cast<std::size_t>(indexOf(model, "gone")));
    check(model.rowCount() == 2);

    std::filesystem::remove_all(root / "gone");
    model.refresh();
    check(model.rowCount() == 0);

    touch(root / "gone" / "one.txt");
    model.refresh();

    check(model.rowCount() == 2);
    check(model.row(0).expanded);

    std::filesystem::remove_all(root);
};

// A refresh picks up what changed underneath, which is what a file watcher
// will drive once eacp has one.
auto tRefreshSeesNewFiles = test("FileTree/refreshPicksUpNewFiles") = []
{
    auto root = scratchTree("refresh");

    touch(root / "one.txt");

    auto model = FileTreeModel {};
    model.setRoot(eacp::FilePath {root});
    check(model.rowCount() == 1);

    touch(root / "two.txt");
    model.refresh();

    check(model.rowCount() == 2);

    std::filesystem::remove_all(root);
};

// A root that cannot be read is a normal thing to point an editor at — a path
// that was deleted, a permission denied — and must not throw out of what is
// often a paint or a click.
auto tUnreadableRootIsEmpty = test("FileTree/anUnreadableRootIsSimplyEmpty") = []
{
    auto model = FileTreeModel {};

    model.setRoot(eacp::FilePath {"/no/such/directory/anywhere"});

    check(model.rowCount() == 0);
};

// An index from a click that raced a refresh must read as nothing rather than
// walk off the end.
auto tOutOfRangeRowIsEmpty = test("FileTree/anOutOfRangeRowReadsAsEmpty") = []
{
    auto model = FileTreeModel {};

    check(model.row(0).name.empty());
    check(!model.row(9999).isDirectory);
    check(!model.toggle(9999));
};
