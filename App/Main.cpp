#include <ECodeCore/TextFile.h>
#include <ECodeRender/TextRenderer.h>
#include <ECodeSyntax/SyntaxHighlighter.h>

#include <eacp/GPU/GPU.h>
#include <eacp/Graphics/Graphics.h>
#include <eacp/Core/App/Clipboard.h>
#include <eacp/Sprites/Sprites.h>

#include <algorithm>
#include <functional>
#include <optional>
#include <string>

using namespace eacp;

namespace ecode
{
// The editor window: one file, drawn on the GPU, highlighted, typed in, saved.
//
// The chrome is still hardcoded rectangles. That is deliberate — the widget
// layer is the next real design decision and building it under a single
// hand-drawn tab is cheaper than building it under a wrong one.

struct Chrome
{
    static constexpr auto activityBarWidth = 48.f;
    static constexpr auto sidebarWidth = 240.f;
    static constexpr auto tabBarHeight = 35.f;
    static constexpr auto statusBarHeight = 22.f;

    static constexpr auto activityBar = Graphics::Color {0.094f, 0.098f, 0.118f};
    static constexpr auto sidebar = Graphics::Color {0.102f, 0.110f, 0.129f};
    static constexpr auto tabBar = Graphics::Color {0.086f, 0.090f, 0.110f};
    static constexpr auto activeTab = Graphics::Color {0.118f, 0.125f, 0.149f};
    static constexpr auto statusBar = Graphics::Color {0.180f, 0.192f, 0.235f};

    // Unsaved work, and a save that could not happen. Until the tab strip draws
    // filenames these two dots are the whole of the file's status on screen.
    static constexpr auto unsaved = Graphics::Color {0.694f, 0.741f, 0.831f};
    static constexpr auto conflict = Graphics::Color {0.898f, 0.541f, 0.310f};
};

// `ECode <path>`, falling back to this file. Until there is a file tree it is
// the only way to open anything, and it is what makes the editor testable
// against a scratch file rather than against its own source — which now that
// Cmd+S works is a difference that matters.
FilePath fileToOpen()
{
    const auto& args = Apps::getAppEnvironment().commandLineArgs;

    if (args.size() > 1 && !args[1].empty())
        return FilePath {args[1]};

    return FilePath {__FILE__};
}

struct EditorView final : GPU::GPUView
{
    EditorView()
    {
        // Text is grayscale-antialiased in the atlas already, so multisampling
        // the surface buys nothing and costs bandwidth. eacp defaults to 4.
        setSampleCount(1);
        setHandlesMouseEvents(true);
        setGrabsFocusOnMouseDown(true);

        openFile(fileToOpen());

        auto syntax = makeOwned<SyntaxHighlighter>();

        // A grammar that failed to load leaves highlighter null, and everything
        // still draws as plain text.
        if (syntax->isValid())
            highlighter = std::move(syntax);

        // Edits go straight to the syntax engine so it reparses the affected
        // subtree rather than the file.
        editor.onEdit = [this](const TextEdit& edit)
        {
            if (highlighter != nullptr)
                highlighter->applyEdit(editor.document(), edit);
        };

        editor.onDocumentReplaced = [this]
        {
            if (highlighter != nullptr)
                highlighter->reset();
        };
    }

    void openFile(const FilePath& path)
    {
        if (!file.open(path))
            return;

        conflicted = false;
        scrollY = 0.f;
        showCaret = true;

        updateTitle();
    }

    const Document& document() const { return editor.document(); }

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

        renderer.emplace(*atlas, theme);
        glyphs.emplace();
        builtAtScale = scale;
    }

    void resized() override
    {
        GPUView::resized();

        const auto bounds = getLocalBounds();

        if (bounds.w > 0 && bounds.h > 0)
        {
            sprites.emplace(Graphics::Point {bounds.w, bounds.h}, sampleCount());

            if (glyphs)
                glyphs->setViewportSize({bounds.w, bounds.h});
        }

        clampScroll();
        repaint();
    }

    void backingScaleChanged() override
    {
        GPUView::backingScaleChanged();

        // Glyphs cached for the old display are the wrong size now.
        ensureRenderer();
        repaint();
    }

    Graphics::Rect editorArea() const
    {
        auto area = getLocalBounds();

        area.removeFromLeft(Chrome::activityBarWidth);
        area.removeFromLeft(Chrome::sidebarWidth);
        area.removeFromTop(Chrome::tabBarHeight);
        area.removeFromBottom(Chrome::statusBarHeight);

        return area;
    }

    void clampScroll()
    {
        if (!renderer)
            return;

        const auto area = editorArea();
        const auto content = renderer->contentHeight(document());

        // Stop at the last line rather than letting the document scroll off the
        // top, but never push a short document around.
        const auto lowest = std::min(0.f, area.h - content);

        scrollY = std::clamp(scrollY, lowest, 0.f);
    }

