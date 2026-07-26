#include <ECodeUI/Chrome.h>
#include <ECodeUI/CommandPalette.h>
#include <ECodeUI/ContextMenu.h>
#include <ECodeUI/EditorGroupView.h>
#include <ECodeUI/EditorWidget.h>
#include <ECodeUI/FileTreeView.h>
#include <ECodeUI/FindBar.h>
#include <ECodeUI/Keymap.h>
#include <ECodeUI/MenuBuilder.h>
#include <ECodeUI/Splitter.h>
#include <ECodeUI/Theme.h>
#include <ECodeUI/WidgetHost.h>
#include <ECodeCore/EditorGroups.h>
#include <ECodeSyntax/SyntaxHighlighter.h>

#include <eacp/Core/App/App.h>
#include <eacp/Core/App/Clipboard.h>
#include <eacp/GPU/GPU.h>
#include <eacp/Graphics/Graphics.h>
#include <eacp/Sprites/Sprites.h>

#include <optional>
#include <string>

using namespace eacp;

namespace ecode
{
// The editor window: a workspace of open files, drawn on the GPU, highlighted,
// typed in, saved.
//
// The chrome is a widget tree now rather than hardcoded rectangles, so this
// file is down to what an application shell actually owns — the GPU resources
// that depend on the display, the command chords, and the two timers.

// `ECode <path>…`, falling back to this file. It is what makes the editor
// testable against scratch files rather than against its own source.
//
// Every path given rather than only the first, now that there is somewhere to
// put the rest: `ECode *.cpp` is how anyone would expect to open a directory's
// worth of files, and the workspace opens each into its own tab.
eacp::Vector<FilePath> filesToOpen()
{
    const auto& args = Apps::getAppEnvironment().commandLineArgs;

    auto paths = eacp::Vector<FilePath> {};

    for (auto index = 1; index < args.size(); ++index)
        if (!args[index].empty())
            paths.add(FilePath {args[index]});

    if (paths.size() == 0)
        paths.add(FilePath {__FILE__});

    return paths;
}

// The window's whole layout. Activity bar and sidebar off the left, status bar
// off the bottom, and the editor groups sharing what is left — which is exactly
// what Rect's splitters express, and only reads correctly now that they are
// y-down.
//
// The tab strip is no longer here: it belongs to a group, and there is one strip
// per group.
struct WindowLayout final : Widget
{
    WindowLayout(EditorGroups& groupsToShow,
                 const CommandRegistry& commands,
                 const Keymap& keymap)
        : groups(&groupsToShow)
        , palette(theme, commands, keymap)
        , contextMenu(theme, commands, keymap)
    {
        sidebarSplitter.setPosition(activityBarWidth + sidebarWidth);

        sidebarSplitter.onMoved = [this](float)
        {
            layout();
            repaint();
        };

        rebuildGroups();
    }

    // --- the editor groups ------------------------------------------------

    // One pane per group, remade whenever there is a different number of them.
    //
    // Remade rather than adjusted: a split is a deliberate, occasional act, and
    // the whole cost of starting over is the row caches of the panes that did
    // not change — one frame of laying out a screenful of text each. Keeping
    // them would mean matching old views to new groups by identity, which is
    // bookkeeping to be got wrong in exchange for something nobody can see.
    void rebuildGroups()
    {
        views.clear();
        seams.clear();
        weights.clear();

        const auto count = std::max(1, groups->count());

        for (auto index = 0; index < count; ++index)
        {
            views.createNew(theme, groups->at(index));

            // Even shares. A split that preserved the ratios would have to
            // decide which pane pays for the new one, and "all of them" — which
            // is what an even redistribution means — is the only answer that
            // does not depend on which pane was split from.
            weights.add(1.f / static_cast<float>(count));
        }

        for (auto index = 0; index + 1 < count; ++index)
        {
            auto& seam = seams.createNew(theme, Splitter::Orientation::Vertical);

            seam.onMoved = [this, index](float position)
            { moveSeam(index, position); };
        }

        rebuildChildren();
        layout();
    }

    int groupCount() const { return views.size(); }

    EditorGroupView& groupView(int index)
    {
        return *views[std::clamp(index, 0, views.size() - 1)];
    }

    EditorGroupView& activeGroupView() { return groupView(groups->activeIndex()); }
    EditorWidget& activeEditor() { return activeGroupView().editor(); }

    // Which pane a point is in, or -1. What a click needs in order to make the
    // group it landed in the active one — focus alone cannot answer it, since a
    // click on a tab strip focuses nothing.
    int groupViewAt(const Graphics::Point& point) const
    {
        for (auto index = 0; index < views.size(); ++index)
            if (views[index]->bounds().contains(point))
                return index;

        return -1;
    }

    // Pushes each group's workspace into its own pane, and marks the one being
    // worked in.
    void refreshGroups()
    {
        for (auto index = 0; index < views.size(); ++index)
        {
            views[index]->refresh();
            views[index]->setGroupActive(index == groups->activeIndex());
        }
    }

    // Each pane gets its own TextRenderer off the shared atlas. Null before the
    // view is on a display, which every pane tolerates.
    void setAtlas(Text::GlyphAtlas* atlas, const TextTheme& textTheme, float scale)
    {
        for (auto& view: views)
            view->setAtlas(atlas, textTheme, scale);
    }

    // Whether another pane would still leave every one of them usable. Asked by
    // the split command's predicate, so a window too narrow to divide again
    // greys the command out rather than making a pane nobody can read.
    bool canSplit() const
    {
        const auto each =
            groupRow.w / static_cast<float>(std::max(1, views.size() + 1));

        return each >= minEditorWidth;
    }

    // A seam dragged: the two panes either side of it trade width and nothing
    // else moves. Kept as shares of the row rather than as positions, so a
    // window resize divides what is there proportionally instead of pushing
    // every seam against its limit.
    void moveSeam(int seam, float position)
    {
        if (seam < 0 || seam + 1 >= views.size() || groupRow.w <= 0.f)
            return;

        const auto pair = weights[seam] + weights[seam + 1];
        const auto left = (position - views[seam]->bounds().x) / groupRow.w;

        weights[seam] = std::clamp(left, 0.f, pair);
        weights[seam + 1] = pair - weights[seam];

        layout();
        repaint();
    }

