#include "CodeEditorView.h"

namespace ecode
{
using namespace eacp;

CodeEditorView::CodeEditorView()
{
    // Text is grayscale-antialiased in the atlas already, so multisampling the
    // surface buys nothing and costs bandwidth. eacp defaults to 4.
    setSampleCount(1);

    setHandlesMouseEvents(true);
    setGrabsFocusOnMouseDown(true);

    host.setRoot(widget);

    // The host attaches nothing itself, so this is what turns a widget's
    // repaint() into a frame.
    widget.onRepaintNeeded = [this] { repaint(); };

    // The one widget in the tree, so there is nothing else focus could mean.
    // Given here rather than on the first click so the arrow keys work as soon
    // as the *window* gives this view the keyboard — a host that never sends a
    // click, having focused us from its own chrome, would otherwise find every
    // key ignored.
    host.setFocus(&widget);

    connectHighlighter();

    widget.onStateChanged = [this] { onTextChanged(); };
}

// --- the text ---------------------------------------------------------------

void CodeEditorView::setText(std::string newText)
{
    open.file.setText(std::move(newText));

    onTextChanged();
    repaint();
}

const std::string& CodeEditorView::text() const
{
    return open.file.document().text();
}

bool CodeEditorView::loadFile(const FilePath& path)
{
    if (!open.file.open(path))
        return false;

    // Nothing carries over from the file that was showing: the scroll offset
    // belongs to a document that is no longer here, and a view left at line
    // 4,000 of a file with forty is a blank screen with nothing to say why.
    open.scroll = {};

    onTextChanged();
    repaint();

    return true;
}

SaveResult CodeEditorView::save()
{
    const auto result = open.file.save();

    // The dirty flag moved, which is the only thing a host hangs a marker off.
    if (result == SaveResult::saved)
        onTextChanged();

    return result;
}

// --- how it is shown --------------------------------------------------------

void CodeEditorView::setTheme(const TextTheme& newTheme)
{
    textTheme = newTheme;

    // Not something ensureRenderer will get to on its own: it rebuilds when the
    // font or the display scale has moved, and a colour is neither. The
    // renderer holds the theme by value and was built against the one before
    // this.
    rebuildRenderer();
    repaint();
}

void CodeEditorView::setFont(const FontSettings& newFont)
{
    // Nothing is rasterized here. ensureRenderer owns the decision of when an
    // atlas is stale and is the only thing that knows the display's scale, so
    // this sets what the next frame will build.
    fontSettings = newFont;
    repaint();
}

// --- syntax highlighting ----------------------------------------------------

void CodeEditorView::setHighlighter(OwningPointer<Highlighter> newHighlighter)
{
    open.highlighter = std::move(newHighlighter);

    connectHighlighter();
    repaint();
}

void CodeEditorView::connectHighlighter()
{
    auto& editor = open.file.editor();

    auto* target = open.highlighter.get();

    if (target == nullptr)
    {
        // Put back, not left pointing at the highlighter that has just gone.
        // Editor invokes these without a null check, which is the whole point
        // of their having non-null defaults.
        editor.onEdit = [](const TextEdit&) {};
        editor.onDocumentReplaced = [] {};

        return;
    }

    // Captured by pointer rather than by reference to the entry, because the
    // entry outlives neither of these: both are replaced when it changes.
    editor.onEdit = [target, &editor](const TextEdit& edit)
    { target->applyEdit(editor.document(), edit); };

    editor.onDocumentReplaced = [target] { target->reset(); };
}

// --- the GPU resources ------------------------------------------------------

void CodeEditorView::ensureRenderer()
{
    const auto scale = backingScale();

    if (atlas && builtAtScale == scale && builtFont == fontSettings)
        return;

    auto built = makeGlyphAtlas(fontSettings, scale);

    // A family that resolved to nothing at all. Everything below tolerates a
    // null atlas by drawing nothing, which is better than drawing at a size
    // that answers for the wrong display.
    if (!built)
        return;

    atlas = std::move(built);

    builtFont = fontSettings;
    builtAtScale = scale;

    // Independent of the atlas — one is named at flush time — but it needs a
    // device, so it is made once there is one.
    if (!glyphs)
        glyphs.emplace();

    rebuildRenderer();
}

void CodeEditorView::rebuildRenderer()
{
    if (!atlas)
        return;

    // Asked before the old renderer goes, because it is the only thing that can
    // turn the scroll offset back into a line and a column.
    const auto topLine = widget.topVisibleLine();
    const auto leftColumn = widget.leftVisibleColumn();

    // Let go through the widget before the old renderer is destroyed, rather
    // than relying on the two statements being adjacent: emplace() runs the
    // destructor of whatever was there, and the widget would be holding a
    // pointer into it for as long as that took.
    widget.setRenderer(nullptr);

    renderer.emplace(*atlas, textTheme, builtAtScale);

    widget.setRenderer(&renderer.value());
    widget.scrollToTopLine(topLine);
    widget.scrollToLeftColumn(leftColumn);
}

void CodeEditorView::ensureSurface()
{
    const auto bounds = getLocalBounds();

    if (bounds.w <= 0.f || bounds.h <= 0.f)
        return;

    if (sprites && surfaceWidth == bounds.w && surfaceHeight == bounds.h)
        return;

    // SpriteRenderer bakes its logical size at construction, so a resize means a
    // new one rather than a setter.
    sprites.emplace(Graphics::Point {bounds.w, bounds.h}, sampleCount());

    surfaceWidth = bounds.w;
    surfaceHeight = bounds.h;

    if (glyphs)
        glyphs->setViewportSize({bounds.w, bounds.h});

    host.setBounds(bounds);
}

void CodeEditorView::resized()
{
    GPUView::resized();

    ensureSurface();
    repaint();
}

void CodeEditorView::backingScaleChanged()
{
    GPUView::backingScaleChanged();

    // Glyphs cached for the old display are the wrong size now.
    ensureRenderer();
    repaint();
}

void CodeEditorView::render(GPU::Frame& frame)
{
    ensureSurface();
    ensureRenderer();

    auto pass = frame.beginPass({textTheme.background});

    // The pass still cleared to the background, so a view whose atlas could not
    // be built shows an empty document rather than whatever was on the drawable.
    if (!sprites || !atlas || !glyphs)
        return;

    const auto bounds = getLocalBounds();

    glyphs->setViewportSize({bounds.w, bounds.h});

    // Every glyph the frame needs is rasterized before the first draw, then
    // uploaded once. Uploading mid-pass would mutate a texture the earlier draws
    // have already bound, and the symptom is the previous frame's text appearing
    // in this one.
    host.prepare(*atlas);
    atlas->commit();

    auto context =
        PaintContext {pass, *sprites, *glyphs, *atlas, bounds, builtAtScale};

    host.paint(context);
}

// --- input ------------------------------------------------------------------
//
// Straight through to the host, which does the routing, the capture and the
// focus. Nothing is decided here: the point of WidgetHost being separate from
// the view is that all of it is testable without a device.

void CodeEditorView::mouseDown(const Graphics::MouseEvent& event)
{
    host.mouseDown(event);
}

void CodeEditorView::mouseDragged(const Graphics::MouseEvent& event)
{
    host.mouseDragged(event);
}

void CodeEditorView::mouseUp(const Graphics::MouseEvent& event)
{
    host.mouseUp(event);

    // A release away from where the pointer started has to settle the cursor
    // shape. Nothing else would until the pointer moved again, and macOS sends
    // no move for a button release.
    setMouseCursor(host.cursorAt(event.pos));
}

void CodeEditorView::mouseMoved(const Graphics::MouseEvent& event)
{
    host.mouseMoved(event);

    // There is exactly one cursor for the whole view, so a widget can only say
    // what it wants and the view is what applies it. Setting the same shape
    // twice is free, so this asks on every move rather than tracking changes.
    setMouseCursor(host.cursorAt(event.pos));
}

void CodeEditorView::mouseExited(const Graphics::MouseEvent&)
{
    host.mouseExited();
}

void CodeEditorView::mouseWheel(const Graphics::MouseEvent& event)
{
    host.mouseWheel(event);
}

void CodeEditorView::keyDown(const Graphics::KeyEvent& event)
{
    // Whatever the editor does not take is left alone rather than swallowed,
    // including every command chord — EditorWidget refuses those outright. A
    // host embedding this keeps its own shortcuts, which is the difference
    // between a text view and an application that has taken over the keyboard.
    host.keyDown(event);
}
} // namespace ecode