    // Any interaction restarts the blink, so the caret is solid while working
    // and only pulses when idle — a caret that blinks out mid-keystroke reads
    // as dropped input.
    void wake()
    {
        showCaret = true;
        blinkPhase = 0;
        scrollToCaret();
        updateTitle();
        repaint();
    }

    void updateTitle()
    {
        auto text = windowTitle();

        // Cached because wake() runs on every keystroke and setTitle crosses
        // into AppKit; the title only actually changes when dirtiness does.
        if (text == shownTitle)
            return;

        shownTitle = std::move(text);
        onTitleChanged(shownTitle);
    }

    void saveFile()
    {
        // The second press takes the conflict. Refusing forever would strand
        // the text in the buffer, and there is no dialog to ask in until the
        // widget layer exists — so the title asks, and Cmd+S answers.
        const auto result = conflicted ? file.saveOverwriting() : file.save();

        conflicted = result == SaveResult::changedOnDisk;

        updateTitle();
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
        {
            file.reload();
            clampScroll();
        }
        else
        {
            conflicted = true;
        }

        updateTitle();
        repaint();
    }

    void scrollToCaret()
    {
        if (!renderer)
            return;

        const auto area = editorArea();
        const auto line = document().lineAt(editor.cursor().head);
        const auto lineHeight = renderer->lineHeight();

        const auto top = static_cast<float>(line) * lineHeight;
        const auto bottom = top + lineHeight;

        // Only move when the caret has actually left the viewport, so typing in
        // the middle of the screen does not drag the view around.
        if (top + scrollY < 0.f)
            scrollY = -top;
        else if (bottom + scrollY > area.h)
            scrollY = area.h - bottom;

        clampScroll();
    }

    bool handleShortcut(const Graphics::KeyEvent& event)
    {
        if (!event.modifiers.command)
            return false;

        const auto& key = event.charactersIgnoringModifiers;

        if (key == "z")
        {
            event.modifiers.shift ? editor.redo() : editor.undo();
            return true;
        }

        if (key == "a")
        {
            editor.selectAll();
            return true;
        }

        if (key == "s")
        {
            saveFile();
            return true;
        }

        if (key == "c" || key == "x")
        {
            if (const auto selected = editor.selectedText(); !selected.empty())
            {
                Clipboard::copyText(selected);

                if (key == "x")
                    editor.backspace();
            }

            return true;
        }

        if (key == "v")
        {
            if (Clipboard::hasText())
            {
                // A paste is one undo step whatever it contains, so it never
                // merges with typing either side of it.
                editor.breakUndoStep();
                editor.insert(Clipboard::getText());
                editor.breakUndoStep();
            }

            return true;
        }

        // Any other Cmd chord is swallowed rather than typed as text.
        return true;
    }

    void keyDown(const Graphics::KeyEvent& event) override
    {
        using namespace Graphics;

        const auto shift = event.modifiers.shift;
        const auto word = event.modifiers.alt;

        if (handleShortcut(event))
        {
            wake();
            return;
        }

        switch (event.keyCode)
        {
            case KeyCode::LeftArrow:
                word ? editor.moveWordLeft(shift) : editor.moveLeft(shift);
                wake();
                return;

            case KeyCode::RightArrow:
                word ? editor.moveWordRight(shift) : editor.moveRight(shift);
                wake();
                return;

            case KeyCode::UpArrow:
                editor.moveUp(shift);
                wake();
                return;

            case KeyCode::DownArrow:
                editor.moveDown(shift);
                wake();
                return;

            case KeyCode::Home:
                editor.moveToLineStart(shift);
                wake();
                return;

            case KeyCode::End:
                editor.moveToLineEnd(shift);
                wake();
                return;

            case KeyCode::PageUp:
                editor.moveUp(shift, visibleLines());
                wake();
                return;

            case KeyCode::PageDown:
                editor.moveDown(shift, visibleLines());
                wake();
                return;

            case KeyCode::Delete:
                word ? editor.deleteWordBefore() : editor.backspace();
                wake();
                return;

            case KeyCode::ForwardDelete:
                word ? editor.deleteWordAfter() : editor.deleteForward();
                wake();
                return;

            case KeyCode::Return:
                editor.insert("\n");
                wake();
                return;

            case KeyCode::Tab:
                editor.insert("    ");
                wake();
                return;

            default:
                break;
        }

        // Control characters would be inserted literally and rasterize as
        // boxes; `characters` carries the resolved text for everything else,
        // including dead-key composition.
        if (!event.characters.empty() && !event.modifiers.control
            && static_cast<unsigned char>(event.characters[0]) >= 0x20)
        {
            editor.insert(event.characters);
            wake();
        }
    }

    int visibleLines() const
    {
        if (!renderer || renderer->lineHeight() <= 0.f)
            return 1;

        return std::max(
            1, static_cast<int>(editorArea().h / renderer->lineHeight()) - 1);
    }