    // Z-order, and it is the whole reason this is a function rather than a
    // constructor body: the panes come and go, and everything painted over them
    // has to be re-added behind them each time.
    void rebuildChildren()
    {
        removeAllChildren();

        addChild(activityBar);
        addChild(sidebar);
        addChild(files);
        addChild(status);

        for (auto& view: views)
            addChild(*view);

        // After the panes, so a grab band is found before the two editors it
        // sits between — widgetAt takes the last child containing the point.
        for (auto& seam: seams)
            addChild(*seam);

        addChild(sidebarSplitter);

        // After the editors, so it draws over the text it is searching.
        addChild(find);

        addChild(palette);

        // Last of all: a context menu can be opened over the palette, so it has
        // to paint above it and be found before it.
        addChild(contextMenu);
    }

    void layout() override
    {
        // The palette is laid *over* the window rather than given a slice of
        // it: it is an overlay, and covering the whole window is what makes a
        // click anywhere outside its box dismiss it without a second widget to
        // catch those clicks.
        palette.setBounds(bounds());

        // Same reasoning as the palette: laid over the window rather than sized
        // to its own box, so a click anywhere outside the box dismisses it.
        contextMenu.setBounds(bounds());

        auto area = bounds();

        // The status bar comes off first so it spans the whole window, under
        // the sidebar as well as the editor. Taking it after the left columns
        // would leave it starting at the editor's edge, which is what the
        // hardcoded chrome did and what VSCode does not.
        status.setBounds(area.removeFromBottom(statusBarHeight));

        activityBar.setBounds(area.removeFromLeft(activityBarWidth));

        // The divider's position is measured from the window's left edge, which
        // is what the splitter reports and what a config file would store — so
        // the sidebar is whatever lies between the activity bar and it, rather
        // than a width kept in step with it separately.
        sidebarSplitter.setLimits(
            area.x + minSidebarWidth,
            std::max(area.x + minSidebarWidth, area.right() - minEditorWidth));

        const auto sidebarArea =
            area.removeFromLeft(sidebarSplitter.position() - area.x);

        sidebar.setBounds(sidebarArea);
        files.setBounds(sidebarArea);

        // Straddles the seam rather than taking a slice of it, so neither pane
        // loses width to the divider and the grab band reaches into both.
        sidebarSplitter.setBounds(
            {sidebarArea.right() - Splitter::grabThickness * 0.5f,
             area.y,
             Splitter::grabThickness,
             area.h});

        // What is left is shared between the groups. Each pane puts its own tab
        // strip along its own top edge, so a strip starts where its pane does
        // rather than spanning the window.
        groupRow = area;
        layoutGroups();

        // Over the *active* pane's top-right corner rather than given a slice
        // of it. The bar covers a few lines instead of pushing the file down,
        // which is what stops the line being read from moving the moment ⌘F is
        // pressed.
        //
        // Its bounds are the box itself and not the pane's width, or it would
        // swallow every click meant for the text beneath it — widgetAt only asks
        // whether a point is inside the bounds.
        const auto text = activeGroupView().editorArea();
        const auto barWidth = std::min(find.barWidth(), text.w);

        find.setBounds({std::max(text.x, text.right() - findMargin - barWidth),
                        text.y,
                        barWidth,
                        find.barHeight()});
    }

    void layoutGroups()
    {
        auto x = groupRow.x;

        for (auto index = 0; index < views.size(); ++index)
        {
            // The last pane takes what is left rather than its own share, so
            // rounding cannot leave a column of background down the far edge.
            const auto width = index + 1 == views.size()
                                   ? groupRow.right() - x
                                   : groupRow.w * weights[index];

            views[index]->setBounds({x, groupRow.y, width, groupRow.h});

            x += width;
        }

        // A second pass, because a seam's limits are the far edges of the two
        // panes it divides and the one to its right has only just been placed.
        for (auto index = 0; index < seams.size(); ++index)
        {
            const auto left = views[index]->bounds();
            const auto right = views[index + 1]->bounds();
            const auto divider = right.x;

            seams[index]->setPosition(divider);

            seams[index]->setLimits(
                left.x + minEditorWidth,
                std::max(left.x + minEditorWidth, right.right() - minEditorWidth));

            // Straddles the seam rather than taking a slice of it, so neither
            // pane loses width to the divider and the grab band reaches into
            // both.
            seams[index]->setBounds({divider - Splitter::grabThickness * 0.5f,
                                     groupRow.y,
                                     Splitter::grabThickness,
                                     groupRow.h});
        }
    }

    static constexpr auto activityBarWidth = 48.f;

    // Where the divider starts. Only the initial value now — after that the
    // splitter owns it.
    static constexpr auto sidebarWidth = 240.f;

    // Narrow enough to be worth collapsing to and wide enough to still show a
    // filename. The editor's floor matters more: a sidebar dragged over the
    // whole window would leave nothing to type in and no obvious way back.
    static constexpr auto minSidebarWidth = 120.f;

    // The floor for a single editor pane, and now for every one of them: a
    // sidebar dragged over the whole window would leave nothing to type in, and
    // a window split five ways at 200 points each is the same mistake spread
    // out.
    static constexpr auto minEditorWidth = 240.f;
    static constexpr auto statusBarHeight = 22.f;

    // Clear of the right edge, where a vertical scrollbar will go.
    static constexpr auto findMargin = 14.f;

    ChromeTheme theme;

    Panel activityBar {theme.activityBar};

    // The sidebar's background is a panel behind the tree rather than the
    // tree's own fill, so the empty space below the last row is still sidebar.
    Panel sidebar {theme.sidebar};
    FileTreeView files {theme};
    StatusBar status {theme};

    // Never null and never owned: the groups outlive the window layout, and are
    // constructed before it for exactly that reason.
    EditorGroups* groups;

    OwnedVector<EditorGroupView> views;
    OwnedVector<Splitter> seams;

    // Each pane's share of the row, summing to one. See moveSeam.
    Vector<float> weights;

    // What the panes divide between them, kept from the last layout so a seam
    // drag can turn a position back into shares.
    Graphics::Rect groupRow;

    Splitter sidebarSplitter {theme, Splitter::Orientation::Vertical};

