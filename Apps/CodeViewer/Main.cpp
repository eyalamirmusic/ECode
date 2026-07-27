#include <ECodeEditor/CodeEditorView.h>
#include <ECodeSyntax/SyntaxHighlighter.h>

#include <eacp/Core/App/App.h>
#include <eacp/Graphics/Graphics.h>

// One code file in a window, built out of the library rather than out of ECode.
//
// This is the app to run when changing anything in ECodeEditor or below. The
// tests drive `CodeEditorView` directly; what a consumer actually meets is this
// — a window, a menu bar, and a file that has to turn up in it — and the two
// fail differently. A view that draws correctly into `renderToImage` can still
// come up blank on a Retina display, ignore a menu command, or lose its scroll
// position when the font changes.
//
// It links `ECode::Editor` and `ECode::Syntax` and stops there. No tabs, no
// sidebar, no command palette, no settings file, no keymap — none of that is in
// the libraries it names, so none of it can appear by accident. The menu bar
// below is eacp's own `Graphics::MenuBar`, deliberately: ECode's `MenuBuilder`
// belongs to the workbench, and reaching for it here would be the first step of
// the split coming undone.
//
//   CodeViewer               shows its own source
//   CodeViewer <path>        shows that file
//   CodeViewer --edit <path> and lets you type in it
//
// Everything else is on the menus, and every one of them is a call into the
// embedding API: setReadOnly, setWordWrap, setFont, setTheme, setHighlighter,
// loadFile and save. Between them they are the whole of what CodeEditorView
// offers, which is the other reason this exists — an API nobody has driven by
// hand is an API with an unexamined corner in it.

using namespace eacp;

namespace ecode
{
// A light palette, written out here rather than fetched from ECode's built-in
// table: `Themes.h` resolves a *name* to a chrome palette as well as a document
// one, and the chrome half means the workbench. A consumer setting its own
// colours does exactly this — fill in a TextTheme and hand it over.
TextTheme lightTheme()
{
    auto theme = TextTheme {};

    theme.background = {0.99f, 0.99f, 0.98f};
    theme.text = {0.14f, 0.15f, 0.18f};
    theme.lineNumber = {0.65f, 0.67f, 0.70f};
    theme.currentLineNumber = {0.25f, 0.27f, 0.32f};
    theme.gutterEdge = {0.f, 0.f, 0.f, 0.06f};
    theme.caret = {0.11f, 0.38f, 0.75f};
    theme.selection = {0.72f, 0.83f, 0.95f};
    theme.currentLine = {0.f, 0.f, 0.f, 0.035f};

    theme.keyword = {0.61f, 0.22f, 0.66f};
    theme.string = {0.13f, 0.48f, 0.20f};
    theme.comment = {0.48f, 0.52f, 0.56f};
    theme.number = {0.72f, 0.40f, 0.09f};
    theme.function = {0.16f, 0.36f, 0.72f};
    theme.type = {0.10f, 0.48f, 0.50f};
    theme.constant = {0.72f, 0.24f, 0.28f};
    theme.operatorColor = {0.28f, 0.30f, 0.34f};

    return theme;
}

struct Arguments
{
    // Its own source, so the app is runnable straight from a checkout with
    // nothing to point it at.
    FilePath path {__FILE__};

    // A viewer by default: showing a file is the case the library exists for,
    // and read-only is the half of it that is easy to get wrong by leaving out.
    bool readOnly = true;
};

Arguments arguments()
{
    const auto& args = Apps::getAppEnvironment().commandLineArgs;

    auto parsed = Arguments {};

    for (auto index = 1; index < args.size(); ++index)
    {
        if (args[index] == "--edit")
            parsed.readOnly = false;
        else if (!args[index].empty())
            parsed.path = FilePath {args[index]};
    }

    return parsed;
}

Graphics::WindowOptions windowOptions(const Graphics::Color& background)
{
    auto options = Graphics::WindowOptions {};

    options.width = 900;
    options.height = 640;
    options.minWidth = 320;
    options.minHeight = 240;
    options.title = "CodeViewer";

    // What the window shows before the first frame, and along any edge a resize
    // outruns. Taken from the view's own theme so a light palette does not open
    // behind a dark flash.
    options.backgroundColor = background;

    return options;
}

struct App
{
    App()
    {
        const auto args = arguments();

        view.setReadOnly(args.readOnly);

        // Before the file, so the first parse is of the text that is about to be
        // shown rather than of the empty buffer it replaces.
        setHighlighting(true);

        openFile(args.path);

        // A dirty marker in the title is the only thing that says an edit has
        // not been saved — there is no tab strip here to put a dot on.
        view.onTextChanged = [this] { updateTitle(); };

        window.setContentView(view);

        // After the window exists, because that is what a menu bar attaches to
        // on the platforms that own menus per window.
        Graphics::setApplicationMenuBar(menuBar(), window);
    }

    // --- the file ---------------------------------------------------------

    void openFile(const FilePath& newPath)
    {
        // A path that cannot be read leaves the empty buffer the view starts
        // with, which is a working editor rather than an error state — so there
        // is nothing to recover from here beyond saying what happened.
        if (!view.loadFile(newPath))
        {
            LOG("could not read " + newPath.str());
            return;
        }

        path = newPath;
        updateTitle();
    }

    void chooseFile()
    {
        // An OS modal that blocks until answered, so a cancel is the common case
        // rather than an error and simply leaves everything as it was.
        if (const auto chosen = Apps::chooseFile())
            openFile(FilePath {*chosen});
    }

