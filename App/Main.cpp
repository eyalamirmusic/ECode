#include <ECodeUI/Chrome.h>
#include <ECodeUI/CommandPalette.h>
#include <ECodeUI/ContextMenu.h>
#include <ECodeUI/EditorWidget.h>
#include <ECodeUI/FileTreeView.h>
#include <ECodeUI/FindBar.h>
#include <ECodeUI/Keymap.h>
#include <ECodeUI/MenuBuilder.h>
#include <ECodeUI/Splitter.h>
#include <ECodeUI/Theme.h>
#include <ECodeUI/WidgetHost.h>
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

// The window's whole layout. Activity bar and sidebar off the left, tab strip
// off the top, status bar off the bottom, editor taking what is left — which is
// exactly what Rect's splitters express, and only reads correctly now that they
// are y-down.
struct WindowLayout final : Widget
{
    WindowLayout(OpenFile& file,
                 const CommandRegistry& commands,
                 const Keymap& keymap)
        : editor(file)
        , palette(theme, commands, keymap)
        , contextMenu(theme, commands, keymap)
    {
        addChild(activityBar);
        addChild(sidebar);
        addChild(files);
        addChild(tabs);
        addChild(status);
        addChild(editor);

        // After both panes, so its grab band is found before the sidebar and
        // the editor it sits between — widgetAt takes the last child that
        // contains the point.
        addChild(sidebarSplitter);

        sidebarSplitter.setPosition(activityBarWidth + sidebarWidth);

        sidebarSplitter.onMoved = [this](float)
        {
            layout();
            repaint();
        };

        // After the editor, so it draws over the text it is searching.
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

        // Tabs belong to the editor group, so they start where the sidebar
        // ends rather than spanning the window.
        tabs.setBounds(area.removeFromTop(tabBarHeight));
        editor.setBounds(area);

        // Over the editor's top-right corner rather than given a slice of it.
        // The bar covers a few lines instead of pushing the file down, which is
        // what stops the line being read from moving the moment ⌘F is pressed.
        //
        // Its bounds are the box itself and not the editor's width, or it would
        // swallow every click meant for the text beneath it — widgetAt only asks
        // whether a point is inside the bounds.
        const auto barWidth = std::min(find.barWidth(), area.w);

        find.setBounds({std::max(area.x, area.right() - findMargin - barWidth),
                        area.y,
                        barWidth,
                        find.barHeight()});
    }

    static constexpr auto activityBarWidth = 48.f;

    // Where the divider starts. Only the initial value now — after that the
    // splitter owns it.
    static constexpr auto sidebarWidth = 240.f;

    // Narrow enough to be worth collapsing to and wide enough to still show a
    // filename. The editor's floor matters more: a sidebar dragged over the
    // whole window would leave nothing to type in and no obvious way back.
    static constexpr auto minSidebarWidth = 120.f;
    static constexpr auto minEditorWidth = 240.f;
    static constexpr auto tabBarHeight = 35.f;
    static constexpr auto statusBarHeight = 22.f;

    // Clear of the right edge, where a vertical scrollbar will go.
    static constexpr auto findMargin = 14.f;

    ChromeTheme theme;

    Panel activityBar {theme.activityBar};

    // The sidebar's background is a panel behind the tree rather than the
    // tree's own fill, so the empty space below the last row is still sidebar.
    Panel sidebar {theme.sidebar};
    FileTreeView files {theme};
    TabBar tabs {theme};
    StatusBar status {theme};
    EditorWidget editor;

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

        workspace.onChanged = [this] { showActiveFile(); };

        registerCommands();
        bindKeys();
        connectFindBar();
        connectTabs();

        layout.palette.onClosed = [this]
        {
            // Back to whatever was being worked in. Falling back to the editor
            // rather than to nothing: a window with focus nowhere swallows the
            // next keystroke silently.
            host.setFocus(focusBeforePalette != nullptr ? focusBeforePalette
                                                        : &layout.editor);
            repaint();
        };

        for (const auto& path: filesToOpen())
            openFile(path);

        // Left on the first, not the last: the first name on the command line
        // is the one that was meant, and the rest are context.
        workspace.activate(0);