    FindBar find {theme};
    CommandPalette palette;
    ContextMenu contextMenu;
};

struct EditorView final : GPU::GPUView
{
    EditorView()
    {
        // Text is grayscale-antialiased in the atlas already, so multisampling
        // the surface buys nothing and costs bandwidth. eacp defaults to 4.
        setSampleCount(1);
        setHandlesMouseEvents(true);
        setGrabsFocusOnMouseDown(true);

        host.setRoot(layout);
        layout.onRepaintNeeded = [this] { repaint(); };

        groups.onGroupsChanged = [this] { rebuildGroupViews(); };
        groups.onChanged = [this] { showActiveFile(); };

        registerCommands();
        bindKeys();
        connectFindBar();
        connectGroups();

        layout.palette.onClosed = [this]
        {
            // Back to whatever was being worked in. Falling back to the editor
            // rather than to nothing: a window with focus nowhere swallows the
            // next keystroke silently.
            host.setFocus(focusBeforePalette != nullptr ? focusBeforePalette
                                                        : &layout.activeEditor());
            repaint();
        };

        for (const auto& path: filesToOpen())
            openFile(path);

        // Left on the first, not the last: the first name on the command line
        // is the one that was meant, and the rest are context.
        groups.active().activate(0);

        // The tree is rooted at the open file's directory, which is the closest
        // thing to a project until there is a folder-open command.
        layout.files.setRoot(activeFile().path().parentDirectory());

        // The chrome was pushed once per file as the workspace changed, but the
        // editor widget only learned its renderer afterwards, so the strip is
        // rebuilt once here with everything in place.
        updateChrome();

        layout.files.onFileChosen = [this](const FilePath& path)
        {
            openFile(path);

            // Focus follows the open: the point of clicking a file is to type
            // in it, and leaving focus in the tree means the first keystroke
            // moves the selection instead.
            host.setFocus(&layout.activeEditor());
            repaint();
        };

        connectContextMenu();

        // The editor starts focused; a window that opens with no caret reads
        // as broken.
        host.setFocus(&layout.activeEditor());
    }

    Editor& editor() { return groups.editor(); }
    TextFile& activeFile() { return groups.active().active().file; }

    // Pushes whatever the groups made active into the chrome around them.
    //
    // Every route that opens, closes or switches a file arrives here through
    // EditorGroups::onChanged rather than by remembering to call it — which is
    // the difference between a tab switch that always redraws the strip and one
    // that redraws it wherever somebody thought to.
    void showActiveFile()
    {
        pendingCloseGroup = -1;

        // Which points each pane at its own active file and puts the active tab
        // in each strip; see updateChrome.
        updateChrome();
        repaint();
    }

    // A group was added or removed, so there is a different number of panes.
    //
    // Every pane is destroyed and remade, which means everything the host is
    // holding a pointer into is about to stop existing — and the clear has to
    // happen while those widgets are still there, since dropping a hover tells
    // the widget it was left.
    void rebuildGroupViews()
    {
        host.forgetTargets();

        layout.rebuildGroups();
        layout.setAtlas(atlas.get(), textTheme, builtAtScale);

        connectGroups();

        host.setFocus(&layout.activeEditor());
        repaint();
    }

    // Wires every pane to the group it shows. Re-run after a rebuild rather
    // than installed once, because the index a callback needs is the pane's
    // position and the panes are made fresh each time.
    void connectGroups()
    {
        for (auto index = 0; index < layout.groupCount(); ++index)
        {
            auto& view = layout.groupView(index);

            view.tabBar().onTabSelected = [this, index](int tab)
            {
                groups.activate(index);
                groups.at(index).activate(tab);

                // Focus follows the click, for the same reason it follows one
                // in the tree: the point of switching to a file is to type in
                // it.
                host.setFocus(&layout.activeEditor());
            };

            view.tabBar().onTabClosed = [this, index](int tab)
            { closeFile(index, tab); };

            view.editor().onStateChanged = [this] { updateChrome(); };

            view.editor().onContextMenuRequested = [this](const Graphics::Point& at)
            { showContextMenu(at); };
        }
    }

    // The find bar reports what was typed and which button was pressed; the
    // editor widget owns the search itself, since it has the document to search
    // and the scroll offset that brings a hit into view. This is the wiring
    // between the two, and the counter is pushed back after every one of them
    // because the bar cannot count what it does not hold.
    void connectFindBar()
    {
        layout.find.onQueryChanged = [this]
        {
            layout.activeEditor().setSearchQuery(layout.find.query(), searchOrigin);
            updateFindCount();
            repaint();
        };

        layout.find.onFindNext = [this]
        {
            layout.activeEditor().findNext();
            updateFindCount();
            repaint();
        };

        layout.find.onFindPrevious = [this]
        {
            layout.activeEditor().findPrevious();
            updateFindCount();
            repaint();
        };

        layout.find.onReplace = [this]
        {
            layout.activeEditor().replaceCurrent(layout.find.replacement());

            updateFindCount();
            updateChrome();
            repaint();
        };

        layout.find.onReplaceAll = [this]
        {
            layout.activeEditor().replaceAllMatches(layout.find.replacement());

            updateFindCount();
            updateChrome();
            repaint();
        };

        layout.find.onFocusRequested = [this](Widget& target)
        {
            host.setFocus(&target);
            repaint();
        };

        layout.find.onClosed = [this]
        {
            // The highlight goes with the bar. Leaving it up would mean a file
            // covered in orange with nothing on screen explaining why.
            layout.activeEditor().clearSearch();

            host.setFocus(&layout.activeEditor());

            // The bar no longer occupies the corner it did.
            layout.layout();
            repaint();
        };
    }

    // The editor's own menu. Ordered the way VSCode's is — the clipboard first,
    // because that is what a right-click is nearly always for.
    static eacp::Vector<std::string> editorMenuCommands()
    {
        return {commands::editCut,
                commands::editCopy,
                commands::editPaste,
                {},
                commands::editSelectAll,
                {},
                "edit.undo",
                "edit.redo",
                {},
                "find.show"};
    }

    // A right-click in any pane. Wired per pane in connectGroups, since the
    // panes are remade on every split and a callback installed once would be
    // installed on a widget that no longer exists.
    void showContextMenu(const Graphics::Point& at)
    {
        // Focus moves to the menu, so the caret stops blinking under it and
        // the arrow keys drive the menu rather than the document.
        focusBeforeMenu = host.focused();

        layout.contextMenu.show(at, editorMenuCommands());

        if (layout.contextMenu.isOpen())
            host.setFocus(&layout.contextMenu);

        repaint();
    }

    void connectContextMenu()
    {
        // Through the dispatcher, exactly like the menu bar: the command may
        // belong to a focused text box rather than to the document.
        layout.contextMenu.onCommandChosen = [this](std::string_view id)
        { dispatchCommand(id); };

        layout.contextMenu.onClosed = [this]
        {
            host.setFocus(focusBeforeMenu != nullptr ? focusBeforeMenu
                                                     : &layout.activeEditor());
            repaint();
        };
    }