    void save()
    {
        switch (view.save())
        {
            case SaveResult::saved:
            case SaveResult::upToDate:
                break;

            // No dialog to ask in, so both of these are said out loud and the
            // buffer is left alone. ECode answers the same questions through its
            // title bar; a viewer has nowhere to put a second press.
            case SaveResult::changedOnDisk:
                LOG("not saved: " + path.str() + " changed on disk");
                break;

            case SaveResult::failed:
                LOG("could not write " + path.str());
                break;
        }

        updateTitle();
    }

    // --- how it is shown --------------------------------------------------

    void setHighlighting(bool on)
    {
        highlighting = on;

        if (!on)
        {
            // Null is a valid state and means "draw this as plain text", which
            // is also what a grammar that failed to load leaves behind.
            view.setHighlighter({});
            return;
        }

        // Budgeted, because the caller owns a frame: an 8,000-line file is
        // ~10 ms of tree-sitter against ~0.06 ms for the frame around it, so a
        // highlighter allowed to finish decides when the window first appears.
        // Given a budget it hands the frame back and the colours arrive over the
        // next few.
        auto syntax =
            makeOwned<SyntaxHighlighter>(SyntaxHighlighter::frameParseBudget);

        if (!syntax->isValid())
        {
            LOG("no C++ grammar available; drawing plain text");
            return;
        }

        view.setHighlighter(OwningPointer<Highlighter> {std::move(syntax)});
    }

    void setLightTheme(bool on)
    {
        light = on;
        view.setTheme(on ? lightTheme() : TextTheme {});
    }

    void zoom(int steps)
    {
        auto font = view.font();

        if (!font.canZoom(steps))
            return;

        font.zoomBy(steps);
        view.setFont(font);
    }

    void resetZoom()
    {
        auto font = view.font();

        font.resetZoom();
        view.setFont(font);
    }

    void updateTitle()
    {
        auto title = path.str();

        if (view.isReadOnly())
            title += "  —  read-only";
        else if (view.file().isDirty())
            title = "• " + title;

        // Only when it actually moved: this runs on every keystroke and setTitle
        // crosses into AppKit.
        if (title == shownTitle)
            return;

        shownTitle = std::move(title);
        window.setTitle(shownTitle);
    }

    // --- the menus --------------------------------------------------------

    Graphics::MenuBar menuBar()
    {
        auto bar = Graphics::MenuBar {};

        // Gives the app its About, Hide and Quit — including ⌘Q, which without a
        // menu bar macOS does not provide at all.
        bar.add(Graphics::standardApplicationMenu("CodeViewer"));

        bar.add(fileMenu());
        bar.add(viewMenu());

        return bar;
    }

    Graphics::Menu fileMenu()
    {
        auto menu = Graphics::Menu {"File"};

        menu.add(Graphics::MenuItem::withAction(
            "Open…", [this] { chooseFile(); }, Graphics::commandKey("o")));

        menu.add(Graphics::MenuItem::withAction(
            "Reload", [this] { openFile(path); }, Graphics::commandKey("r")));

        menu.addSeparator();

        // Greyed rather than hidden while read-only, and rather than absent: an
        // item that is visibly unavailable says the file is protected, where a
        // missing one says nothing at all.
        menu.add(Graphics::MenuItem::withAction(
            "Save",
            [this] { save(); },
            Graphics::commandKey("s"),
            [this] { return !view.isReadOnly(); }));

        return menu;
    }

    Graphics::Menu viewMenu()
    {
        auto menu = Graphics::Menu {"View"};

        // Checkable rather than a pair of items, and the predicate reads the
        // view rather than a copy of the flag — so the mark cannot drift from
        // what the editor is actually doing. eacp asks it each time the menu
        // opens, which is why none of these rebuild the bar.
        menu.add(Graphics::MenuItem::withCheckableAction(
            "Read Only",
            [this]
            {
                view.setReadOnly(!view.isReadOnly());
                updateTitle();
            },
            [this] { return view.isReadOnly(); }));

        menu.add(Graphics::MenuItem::withCheckableAction(
            "Word Wrap",
            [this] { view.setWordWrap(!view.isWordWrapped()); },
            [this] { return view.isWordWrapped(); },
            Graphics::commandAltKey("z")));

        menu.add(Graphics::MenuItem::withCheckableAction(
            "Syntax Highlighting",
            [this] { setHighlighting(!highlighting); },
            [this] { return highlighting; }));

        menu.add(Graphics::MenuItem::withCheckableAction(
            "Light Theme",
            [this] { setLightTheme(!light); },
            [this] { return light; }));

        menu.addSeparator();

        menu.add(Graphics::MenuItem::withAction(
            "Increase Font Size",
            [this] { zoom(1); },
            Graphics::commandKey("+"),
            [this] { return view.font().canZoom(1); }));

        menu.add(Graphics::MenuItem::withAction(
            "Decrease Font Size",
            [this] { zoom(-1); },
            Graphics::commandKey("-"),
            [this] { return view.font().canZoom(-1); }));

        menu.add(Graphics::MenuItem::withAction(
            "Reset Font Size",
            [this] { resetZoom(); },
            Graphics::commandKey("0"),
            [this] { return view.font().zoom != 0.f; }));

        return menu;
    }

    // The view first, and it has to stay first: the window's background colour
    // is read off the theme the view holds.
    CodeEditorView view;
    Graphics::Window window {windowOptions(view.theme().background)};

    FilePath path;
    std::string shownTitle;

    bool highlighting = true;
    bool light = false;
};
} // namespace ecode

int main(int argc, char* argv[])
{
    return eacp::Apps::run<ecode::App>(argc, argv);
}
