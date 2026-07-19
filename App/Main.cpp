#include <ECodeUI/Chrome.h>
#include <ECodeUI/EditorWidget.h>
#include <ECodeUI/Theme.h>
#include <ECodeUI/WidgetHost.h>
#include <ECodeSyntax/SyntaxHighlighter.h>

#include <eacp/Core/App/Clipboard.h>
#include <eacp/GPU/GPU.h>
#include <eacp/Graphics/Graphics.h>
#include <eacp/Sprites/Sprites.h>

#include <optional>
#include <string>

using namespace eacp;

namespace ecode
{
// The editor window: one file, drawn on the GPU, highlighted, typed in, saved.
//
// The chrome is a widget tree now rather than hardcoded rectangles, so this
// file is down to what an application shell actually owns — the GPU resources
// that depend on the display, the command chords, and the two timers.

// `ECode <path>`, falling back to this file. Until there is a file tree it is
// the only way to open anything, and it is what makes the editor testable
// against a scratch file rather than against its own source.
FilePath fileToOpen()
{
    const auto& args = Apps::getAppEnvironment().commandLineArgs;

    if (args.size() > 1 && !args[1].empty())
        return FilePath {args[1]};

    return FilePath {__FILE__};
}

// The window's whole layout. Activity bar and sidebar off the left, tab strip
// off the top, status bar off the bottom, editor taking what is left — which is
// exactly what Rect's splitters express, and only reads correctly now that they
// are y-down.
struct WindowLayout final : Widget
{
    explicit WindowLayout(TextFile& file)
        : editor(file)
    {
        addChild(activityBar);
        addChild(sidebar);
        addChild(tabs);
        addChild(status);
        addChild(editor);
    }

    void layout() override
    {
        auto area = bounds();

        // The status bar comes off first so it spans the whole window, under
        // the sidebar as well as the editor. Taking it after the left columns
        // would leave it starting at the editor's edge, which is what the
        // hardcoded chrome did and what VSCode does not.
        status.setBounds(area.removeFromBottom(statusBarHeight));

        activityBar.setBounds(area.removeFromLeft(activityBarWidth));
        sidebar.setBounds(area.removeFromLeft(sidebarWidth));

        // Tabs belong to the editor group, so they start where the sidebar
        // ends rather than spanning the window.
        tabs.setBounds(area.removeFromTop(tabBarHeight));
        editor.setBounds(area);
    }

    static constexpr auto activityBarWidth = 48.f;
    static constexpr auto sidebarWidth = 240.f;
    static constexpr auto tabBarHeight = 35.f;
    static constexpr auto statusBarHeight = 22.f;

    ChromeTheme theme;

    Panel activityBar {theme.activityBar};
    Panel sidebar {theme.sidebar};
    TabBar tabs {theme};
    StatusBar status {theme};
    EditorWidget editor;
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

        openFile(fileToOpen());

        auto syntax = makeOwned<SyntaxHighlighter>();

        // A grammar that failed to load leaves highlighter null, and everything
        // still draws as plain text.
        if (syntax->isValid())
            highlighter = std::move(syntax);

        layout.editor.setHighlighter(highlighter.get());

        // Edits go straight to the syntax engine so it reparses the affected
        // subtree rather than the file.
        editor().onEdit = [this](const TextEdit& edit)
        {
            if (highlighter != nullptr)
                highlighter->applyEdit(editor().document(), edit);
        };

        editor().onDocumentReplaced = [this]
        {
            if (highlighter != nullptr)
                highlighter->reset();
        };

        layout.editor.onStateChanged = [this] { updateChrome(); };

        // The editor is the only focusable thing in the window so far, and a
        // window that opens with no caret reads as broken.
        host.setFocus(&layout.editor);
    }

    Editor& editor() { return file.editor(); }

    void openFile(const FilePath& path)
    {
        if (!file.open(path))
            return;

        conflicted = false;
        updateChrome();
    }