    // The overlay that owns the keyboard right now, or null.
    //
    // Two of these exist now — the palette and the context menu — and both want
    // every non-chord key rather than letting a binding without a modifier fire
    // underneath them. That is the job a VSCode `when` clause does, and two
    // cases is the point at which writing them by hand starts to look like the
    // thing contexts replace. Still not worth inventing them for two; worth
    // noting that a third would be.
    Widget* modalOverlay()
    {
        if (layout.contextMenu.isOpen())
            return &layout.contextMenu;

        if (layout.palette.isOpen())
            return &layout.palette;

        return nullptr;
    }

    void updateFindCount()
    {
        const auto& search = layout.activeEditor().search();

        layout.find.setMatchCount(search.currentNumber(), search.count());
    }

    void showFind(bool withReplace)
    {
        // Seeded from the selection, which is what every editor does: select a
        // word, press ⌘F, and it is already the query.
        auto seed = editor().selectedText();

        // Except a multi-line one. That means "search within this", which is a
        // different feature, and a newline in the query would match nothing
        // while looking like an ordinary search that had failed.
        if (seed.find('\n') != std::string::npos)
            seed.clear();

        // Where an as-you-type search starts from, so the first hit found is the
        // one nearest the work rather than the one nearest line 1.
        searchOrigin = editor().cursor().start();

        layout.find.show(seed, withReplace);

        host.setFocus(&layout.find.keyboardTarget());

        layout.layout();
        repaint();
    }

    // ⌘G with no bar open is still a search, so it opens one rather than doing
    // nothing — the query it would have used was cleared when the bar closed.
    void findNextOrOpen(bool backwards)
    {
        if (!layout.find.isOpen())
        {
            showFind(false);
            return;
        }

        if (backwards)
            layout.activeEditor().findPrevious();
        else
            layout.activeEditor().findNext();

        updateFindCount();
        repaint();
    }

    // Everything the editor can be asked to do, named once. The keymap points
    // at these ids and the palette lists them, so a command added here shows up
    // in both without either holding a list of its own — which is the whole
    // reason the registry exists rather than the if-chain this replaced.
    //
    // Registration order is the palette's order for an empty query, so it runs
    // most-reached-for first rather than alphabetically.
    void registerCommands()
    {
        commands.add({"workbench.showPalette",
                      "Show All Commands",
                      [this] { togglePalette(); }});

        commands.add({"file.new", "File: New File", [this] { newFile(); }});

        commands.add(
            {"file.open", "File: Open File…", [this] { chooseFileToOpen(); }});

        commands.add({"file.openFolder",
                      "File: Open Folder…",
                      [this] { chooseFolderToOpen(); }});

        commands.add({"file.save", "File: Save", [this] { saveFile(); }});

        commands.add({"file.saveAs", "File: Save As…", [this] { saveFileAs(); }});

        commands.add(
            {"file.close",
             "File: Close Editor",
             [this]
             { closeFile(groups.activeIndex(), groups.active().activeIndex()); }});

        commands.add({"file.revert",
                      "File: Revert File",
                      [this] { revertFile(); },
                      [this] { return activeFile().isDirty(); }});

        // Registered everywhere even though only Windows shows it in a menu:
        // the registry is what the editor can do, and the menu spec decides
        // where it appears. On macOS the palette is the one that offers it,
        // alongside the application menu's ⌘Q. See defaultMenus.
        commands.add({"file.exit", "File: Exit", [] { Apps::quit(); }});

        commands.add({"edit.undo",
                      "Edit: Undo",
                      [this] { editor().undo(); },
                      [this] { return editor().canUndo(); }});

        commands.add({"edit.redo",
                      "Edit: Redo",
                      [this] { editor().redo(); },
                      [this] { return editor().canRedo(); }});

        commands.add({"edit.cut",
                      "Edit: Cut",
                      [this] { cutOrCopy(true); },
                      [this] { return !editor().selectedText().empty(); }});

        commands.add({"edit.copy",
                      "Edit: Copy",
                      [this] { cutOrCopy(false); },
                      [this] { return !editor().selectedText().empty(); }});

        commands.add({"edit.paste",
                      "Edit: Paste",
                      [this] { paste(); },
                      [] { return Clipboard::hasText(); }});

        commands.add({"edit.selectAll",
                      "Edit: Select All",
                      [this] { editor().selectAll(); }});

        commands.add({"edit.addCursorAbove",
                      "Edit: Add Cursor Above",
                      [this] { editor().addCursorAbove(); }});

        commands.add({"edit.addCursorBelow",
                      "Edit: Add Cursor Below",
                      [this] { editor().addCursorBelow(); }});

        commands.add({"edit.addNextOccurrence",
                      "Edit: Add Selection To Next Find Match",
                      [this] { editor().selectNextOccurrence(); }});

        commands.add({"edit.selectAllOccurrences",
                      "Edit: Select All Occurrences",
                      [this] { editor().selectAllOccurrences(); }});

        // Listed but unavailable with one cursor, for the reason the tab
        // commands are: a command that only appears once the state exists is
        // harder to discover than one that is visibly not applicable yet. It
        // is also how anyone finds out that Escape is what does this.
        commands.add({"edit.collapseCursors",
                      "Edit: Collapse To One Cursor",
                      [this] { editor().collapseCursors(); },
                      [this] { return editor().cursors().hasMultiple(); }});

        commands.add({"find.show", "Find", [this] { showFind(false); }});

        commands.add({"find.showReplace", "Replace", [this] { showFind(true); }});

        commands.add(
            {"find.next", "Find: Find Next", [this] { findNextOrOpen(false); }});

        commands.add({"find.previous",
                      "Find: Find Previous",
                      [this] { findNextOrOpen(true); }});

        commands.add(
            {"find.replaceAll",
             "Find: Replace All",
             [this]
             {
                 layout.activeEditor().replaceAllMatches(layout.find.replacement());

                 updateFindCount();
                 updateChrome();
             },
             [this]
             {
                 // Listed but unavailable rather than hidden: a command
                 // that vanishes is harder to understand than one that
                 // is visibly not ready.
                 return layout.find.isOpen() && !layout.find.query().isEmpty();
             }});

        commands.add({"view.focusEditor",
                      "View: Focus Editor",
                      [this] { host.setFocus(&layout.activeEditor()); }});

        commands.add({"view.focusExplorer",
                      "View: Focus Explorer",
                      [this] { host.setFocus(&layout.files.keyboardTarget()); }});

        // Listed but unavailable with one file open, rather than hidden: a
        // command that appears once a second tab exists is harder to find than
        // one that is visibly not applicable yet.
        commands.add({"view.nextTab",
                      "View: Next Editor",
                      [this] { groups.active().activateNext(); },
                      [this] { return groups.active().count() > 1; }});

        commands.add({"view.previousTab",
                      "View: Previous Editor",
                      [this] { groups.active().activatePrevious(); },
                      [this] { return groups.active().count() > 1; }});

        // --- editor groups ------------------------------------------------

        commands.add({"view.splitEditor",
                      "View: Split Editor",
                      [this] { groups.split(); },

                      // Greyed rather than making a pane too narrow to read. The
                      // floor is the same one the sidebar is held to, and it is
                      // the only reason there is no hard cap on the number of
                      // groups: how many fit is a question about the window.
                      [this] { return layout.canSplit(); }});

        commands.add({"view.focusNextGroup",
                      "View: Focus Next Editor Group",
                      [this] { groups.activateNext(); },
                      [this] { return groups.count() > 1; }});

        commands.add({"view.focusPreviousGroup",
                      "View: Focus Previous Editor Group",
                      [this] { groups.activatePrevious(); },
                      [this] { return groups.count() > 1; }});

        // What fills a split. A file lives in exactly one group — see
        // EditorGroups — so "show this one over there" is the move that is
        // actually available, and it is the reason a split opens empty.
        commands.add({"view.moveEditorToNextGroup",
                      "View: Move Editor Into Next Group",
                      [this] { groups.moveActiveFile(1); },
                      [this] { return canMoveEditor(1); }});

        commands.add({"view.moveEditorToPreviousGroup",
                      "View: Move Editor Into Previous Group",
                      [this] { groups.moveActiveFile(-1); },
                      [this] { return canMoveEditor(-1); }});

        commands.add({"view.refreshExplorer",
                      "View: Refresh Explorer",
                      [this] { layout.files.refresh(); }});

        commands.add({"view.toggleWordWrap",
                      "View: Toggle Word Wrap",
                      [this]
                      {
                          layout.activeEditor().setWordWrap(
                              !layout.activeEditor().isWordWrapped());
                          updateChrome();
                      },
                      [] { return true; },
                      [this] { return layout.activeEditor().isWordWrapped(); }});
    }

