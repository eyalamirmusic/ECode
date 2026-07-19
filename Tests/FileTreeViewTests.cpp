#include <ECodeUI/FileTreeView.h>
#include <ECodeUI/WidgetHost.h>

#include <NanoTest/NanoTest.h>

#include <filesystem>
#include <fstream>

// What a click on a row means.
//
// FileTreeTests covers the flattening and ScrollTests covers the routing; the
// gap between them is this view's wiring — that a directory expands, a file
// opens, and neither does the other. Driven through WidgetHost at real
// coordinates rather than by calling the handler, because "which row is under
// this point" is half of what could be wrong and calling the handler directly
// would assume it.

using namespace nano;
using namespace ecode;
using namespace eacp;

namespace
{
constexpr auto rowHeight = 22.f;

std::filesystem::path scratchTree(const std::string& name)
{
    auto root = std::filesystem::temp_directory_path() / ("ecode-view-" + name);

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

Graphics::MouseEvent clickAt(float x, float y)
{
    auto event = Graphics::MouseEvent {};

    event.pos = Graphics::Point {x, y};
    event.clickCount = 1;

    return event;
}

// Centre of row n, in the tree's coordinates.
float centreOfRow(int row)
{
    return static_cast<float>(row) * rowHeight + rowHeight * 0.5f;
}
} // namespace

auto tClickingADirectoryExpandsIt = test("FileTreeView/clickingADirectoryExpandsIt") = []
{
    auto root = scratchTree("expand");

    touch(root / "dir" / "inner.txt");

    auto theme = ChromeTheme {};
    auto view = FileTreeView {theme};
    auto host = WidgetHost {};

    auto opened = FilePath {};
    view.onFileChosen = [&opened](const FilePath& path) { opened = path; };

    view.setRoot(FilePath {root});

    host.setRoot(view);
    view.setBounds({0.f, 0.f, 240.f, 400.f});

    check(view.treeModel().rowCount() == 1);

    host.mouseDown(clickAt(60.f, centreOfRow(0)));

    // The directory opened, and no file was reported as chosen.
    check(view.treeModel().rowCount() == 2);
    check(opened.str().empty());

    // Clicking it again closes it.
    host.mouseDown(clickAt(60.f, centreOfRow(0)));
    check(view.treeModel().rowCount() == 1);

    std::filesystem::remove_all(root);
};

auto tClickingAFileOpensIt = test("FileTreeView/clickingAFileReportsIt") = []
{
    auto root = scratchTree("open");

    touch(root / "one.txt");
    touch(root / "two.txt");

    auto theme = ChromeTheme {};
    auto view = FileTreeView {theme};
    auto host = WidgetHost {};

    auto opened = FilePath {};
    view.onFileChosen = [&opened](const FilePath& path) { opened = path; };

    view.setRoot(FilePath {root});

    host.setRoot(view);
    view.setBounds({0.f, 0.f, 240.f, 400.f});

    host.mouseDown(clickAt(60.f, centreOfRow(1)));

    check(opened == FilePath {root / "two.txt"});

    // And the row count is untouched: opening a file is not an expansion.
    check(view.treeModel().rowCount() == 2);

    std::filesystem::remove_all(root);
};

// A click in the empty space below the last row must do nothing at all —
// neither open the last file nor expand the last directory.
auto tClickingBelowTheRowsDoesNothing =
    test("FileTreeView/clickingBelowTheLastRowDoesNothing") = []
{
    auto root = scratchTree("empty-space");

    touch(root / "one.txt");

    auto theme = ChromeTheme {};
    auto view = FileTreeView {theme};
    auto host = WidgetHost {};

    auto opens = 0;
    view.onFileChosen = [&opens](const FilePath&) { ++opens; };

    view.setRoot(FilePath {root});

    host.setRoot(view);
    view.setBounds({0.f, 0.f, 240.f, 400.f});

    host.mouseDown(clickAt(60.f, 300.f));

    check(opens == 0);

    std::filesystem::remove_all(root);
};

// Expanding a directory makes the content taller, and the scroll range has to
// follow immediately — otherwise the rows revealed below the fold cannot be
// reached until something else triggers a layout.
auto tExpandingUpdatesTheScrollRange =
    test("FileTreeView/expandingADirectoryGrowsTheScrollRange") = []
{
    auto root = scratchTree("range");

    // Far more children than fit in the viewport below.
    for (auto i = 0; i < 60; ++i)
        touch(root / "dir" / ("file-" + std::to_string(i) + ".txt"));

    auto theme = ChromeTheme {};
    auto view = FileTreeView {theme};
    auto host = WidgetHost {};

    view.setRoot(FilePath {root});

    host.setRoot(view);
    view.setBounds({0.f, 0.f, 240.f, 200.f});

    // One row of content in a 200-point viewport: nothing to scroll.
    check(view.maxScroll() == 0.f);

    host.mouseDown(clickAt(60.f, centreOfRow(0)));

    check(view.treeModel().rowCount() == 61);
    check(view.maxScroll() > 0.f);
};

// The row a click lands on has to account for the scroll, or every click below
// the fold opens the wrong file — the kind of bug that looks like a hit-test
// being off by a constant until you notice the constant is the scroll offset.
auto tClicksAccountForScrolling =
    test("FileTreeView/clicksLandOnTheRightRowWhenScrolled") = []
{
    auto root = scratchTree("scrolled");

    for (auto i = 0; i < 40; ++i)
        touch(root / ("file-" + std::to_string(i) + ".txt"));

    auto theme = ChromeTheme {};
    auto view = FileTreeView {theme};
    auto host = WidgetHost {};

    auto opened = FilePath {};
    view.onFileChosen = [&opened](const FilePath& path) { opened = path; };

    view.setRoot(FilePath {root});

    host.setRoot(view);
    view.setBounds({0.f, 0.f, 240.f, 200.f});

    // Scroll down by exactly five rows, then click the top visible one.
    view.setScrollPosition(rowHeight * 5.f);
    host.mouseDown(clickAt(60.f, centreOfRow(0)));

    // Sorted case-insensitively as strings, so file-0, file-1, file-10, ...
    // Whatever the order, the point is that it is row 5 and not row 0.
    check(opened == view.treeModel().row(5).path);
    check(opened != view.treeModel().row(0).path);

    std::filesystem::remove_all(root);
};

// Clicking a directory leaves focus on the tree, so the arrow keys drive it
// from there. A selection that walks off the bottom with nothing scrolling to
// follow is the keyboard equivalent of the caret scrolling out of view.
auto tKeyboardSelectionStaysVisible =
    test("FileTreeView/keyboardSelectionScrollsIntoView") = []
{
    auto root = scratchTree("keyboard");

    for (auto i = 0; i < 40; ++i)
        touch(root / ("file-" + std::to_string(i) + ".txt"));

    auto theme = ChromeTheme {};
    auto view = FileTreeView {theme};
    auto host = WidgetHost {};

    view.setRoot(FilePath {root});

    host.setRoot(view);
    view.setBounds({0.f, 0.f, 240.f, 200.f});   // nine rows fit

    // Click the first row, which focuses the tree.
    host.mouseDown(clickAt(60.f, centreOfRow(0)));

    auto down = Graphics::KeyEvent {};
    down.keyCode = Graphics::KeyCode::DownArrow;

    // Walk past the bottom of the viewport.
    for (auto i = 0; i < 20; ++i)
        host.keyDown(down);

    check(view.scrollPosition() > 0.f);

    // And back up again: the same rule has to work in the direction that
    // scrolls towards zero, which is a different branch of scrollToShow.
    auto up = Graphics::KeyEvent {};
    up.keyCode = Graphics::KeyCode::UpArrow;

    for (auto i = 0; i < 25; ++i)
        host.keyDown(up);

    check(view.scrollPosition() == 0.f);

    std::filesystem::remove_all(root);
};