    // What the window's title bar should read. A pure function of the file's
    // state, so App can ask for it once at startup and then let onTitleChanged
    // push every later change.
    std::string windowTitle() const
    {
        auto name = file.name();

        if (name.empty())
            name = "Untitled";

        if (file.isDirty())
            name = "• " + name;

        // There is no dialog to ask in yet, so the title carries the question.
        // A second Cmd+S answers it; see saveFile.
        if (conflicted)
            name += "  —  changed on disk. ⌘S again to overwrite";

        return name;
    }

    // Pushes the file's state into the chrome that displays it. Cheap enough to
    // call on every keystroke: both the tab strip and the status bar compare
    // before they store, so an unchanged state asks for no frame.
    void updateChrome()
    {
        auto tab = TabItem {};
        tab.title = file.name().empty() ? "Untitled" : file.name();
        tab.modified = file.isDirty();
        tab.conflicted = conflicted;

        layout.tabs.setTabs({tab});

        layout.status.setText("Ln " + std::to_string(layout.editor.caretLine())
                                  + ", Col "
                                  + std::to_string(layout.editor.caretColumn()),
                              "UTF-8    C++");

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

        renderer.emplace(*atlas, textTheme);
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
        // The second press takes the conflict. Refusing forever would strand
        // the text in the buffer, and there is no dialog to ask in until there
        // is a widget for one — so the title asks, and Cmd+S answers.
        const auto result = conflicted ? file.saveOverwriting() : file.save();

        conflicted = result == SaveResult::changedOnDisk;

        updateChrome();
        repaint();
    }

    // Standing in for file watching, which eacp does not have: one stat a
    // second, which is nothing next to a frame.
    void checkDisk()
    {
        if (!file.hasChangedOnDisk())
            return;

        // Nothing local to lose, so take the new version — a git checkout or a
        // formatter run should simply appear.
        if (!file.isDirty())
            file.reload();
        else
            conflicted = true;

        updateChrome();
        repaint();
    }

    // The chords that belong to the window rather than to whatever has focus.
    // Matched on charactersIgnoringModifiers so Cmd+C is "c" on any layout.
    bool handleShortcut(const Graphics::KeyEvent& event)
    {
        if (!event.modifiers.command)
            return false;

        const auto& key = event.charactersIgnoringModifiers;

        if (key == "z")
        {
            event.modifiers.shift ? editor().redo() : editor().undo();
            return true;
        }

        if (key == "a")
        {
            editor().selectAll();
            return true;
        }

        if (key == "s")
        {
            saveFile();
            return true;
        }

        if (key == "c" || key == "x")
        {
            if (const auto selected = editor().selectedText(); !selected.empty())
            {
                Clipboard::copyText(selected);

                if (key == "x")
                    editor().backspace();
            }

            return true;
        }

        if (key == "v")
        {
            if (Clipboard::hasText())
            {
                // A paste is one undo step whatever it contains, so it never
                // merges with typing either side of it.
                editor().breakUndoStep();
                editor().insert(Clipboard::getText());
                editor().breakUndoStep();
            }

            return true;
        }

        // Any other Cmd chord is swallowed rather than typed as text.
        return true;
    }

    void keyDown(const Graphics::KeyEvent& event) override
    {
        if (handleShortcut(event))
        {
            layout.editor.wake();
            return;
        }

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

    void mouseUp(const Graphics::MouseEvent& event) override { host.mouseUp(event); }

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

        auto context = PaintContext {pass,
                                     *sprites,
                                     *glyphs,
                                     *atlas,
                                     getLocalBounds(),
                                     builtAtScale};

        host.paint(context);
    }

    TextTheme textTheme;

    TextFile file;
    WindowLayout layout {file};
    WidgetHost host;

    std::function<void(const std::string&)> onTitleChanged =
        [](const std::string&) {};

    std::optional<Sprites::SpriteRenderer> sprites;
    OwningPointer<Text::GlyphAtlas> atlas;
    std::optional<TextRenderer> renderer;
    std::optional<Text::GlyphRenderer> glyphs;
    OwningPointer<SyntaxHighlighter> highlighter;

    float builtAtScale = 1.f;

    std::string shownTitle;

    // Set when a save was refused because the file moved underneath us, and
    // cleared by the save that resolves it.
    bool conflicted = false;

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