    // Whether there is anywhere for the active file to go, which is either a
    // group already that way or room to make one. The model refuses the move
    // that would change nothing; this refuses the one there is no room for.
    bool canMoveEditor(int direction) const
    {
        if (const auto target = groups.activeIndex() + direction;
            target >= 0 && target < groups.count())
            return true;

        return groups.active().count() > 1 && layout.canSplit();
    }

    // The default keymap. A table rather than a chain of ifs, and the shape a
    // config file will be read into — which is why bindings name commands by id
    // instead of holding the callable.
    void bindKeys()
    {
        keymap.bind("cmd+shift+p", "workbench.showPalette");
        keymap.bind("cmd+n", "file.new");
        keymap.bind("cmd+o", "file.open");
        keymap.bind("cmd+shift+o", "file.openFolder");
        keymap.bind("cmd+s", "file.save");
        keymap.bind("cmd+shift+s", "file.saveAs");
        keymap.bind("cmd+w", "file.close");
        keymap.bind("cmd+z", "edit.undo");
        keymap.bind("cmd+shift+z", "edit.redo");
        keymap.bind("cmd+x", "edit.cut");
        keymap.bind("cmd+c", "edit.copy");
        keymap.bind("cmd+v", "edit.paste");
        keymap.bind("cmd+a", "edit.selectAll");
        keymap.bind("cmd+d", "edit.addNextOccurrence");
        keymap.bind("cmd+shift+l", "edit.selectAllOccurrences");

        // VSCode's chords, and like ⌃Tab they cannot become menu key
        // equivalents: toKeyEquivalent only converts single characters, so an
        // arrow stays with the keymap. That is the right side of the trade
        // here — a key equivalent is matched by macOS before the window sees
        // the key, and ⌥⌘↑ has to reach the editor.
        keymap.bind("cmd+alt+up", "edit.addCursorAbove");
        keymap.bind("cmd+alt+down", "edit.addCursorBelow");
        keymap.bind("cmd+f", "find.show");
        keymap.bind("cmd+alt+f", "find.showReplace");
        keymap.bind("cmd+g", "find.next");
        keymap.bind("cmd+shift+g", "find.previous");
        keymap.bind("cmd+1", "view.focusEditor");
        keymap.bind("cmd+shift+e", "view.focusExplorer");

        // VSCode's chords, and deliberately not expressible as menu key
        // equivalents: toKeyEquivalent only converts single characters, so
        // "tab" stays with the keymap and the menu item prints no shortcut
        // rather than claiming one macOS would match before the window.
        keymap.bind("ctrl+tab", "view.nextTab");
        keymap.bind("ctrl+shift+tab", "view.previousTab");

        // VSCode's chord for splitting, and unlike the arrows it *is* a single
        // character, so the menu takes it as a native key equivalent and macOS
        // matches it before the window. That is the right side of the trade for
        // a command that has no business reaching the document.
        keymap.bind("cmd+\\", "view.splitEditor");

        // VSCode spells these ⌘K ⌘→, which is a chord *sequence* and there is
        // no such thing here. ⌥⌘← / → are the nearest free pair, and the
        // shifted ones move the file rather than the focus, which is the same
        // shift-means-take-it-with-you the arrow keys already mean in the
        // document.
        keymap.bind("cmd+alt+right", "view.focusNextGroup");
        keymap.bind("cmd+alt+left", "view.focusPreviousGroup");
        keymap.bind("cmd+alt+shift+right", "view.moveEditorToNextGroup");
        keymap.bind("cmd+alt+shift+left", "view.moveEditorToPreviousGroup");

        // VSCode's chord, and the one place a binding without Command matters:
        // handleShortcut runs before the editor sees the key, so ⌥Z toggles
        // wrapping rather than typing the Ω that macOS resolves it to.
        keymap.bind("alt+z", "view.toggleWordWrap");
    }

    void togglePalette()
    {
        if (layout.palette.isOpen())
        {
            layout.palette.hide();
            return;
        }

        focusBeforePalette = host.focused();

        layout.palette.show();
        host.setFocus(&layout.palette.keyboardTarget());

        repaint();
    }