    void mouseDown(const Graphics::MouseEvent& event) override
    {
        if (!renderer)
            return;

        const auto area = editorArea();
        const auto offset =
            renderer->offsetAtPoint(document(), event.pos, area, scrollY);

        if (event.clickCount >= 3)
            editor.selectLineAt(offset);
        else if (event.clickCount == 2)
            editor.selectWordAt(offset);
        else
            editor.placeCaret(offset, event.modifiers.shift);

        wake();
    }

    void mouseDragged(const Graphics::MouseEvent& event) override
    {
        if (!renderer)
            return;

        const auto area = editorArea();

        // Always an extension: the anchor was set on mouse-down.
        editor.placeCaret(
            renderer->offsetAtPoint(document(), event.pos, area, scrollY), true);

        wake();
    }

    void mouseWheel(const Graphics::MouseEvent& event) override
    {
        if (!renderer)
            return;

        // A trackpad reports points; a notched wheel reports lines, and only
        // this view knows how tall a line is.
        const auto points = event.preciseScrolling
                                ? event.delta.y
                                : event.delta.y * renderer->lineHeight() * 3.f;

        scrollY += points;
        clampScroll();
        repaint();
    }

    void render(GPU::Frame& frame) override
    {
        ensureRenderer();

        auto pass = frame.beginPass({theme.background});

        if (!sprites)
            return;

        sprites->begin(pass);
        drawChrome();

        if (!renderer || !atlas || !glyphs)
            return;

        glyphs->setViewportSize({getLocalBounds().w, getLocalBounds().h});

        const auto area = editorArea();

        // Every glyph this frame needs is rasterized before the first draw, then
        // uploaded once. Uploading mid-pass would mutate a texture the earlier
        // draws have already bound.
        renderer->prepare(document(), area, scrollY);
        atlas->commit();

        // Highlight exactly the lines about to be drawn: tree-sitter parses the
        // whole file, but querying it all would put scrolling cost back in
        // proportion to file size.
        if (highlighter != nullptr)
            highlighter->update(
                document(),
                renderer->firstVisibleLine(scrollY),
                renderer->lastVisibleLine(document(), area, scrollY));

        renderer->draw(pass,
                       *sprites,
                       *glyphs,
                       document(),
                       &editor.cursor(),
                       showCaret && hasFocus(),
                       highlighter.get(),
                       area,
                       scrollY,
                       builtAtScale);
    }

    void drawChrome()
    {
        const auto bounds = getLocalBounds();
        auto area = bounds;

        const auto activityBar = area.removeFromLeft(Chrome::activityBarWidth);
        const auto sidebar = area.removeFromLeft(Chrome::sidebarWidth);
        const auto tabBar = area.removeFromTop(Chrome::tabBarHeight);
        const auto statusBar = area.removeFromBottom(Chrome::statusBarHeight);

        sprites->fillRect(activityBar, Chrome::activityBar);
        sprites->fillRect(sidebar, Chrome::sidebar);
        sprites->fillRect(tabBar, Chrome::tabBar);
        sprites->fillRect(statusBar, Chrome::statusBar);

        // A single tab, sized to the filename, standing in for the tab strip.
        const auto tab = Graphics::Rect {tabBar.x, tabBar.y, 180.f, tabBar.h};
        sprites->fillRect(tab, Chrome::activeTab);

        if (file.isDirty() || conflicted)
        {
            const auto size = 7.f;
            const auto colour = conflicted ? Chrome::conflict : Chrome::unsaved;

            sprites->fillRect(
                {tab.x + 14.f, tab.y + (tab.h - size) * 0.5f, size, size}, colour);
        }
    }

    TextTheme theme;

    TextFile file;

    // The editor lives inside the file, and every call site here wants the
    // editor rather than the file, so it gets a name of its own.
    Editor& editor = file.editor();

    std::function<void(const std::string&)> onTitleChanged =
        [](const std::string&) {};

    std::optional<Sprites::SpriteRenderer> sprites;
    OwningPointer<Text::GlyphAtlas> atlas;
    std::optional<TextRenderer> renderer;
    std::optional<Text::GlyphRenderer> glyphs;
    OwningPointer<SyntaxHighlighter> highlighter;

    float builtAtScale = 1.f;
    float scrollY = 0.f;

    std::string shownTitle;

    // Set when a save was refused because the file moved underneath us, and
    // cleared by the save that resolves it.
    bool conflicted = false;

    // The caret holds solid for the first few ticks after any interaction, then
    // pulses; see wake().
    bool showCaret = true;
    int blinkPhase = 0;

    Threads::Timer blink {[this]
                          {
                              if (++blinkPhase < 2)
                                  return;

                              showCaret = !showCaret;
                              repaint();
                          },
                          2};

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