        layout.editor.onStateChanged = [this] { updateChrome(); };

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
            host.setFocus(&layout.editor);
            repaint();
        };

        connectContextMenu();

        // The editor starts focused; a window that opens with no caret reads
        // as broken.
        host.setFocus(&layout.editor);
    }

    Editor& editor() { return workspace.editor(); }
    TextFile& activeFile() { return workspace.active().file; }

    // Points the view at whatever the workspace made active, and pushes the
    // change into the chrome around it.
    //
    // Every route that opens, closes or switches a file arrives here through
    // Workspace::onChanged rather than by remembering to call it — which is the
    // difference between a tab switch that always redraws the strip and one
    // that redraws it wherever somebody thought to.
    void showActiveFile()
    {
        layout.editor.setFile(workspace.active());

        pendingClose = -1;

        // Which also puts the active tab in the strip; see updateChrome.
        updateChrome();
        repaint();
    }

    void connectTabs()
    {
        layout.tabs.onTabSelected = [this](int index)
        {
            workspace.activate(index);

            // Focus follows the click, for the same reason it follows one in
            // the tree: the point of switching to a file is to type in it.
            host.setFocus(&layout.editor);
        };

        layout.tabs.onTabClosed = [this](int index) { closeFile(index); };
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
            layout.editor.setSearchQuery(layout.find.query(), searchOrigin);
            updateFindCount();
            repaint();
        };

        layout.find.onFindNext = [this]
        {
            layout.editor.findNext();
            updateFindCount();
            repaint();
        };

        layout.find.onFindPrevious = [this]
        {
            layout.editor.findPrevious();
            updateFindCount();
            repaint();
        };

        layout.find.onReplace = [this]
        {
            layout.editor.replaceCurrent(layout.find.replacement());

            updateFindCount();
            updateChrome();
            repaint();
        };

        layout.find.onReplaceAll = [this]
        {
            layout.editor.replaceAllMatches(layout.find.replacement());

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
            layout.editor.clearSearch();

            host.setFocus(&layout.editor);

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

    void connectContextMenu()
    {
        layout.editor.onContextMenuRequested = [this](const Graphics::Point& at)
        {
            // Focus moves to the menu, so the caret stops blinking under it and
            // the arrow keys drive the menu rather than the document.
            focusBeforeMenu = host.focused();

            layout.contextMenu.show(at, editorMenuCommands());

            if (layout.contextMenu.isOpen())
                host.setFocus(&layout.contextMenu);

            repaint();
        };

        // Through the dispatcher, exactly like the menu bar: the command may
        // belong to a focused text box rather than to the document.
        layout.contextMenu.onCommandChosen = [this](std::string_view id)
        { dispatchCommand(id); };

        layout.contextMenu.onClosed = [this]
        {
            host.setFocus(focusBeforeMenu != nullptr ? focusBeforeMenu
                                                     : &layout.editor);
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
        const auto& search = layout.editor.search();

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
            layout.editor.findPrevious();
        else
            layout.editor.findNext();

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

        commands.add({"file.close",
                      "File: Close Editor",
                      [this] { closeFile(workspace.activeIndex()); }});

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

        commands.add({"find.replaceAll",
                      "Find: Replace All",
                      [this]
                      {
                          layout.editor.replaceAllMatches(layout.find.replacement());

                          updateFindCount();
                          updateChrome();
                      },
                      [this]
                      {
                          // Listed but unavailable rather than hidden: a command
                          // that vanishes is harder to understand than one that
                          // is visibly not ready.
                          return layout.find.isOpen()
                                 && !layout.find.query().isEmpty();
                      }});

        commands.add({"view.focusEditor",
                      "View: Focus Editor",
                      [this] { host.setFocus(&layout.editor); }});

        commands.add({"view.focusExplorer",
                      "View: Focus Explorer",
                      [this] { host.setFocus(&layout.files.keyboardTarget()); }});

        // Listed but unavailable with one file open, rather than hidden: a
        // command that appears once a second tab exists is harder to find than
        // one that is visibly not applicable yet.
        commands.add({"view.nextTab",
                      "View: Next Editor",
                      [this] { workspace.activateNext(); },
                      [this] { return workspace.count() > 1; }});

        commands.add({"view.previousTab",
                      "View: Previous Editor",
                      [this] { workspace.activatePrevious(); },
                      [this] { return workspace.count() > 1; }});

        commands.add({"view.refreshExplorer",
                      "View: Refresh Explorer",
                      [this] { layout.files.refresh(); }});

        commands.add({"view.toggleWordWrap",
                      "View: Toggle Word Wrap",
                      [this]
                      {
                          layout.editor.setWordWrap(!layout.editor.isWordWrapped());
                          updateChrome();
                      },
                      [] { return true; },
                      [this] { return layout.editor.isWordWrapped(); }});
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
        host.setFocus(&layout.editor);
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
    // open. Workspace::onChanged does the rest.
    void openFile(const FilePath& path) { workspace.open(path); }

    // A new empty buffer with nowhere to save to yet, which is what Save As is
    // for. Also what closing the last tab leaves behind, so the two arrived
    // together.
    void newFile()
    {
        workspace.addUntitled();
        host.setFocus(&layout.editor);
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
    bool closeIsPending(int index) const
    {
        return index >= 0 && pendingClose == index
               && pendingCloseState == workspace.at(index).file.editor().stateId();
    }

    // Refuses a file with unsaved edits and says so in the title, which is the
    // same shape ⌘S over a conflict already takes: there is no dialog to ask
    // in, so the title carries the question and a second ⌘W answers it.
    void closeFile(int index)
    {
        if (closeIsPending(index))
        {
            pendingClose = -1;
            workspace.closeDiscarding(index);

            return;
        }

        if (workspace.close(index) == CloseResult::hasUnsavedChanges)
        {
            // Only ever one tab is armed: arming every refused close would mean
            // a ⌘W aimed at one file discarding another that had been refused
            // ten minutes earlier.
            pendingClose = index;
            pendingCloseState = workspace.at(index).file.editor().stateId();

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

    // The name a file goes by in a tab or a title. Untitled is what a buffer
    // with no path is called everywhere, and it has to be called something.
    static std::string displayName(const TextFile& file)
    {
        auto name = file.name();

        return name.empty() ? "Untitled" : name;
    }

    // What the window's title bar should read. A pure function of the active
    // file's state, so App can ask for it once at startup and then let
    // onTitleChanged push every later change.
    std::string windowTitle() const
    {
        const auto& file = workspace.active().file;

        auto name = displayName(file);

        if (file.isDirty())
            name = "• " + name;

        // There is no dialog to ask in yet, so the title carries both questions
        // and a second press of the same chord answers each. See saveFile and
        // closeFile.
        if (file.isConflicted())
            name += "  —  changed on disk. ⌘S again to overwrite";

        if (closeIsPending(workspace.activeIndex()))
            name += "  —  unsaved. ⌘W again to close anyway";

        return name;
    }

    // Pushes the workspace's state into the chrome that displays it. Cheap
    // enough to call on every keystroke: the tab strip and the status bar both
    // compare before they store, so an unchanged state asks for no frame.
    void updateChrome()
    {
        auto tabs = eacp::Vector<TabItem> {};

        for (auto index = 0; index < workspace.count(); ++index)
        {
            const auto& file = workspace.at(index).file;

            auto tab = TabItem {};
            tab.title = displayName(file);
            tab.modified = file.isDirty();
            tab.conflicted = file.isConflicted();

            tabs.add(std::move(tab));
        }

        layout.tabs.setTabs(std::move(tabs));
        layout.tabs.setActiveTab(workspace.activeIndex());

        auto position = "Ln " + std::to_string(layout.editor.caretLine()) + ", Col "
                        + std::to_string(layout.editor.caretColumn());

        // The only place that says the mode is on when the other carets have
        // been scrolled off the screen. Without it, a keystroke lands in places
        // nobody can see with nothing on screen to explain it.
        if (const auto count = layout.editor.editor().cursors().count(); count > 1)
            position += "  (" + std::to_string(count) + " cursors)";

        layout.status.setText(position, "UTF-8    C++");

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
    void ensureRenderer()
    {
        const auto scale = backingScale();

        if (renderer && builtAtScale == scale)
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

        renderer.emplace(*atlas, textTheme, scale);
        glyphs.emplace();
        builtAtScale = scale;

        layout.editor.setRenderer(&renderer.value());
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
    // Every open file, not only the visible one: a tab switched to an hour
    // after a git checkout should show what is on disk now, and finding out at
    // the moment it is switched to would mean discovering it too late to warn
    // about a conflict.
    void checkDisk()
    {
        auto changed = false;

        for (auto index = 0; index < workspace.count(); ++index)
            changed |= workspace.at(index).file.pollDisk();

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
        layout.editor.wake();

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

        if (!sprites || !renderer || !atlas || !glyphs)
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

    // Ahead of the layout, which is constructed against the active file.
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
    Workspace workspace {[]
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

    WindowLayout layout {workspace.active(), commands, keymap};
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

    std::optional<Sprites::SpriteRenderer> sprites;
    OwningPointer<Text::GlyphAtlas> atlas;
    std::optional<TextRenderer> renderer;
    std::optional<Text::GlyphRenderer> glyphs;

    float builtAtScale = 1.f;

    std::string shownTitle;

    // The one tab whose close was refused for having unsaved edits, or -1, and
    // the text it was refused over. See closeIsPending.
    int pendingClose = -1;
    std::uint64_t pendingCloseState = 0;

    Threads::Timer blink {[this] { layout.editor.tickCaretBlink(); }, 2};
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