    void cutOrCopy(bool removeSelection)
    {
        const auto selected = editor().selectedText();

        if (selected.empty())
            return;

        Clipboard::copyText(selected);

        if (removeSelection)
            editor().backspace();
    }

    void paste()
    {
        if (!Clipboard::hasText())
            return;

        // A paste is one undo step whatever it contains, so it never merges
        // with typing either side of it.
        editor().breakUndoStep();
        editor().insert(Clipboard::getText());
        editor().breakUndoStep();
    }

    void revertFile()
    {
        activeFile().reload();

        updateChrome();
        repaint();
    }

    // Both pickers are OS modals that block until answered, so a cancel is the
    // common case rather than an error and simply leaves everything as it was.
    void chooseFileToOpen()
    {
        const auto chosen = Apps::chooseFile();

        if (!chosen)
            return;

        openFile(FilePath {*chosen});

        // Focus follows the open, as it does from the tree: the point of
        // opening a file is to type in it.
        host.setFocus(&layout.activeEditor());
        repaint();
    }

    // Opening a *folder* is what sets the sidebar's root; opening a file does
    // not move it. That is what VSCode does, and the alternative — retargeting
    // the tree at whatever directory the last file happened to live in — throws
    // away the project someone is working in as a side effect of opening a file.
    void chooseFolderToOpen()
    {
        const auto chosen = Apps::chooseDirectory();

        if (!chosen)
            return;

        openFolder(FilePath {*chosen});
    }

    void openFolder(const FilePath& path)
    {
        layout.files.setRoot(path);

        // The tree is where the work is now, so the keyboard goes with it —
        // there is nothing to type into until a file is picked from it.
        host.setFocus(&layout.files.keyboardTarget());
        repaint();
    }

    // Opens the file into a new tab, or brings its tab forward if it is already
    // open — in whichever group has it, since a path lives in exactly one.
    // EditorGroups::onChanged does the rest.
    void openFile(const FilePath& path) { groups.open(path); }

    // A new empty buffer with nowhere to save to yet, which is what Save As is
    // for. Also what closing the last tab leaves behind, so the two arrived
    // together.
    void newFile()
    {
        groups.active().addUntitled();
        host.setFocus(&layout.activeEditor());
    }

    // Whether a "close without saving?" put to the person is still the question
    // it was.
    //
    // The tab *and* the text, because the question is about a particular
    // version of a particular file: type one character and it is a different
    // question. Compared rather than cleared by an event, because the edit can
    // arrive from a keystroke, a menu paste or an undo, and only the first of
    // those runs the editor's own key handling — a callback hung off one route
    // would leave the other two asking about text that is gone.
    // The pane as well as the tab, now that a tab number means nothing on its
    // own: two panes both have a tab 0, and a ⌘W in one of them must not answer
    // a question that was asked in the other.
    bool closeIsPending(int group, int index) const
    {
        return group >= 0 && index >= 0 && pendingCloseGroup == group
               && pendingCloseTab == index
               && pendingCloseState
                      == groups.at(group).at(index).file.editor().stateId();
    }

    // Refuses a file with unsaved edits and says so in the title, which is the
    // same shape ⌘S over a conflict already takes: there is no dialog to ask
    // in, so the title carries the question and a second ⌘W answers it.
    void closeFile(int group, int index)
    {
        if (closeIsPending(group, index))
        {
            pendingCloseGroup = -1;
            groups.closeFileDiscarding(group, index);

            return;
        }

        if (groups.closeFile(group, index) == CloseResult::hasUnsavedChanges)
        {
            // Only ever one tab is armed: arming every refused close would mean
            // a ⌘W aimed at one file discarding another that had been refused
            // ten minutes earlier.
            pendingCloseGroup = group;
            pendingCloseTab = index;
            pendingCloseState = groups.at(group).at(index).file.editor().stateId();

            updateChrome();
            repaint();
        }
    }

    void saveFileAs()
    {
        auto options = Apps::FileSaveOptions {};
        options.suggestedName = activeFile().name();

        const auto chosen = Apps::chooseSaveFile(options);

        if (!chosen)
            return;

        activeFile().saveAs(FilePath {*chosen});

        // The tab now has a name, and the title with it.
        updateChrome();
        repaint();
    }

    // What the window's title bar should read. A pure function of the active
    // file's state, so App can ask for it once at startup and then let
    // onTitleChanged push every later change.
    std::string windowTitle() const
    {
        const auto& file = groups.active().active().file;

        auto name = displayName(file);

        if (file.isDirty())
            name = "• " + name;

        // There is no dialog to ask in yet, so the title carries both questions
        // and a second press of the same chord answers each. See saveFile and
        // closeFile.
        if (file.isConflicted())
            name += "  —  changed on disk. ⌘S again to overwrite";

        if (closeIsPending(groups.activeIndex(), groups.active().activeIndex()))
            name += "  —  unsaved. ⌘W again to close anyway";

        return name;
    }

    // Pushes the groups' state into the chrome that displays it. Cheap enough
    // to call on every keystroke: every tab strip and the status bar all
    // compare before they store, so an unchanged state asks for no frame.
    //
    // Every pane, not only the active one — a dirty dot in the pane nobody is
    // typing in still has to appear, and a file changed on disk under an
    // inactive pane still has to say so.
    void updateChrome()
    {
        layout.refreshGroups();

        auto& editor = layout.activeEditor();

        auto position = "Ln " + std::to_string(editor.caretLine()) + ", Col "
                        + std::to_string(editor.caretColumn());

        // The only place that says the mode is on when the other carets have
        // been scrolled off the screen. Without it, a keystroke lands in places
        // nobody can see with nothing on screen to explain it.
        if (const auto count = editor.editor().cursors().count(); count > 1)
            position += "  (" + std::to_string(count) + " cursors)";

        // A file that lost its colours on the way past the size limit has to say
        // so. Drawn plain, it is indistinguishable from a language with no
        // grammar — and the person watching just watched it happen, so silence
        // reads as the highlighter having broken rather than as a decision.
        const auto* const highlighter = editor.highlighter();
        const auto tooLarge =
            highlighter != nullptr && highlighter->isTooLargeToHighlight();

        layout.status.setText(
            position, tooLarge ? "UTF-8    Plain (file too large)" : "UTF-8    C++");

        updateTitle();
    }

    void updateTitle()
    {
        auto text = windowTitle();

        // Cached because this runs on every keystroke and setTitle crosses into
        // AppKit; the title only actually changes when dirtiness does.
        if (text == shownTitle)
            return;

        shownTitle = std::move(text);
        onTitleChanged(shownTitle);
    }

    // The atlas rasterizes at the display's real scale, so it cannot be built
    // until the view is on a display — and must be rebuilt if it moves to one
    // with a different scale.
    //
    // One atlas for the window and one TextRenderer per pane. The atlas is
    // shared because a glyph is a glyph wherever it is drawn and the whole point
    // of it is that it is uploaded once; the renderers are not, because each
    // owns a RowCache of the rows *its own* pane is showing. See
    // EditorGroupView.
    void ensureRenderer()
    {
        const auto scale = backingScale();

        if (atlas && builtAtScale == scale)
            return;

        auto request = Text::FontRequest {};
        request.family = "Menlo";
        request.pointSize = 13.f;
        request.scale = scale;

        auto rasterizer = makeOwned<Text::GlyphRasterizer>(request);

        if (!rasterizer->isValid())
            return;

        atlas = makeOwned<Text::GlyphAtlas>(
            OwningPointer<Text::GlyphSource> {std::move(rasterizer)}, 512, 4096);

        glyphs.emplace();
        builtAtScale = scale;

        layout.setAtlas(atlas.get(), textTheme, scale);
    }

    void resized() override
    {
        GPUView::resized();

        const auto bounds = getLocalBounds();

        if (bounds.w > 0 && bounds.h > 0)
        {
            // SpriteRenderer bakes its logical size at construction, so a
            // resize means a new one rather than a setter.
            sprites.emplace(Graphics::Point {bounds.w, bounds.h}, sampleCount());

            if (glyphs)
                glyphs->setViewportSize({bounds.w, bounds.h});
        }

        host.setBounds(bounds);
        repaint();
    }

    void backingScaleChanged() override
    {
        GPUView::backingScaleChanged();

        // Glyphs cached for the old display are the wrong size now.
        ensureRenderer();
        repaint();
    }

    void saveFile()
    {
        auto& file = activeFile();

        // An untitled buffer has nowhere to go, so ⌘S on one asks where —
        // which is what every editor does and the only thing that makes the
        // buffer closing the last tab leaves behind worth typing in.
        if (file.path().empty())
        {
            saveFileAs();
            return;
        }

        // The second press takes the conflict. Refusing forever would strand
        // the text in the buffer, and there is no dialog to ask in until there
        // is a widget for one — so the title asks, and Cmd+S answers.
        if (file.isConflicted())
            file.saveOverwriting();
        else
            file.save();

        updateChrome();
        repaint();
    }

    // Standing in for file watching, which eacp does not have: one stat a
    // second per open file, which is nothing next to a frame.
    //
    // Every open file in every group, not only the visible ones: a tab switched
    // to an hour after a git checkout should show what is on disk now, and
    // finding out at the moment it is switched to would mean discovering it too
    // late to warn about a conflict.
    void checkDisk()
    {
        auto changed = false;

        for (auto group = 0; group < groups.count(); ++group)
            for (auto index = 0; index < groups.at(group).count(); ++index)
                changed |= groups.at(group).at(index).file.pollDisk();

        if (!changed)
            return;

        updateChrome();
        repaint();
    }

    // Where every command arrives, from the keymap and from the menu bar alike.
    //
    // The two routes have to converge here rather than at the registry, because
    // a menu item's key equivalent is matched by macOS against the menu bar
    // *before* the window is sent a key down — so the moment Paste is in the
    // Edit menu, a ⌘V that used to reach a focused find field as a keystroke
    // arrives as a command instead. Running it straight off the registry would
    // paste into the document while the caret is visibly in the find box.
    void dispatchCommand(std::string_view id)
    {
        if (!host.runCommandOnFocus(id))
            commands.run(id);

        // Several commands change the document or the caret without ever
        // reaching the editor's key handling — paste, undo, redo, cut, select
        // all — and that is what normally pushes a change into the chrome. Left
        // out, the dirty dot and the line/column readout lag a whole ⌘V behind,
        // catching up only at the next keystroke. Found by running the app:
        // pasting into a fresh tab through the Edit menu left the tab looking
        // clean over text that was not.
        layout.activeEditor().wake();

        repaint();
    }

    // Built here and installed by App, which is the half that owns a window.
    //
    // The split is not bookkeeping: a menu bar belongs to a window on Windows
    // and to the application on macOS, so eacp takes a window either way — and
    // this view has the registry and the keymap the menus are made of but no
    // window at all. It says what the menus are; App says where they go.
    Graphics::MenuBar menuBar()
    {
        const auto menus = defaultMenus();

        // A menu naming a command that was never registered is left out, and
        // silently: the item simply is not there. Worth saying out loud, since
        // it means a rename landed on one side only.
        for (const auto& missing: unknownCommandIds(menus, commands))
            LOG("menu names an unregistered command: " + missing);

        auto bar = Graphics::MenuBar {};

        // Gives the app its About, Hide and Quit — including ⌘Q, which without
        // a menu bar macOS does not provide at all.
        bar.add(Graphics::standardApplicationMenu("ECode"));

        for (auto& menu:
             buildMenuBar(menus,
                          commands,
                          keymap,
                          [this](std::string_view id) { dispatchCommand(id); })
                 .menus)
            bar.add(std::move(menu));

        return bar;
    }

    // The chords that belong to the window rather than to whatever has focus.
    bool handleShortcut(const Graphics::KeyEvent& event)
    {
        if (const auto id = keymap.commandFor(event); !id.empty())
        {
            // Consumed whether or not the command could run: a disabled undo
            // must not fall through and arrive in the document as a "z".
            dispatchCommand(id);
            return true;
        }

        // Any other Cmd chord is swallowed rather than typed as text.
        return event.modifiers.command;
    }

    void keyDown(const Graphics::KeyEvent& event) override
    {
        // An open overlay is modal, so everything except a command chord reaches
        // it before the keymap does — otherwise a binding without a modifier
        // would fire instead of being typed into the palette's query or moving
        // the context menu's highlight. See modalOverlay.
        if (modalOverlay() != nullptr && !event.modifiers.command)
        {
            host.keyDown(event);
            return;
        }

        // A focused text box takes its own editing chords before the keymap sees
        // them: ⌘A, ⌘C and ⌘V in a find field mean the field, and letting them
        // through to the document would select the whole file or paste the
        // search term into it. The field consumes only those four and passes
        // every other chord on. See Widget::isTextInput.
        if (const auto* focused = host.focused();
            focused != nullptr && focused->isTextInput())
            if (host.keyDown(event))
                return;

        // dispatchCommand wakes the editor, so a chord that ran a command has
        // already refreshed the chrome by the time this returns.
        if (handleShortcut(event))
            return;

        // Tab traversal is the host's, but only once there is more than one
        // focusable widget to move between — until then Tab is indentation.
        if (host.keyDown(event))
            return;
    }

    void mouseDown(const Graphics::MouseEvent& event) override
    {
        // Which pane the press landed in decides which group is being worked
        // in, and it is decided *before* the press is routed. Focus alone
        // cannot answer it: a click on a tab strip focuses nothing, so a tab
        // selected in one pane while another was active would switch the wrong
        // file. An open overlay covers every pane, so a click on the palette
        // must not count as a click on whatever is behind it.
        if (modalOverlay() == nullptr)
            if (const auto group = layout.groupViewAt(event.pos); group >= 0)
                groups.activate(group);

        host.mouseDown(event);
    }

    void mouseDragged(const Graphics::MouseEvent& event) override
    {
        host.mouseDragged(event);
    }

    void mouseUp(const Graphics::MouseEvent& event) override
    {
        host.mouseUp(event);

        // Releasing a splitter drag away from its band has to put the arrow
        // back. Nothing else would until the pointer moved again, and macOS
        // sends no move for a button release.
        updateCursor(event.pos);
    }

    // Forwarded only now that something tracks the pointer: the context menu
    // highlights the row under it, which is the first hover state in the app.
    void mouseMoved(const Graphics::MouseEvent& event) override
    {
        host.mouseMoved(event);
        updateCursor(event.pos);
    }

    // The pointer left the window. Nothing else will say so — the last move
    // inside the view is the last move there is — so a tab lit under the
    // pointer would stay lit with the pointer in another application.
    void mouseExited(const Graphics::MouseEvent&) override { host.mouseExited(); }

    // The window is one Graphics::View, so there is exactly one cursor for the
    // whole thing and the application is what applies it — a widget can only say
    // what it wants. Setting the same shape twice is free, so this runs on every
    // move without asking whether anything changed.
    void updateCursor(const Graphics::Point& position)
    {
        setMouseCursor(host.cursorAt(position));
    }

    void mouseWheel(const Graphics::MouseEvent& event) override
    {
        host.mouseWheel(event);
    }

    void render(GPU::Frame& frame) override
    {
        ensureRenderer();

        auto pass = frame.beginPass({textTheme.background});

        if (!sprites || !atlas || !glyphs)
            return;

        glyphs->setViewportSize({getLocalBounds().w, getLocalBounds().h});

        // Every glyph the frame needs is rasterized before the first draw, then
        // uploaded once. Uploading mid-pass would mutate a texture the earlier
        // draws have already bound.
        host.prepare(*atlas);
        atlas->commit();

        auto context = PaintContext {
            pass, *sprites, *glyphs, *atlas, getLocalBounds(), builtAtScale};

        host.paint(context);
    }

    TextTheme textTheme;

    // Ahead of the layout, which builds one pane per group.
    //
    // One highlighter per open file rather than one for the workspace: a shared
    // one would have to reparse from scratch on every switch, which is the cold
    // open PLAN.md §7 measures, paid on a ⌃Tab.
    //
    // Budgeted, because this is the caller that owns a frame. An 8,000-line file
    // is 10 ms of tree-sitter next to 0.06 ms for the frame around it, so a
    // highlighter allowed to finish decides when the window first appears; given
    // 2 ms it hands the frame back and the text is on screen and scrollable
    // immediately, with the colours arriving over the next few frames.
    EditorGroups groups {[]
                         {
                             auto syntax = makeOwned<SyntaxHighlighter>(
                                 SyntaxHighlighter::frameParseBudget);

                             // A grammar that failed to load leaves this null,
                             // and everything draws as plain text.
                             if (!syntax->isValid())
                                 return OwningPointer<Highlighter> {};

                             return OwningPointer<Highlighter> {std::move(syntax)};
                         }};

    // Ahead of the layout too, which holds the palette that reads both of them.
    CommandRegistry commands;
    Keymap keymap;

    WindowLayout layout {groups, commands, keymap};
    WidgetHost host;

    // Where focus was when the palette opened, so closing it puts the keyboard
    // back rather than always in the editor.
    Widget* focusBeforePalette = nullptr;

    // The same, for the context menu. Kept separate because a menu can be
    // opened while the palette is up, and one field would then lose the
    // palette's answer.
    Widget* focusBeforeMenu = nullptr;

    // Where the caret was when the find bar opened. An as-you-type search runs
    // from here rather than from the caret as it stands, which would otherwise
    // walk down the file one hit per keystroke.
    std::size_t searchOrigin = 0;

    std::function<void(const std::string&)> onTitleChanged =
        [](const std::string&) {};

    // The atlas and the glyph batch are the window's; the renderers belong to
    // the panes, since each keeps a cache of the rows it is showing.
    std::optional<Sprites::SpriteRenderer> sprites;
    OwningPointer<Text::GlyphAtlas> atlas;
    std::optional<Text::GlyphRenderer> glyphs;

    float builtAtScale = 1.f;

    std::string shownTitle;

    // The one tab whose close was refused for having unsaved edits, which pane
    // it is in, and the text it was refused over. See closeIsPending.
    int pendingCloseGroup = -1;
    int pendingCloseTab = -1;
    std::uint64_t pendingCloseState = 0;

    Threads::Timer blink {[this] { layout.activeEditor().tickCaretBlink(); }, 2};
    Threads::Timer diskWatch {[this] { checkDisk(); }, 1};
};

Graphics::WindowOptions windowOptions()
{
    auto options = Graphics::WindowOptions {};

    options.width = 1200;
    options.height = 800;
    options.minWidth = 480;
    options.minHeight = 320;
    options.title = "ECode";
    options.backgroundColor = TextTheme {}.background;

    return options;
}

struct App
{
    App()
    {
        window.setContentView(view);

        // After the window exists, because that is what the menu bar attaches
        // to on the platforms that own menus per window.
        Graphics::setApplicationMenuBar(view.menuBar(), window);

        view.onTitleChanged = [this](const std::string& text)
        { window.setTitle(text); };

        // The view opened its file before this callback existed, so the first
        // title is pushed by hand.
        window.setTitle(view.windowTitle());
    }

    EditorView view;
    Graphics::Window window {windowOptions()};
};
} // namespace ecode

int main(int argc, char* argv[])
{
    return eacp::Apps::run<ecode::App>(argc, argv);
}
