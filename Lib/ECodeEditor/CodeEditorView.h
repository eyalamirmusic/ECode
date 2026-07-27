#pragma once

#include "EditorWidget.h"

#include <ECodeRender/FontSettings.h>
#include <ECodeRender/TextRenderer.h>
#include <ECodeRender/TextTheme.h>
#include <ECodeWidgets/WidgetHost.h>

#include <eacp/Core/Core.h>
#include <eacp/GPU/GPU.h>
#include <eacp/Sprites/Sprites.h>

#include <functional>
#include <optional>
#include <string>

namespace ecode
{
// One code file, drawn on the GPU, and nothing around it.
//
// This is what another project embeds. Everything ECode's own window puts around
// a document — tabs, the sidebar tree, the command palette, the find bar, the
// status bar, the settings file, the keymap — is in ECodeWorkbench and none of
// it is linked from here. What is left is the part a host actually asked for:
// text, a gutter, syntax colours, a caret, selection, and scrolling in both
// directions.
//
// **It owns the GPU resources, and that is the whole reason it exists.** The
// atlas, the sprite renderer and the glyph batch cannot be built until the view
// is on a display, must be rebuilt when it moves to one with a different scale,
// and have an ordering constraint between them that has no CPU-side symptom when
// it is broken — a glyph first touched during paint mutates a texture the pass
// has already bound, and the picture that comes out is last frame's text. That
// sequence used to live in App/Main.cpp, where a second application could not
// reach it; this is it, extracted, so embedding costs a constructor rather than
// 250 lines that have to be got right in the same order.
//
// A `GPUView` rather than a `Widget`, because the whole architecture turns on
// there being exactly one GPU view — see PLAN.md §1. A host adds this with
// `Graphics::View::addSubview` alongside its own native chrome. A host that has
// its own widget tree inside its own GPUView wants EditorWidget instead, which
// is a plain Widget and is what this is built out of.
//
// Highlighting is opt-in and injected, so this target does not link tree-sitter:
// see setHighlighter.
class CodeEditorView : public eacp::GPU::GPUView
{
public:
    CodeEditorView();

    // --- the text --------------------------------------------------------

    // Replaces the buffer. The undo history does not survive it, which is what
    // setDocument means: the edits it holds describe text that is no longer
    // there.
    void setText(std::string newText);
    const std::string& text() const;

    // Reads the file into the buffer. False if it cannot be read, in which case
    // nothing changes — so a host can offer a path without checking it first.
    bool loadFile(const eacp::FilePath& path);

    // Writes it back, atomically, refusing rather than clobbering a write that
    // landed since the read. See TextFile::save for what each result means.
    SaveResult save();

    // --- how it is shown -------------------------------------------------

    // A view that cannot be typed into. Selection, copy, movement, scrolling and
    // search all go on working; see EditorWidget::setReadOnly.
    void setReadOnly(bool shouldBeReadOnly) { widget.setReadOnly(shouldBeReadOnly); }
    bool isReadOnly() const { return widget.isReadOnly(); }

    // The document's colours: background, gutter, caret, selection, and one per
    // TokenKind. Defaults to the dark palette ECode itself opens in.
    //
    // Applied to the next frame rather than kept for one, because the renderer
    // holds the theme by value and was built against the one before this.
    void setTheme(const TextTheme& newTheme);
    const TextTheme& theme() const { return textTheme; }

    // The family and size to rasterize at. A change rebuilds the atlas on the
    // next frame, keeping the top line where it was — the offset is in points
    // and the row height has just moved under it.
    void setFont(const FontSettings& newFont);
    const FontSettings& font() const { return fontSettings; }

    // Off by default, which is what code wants: a wrapped line hides the
    // indentation that says what it belongs to.
    void setWordWrap(bool shouldWrap) { widget.setWordWrap(shouldWrap); }
    bool isWordWrapped() const { return widget.isWordWrapped(); }

    // --- syntax highlighting ---------------------------------------------

    // Colours the text, or draws it plain when null — which is the default, and
    // the reason this target links no grammar.
    //
    // Injected rather than constructed here for the reason Workspace takes a
    // factory: ECodeCore knows the Highlighter interface and nothing about who
    // implements it, so a host that wants C++ links ECodeSyntax and hands over a
    // SyntaxHighlighter, and a host that wants its own language implements the
    // interface instead. Wiring the incremental-reparse callbacks is done here,
    // since forgetting them is the failure that draws perfectly until the second
    // keystroke.
    void setHighlighter(eacp::OwningPointer<Highlighter> newHighlighter);
    Highlighter* highlighter() const { return open.highlighter.get(); }

    // --- escape hatches ---------------------------------------------------
    //
    // A host doing something this class did not anticipate reaches through
    // these rather than being stuck. All three are the same objects the view is
    // drawing, so a change through one shows up on the next frame — ask for a
    // repaint() after anything the view did not do itself.

    Editor& editor() { return open.file.editor(); }
    const Editor& editor() const { return open.file.editor(); }

    TextFile& file() { return open.file; }
    const TextFile& file() const { return open.file; }

    EditorWidget& editorWidget() { return widget; }

    // The text changed, from a keystroke or from a call. What a host hangs a
    // dirty marker or a live preview off.
    std::function<void()> onTextChanged = [] {};

private:
    // Rasterizes the atlas, and rebuilds it when the font or the display's
    // scale has moved since the last frame. Null until the view is on a
    // display, which the editor widget tolerates by drawing nothing.
    void ensureRenderer();

    // Sizes the sprite renderer, the glyph batch and the widget tree to the
    // drawable as it stands.
    //
    // Called from render() as well as from resized(), and that is not belt and
    // braces: setBounds hands the new frame to the platform view, and the
    // resized() that follows arrives on the platform's own schedule — an
    // off-screen view rendered straight after being sized has not had one at
    // all, and draws a correct background with no text on it. render() is the
    // one place that knows the size the frame is actually being drawn at, so it
    // is the one place that can be sure.
    void ensureSurface();

    // Hands the widget a renderer built against the atlas and theme in force,
    // putting the view back on the line and column it was showing.
    //
    // The pair has to be carried across by hand because the scroll offset is in
    // points, and both the row height and the column width it was measured
    // against have just moved — holding the offset fixed slides the file under
    // whoever is reading it. See EditorWidget::topVisibleLine.
    void rebuildRenderer();

    // Points the editor's edit stream at whichever highlighter is installed.
    // Re-run whenever that changes, because both callbacks capture it.
    void connectHighlighter();

    void resized() override;
    void backingScaleChanged() override;
    void render(eacp::GPU::Frame& frame) override;

    void mouseDown(const eacp::Graphics::MouseEvent& event) override;
    void mouseDragged(const eacp::Graphics::MouseEvent& event) override;
    void mouseUp(const eacp::Graphics::MouseEvent& event) override;
    void mouseMoved(const eacp::Graphics::MouseEvent& event) override;
    void mouseExited(const eacp::Graphics::MouseEvent&) override;
    void mouseWheel(const eacp::Graphics::MouseEvent& event) override;
    void keyDown(const eacp::Graphics::KeyEvent& event) override;

    // The file, its highlighter and its scroll offset. One rather than a
    // Workspace: this view shows exactly one document, and the tab model that
    // decides which of several is active belongs to the workbench.
    OpenFile open;

    // Declared after `open`, which it holds a reference to.
    EditorWidget widget {open};

    WidgetHost host;

    TextTheme textTheme;
    FontSettings fontSettings;

    // What the atlas was actually built for, so a frame can tell whether either
    // has moved since. Compared whole: an atlas answers for one family at one
    // size and the two are only right together.
    FontSettings builtFont;
    float builtAtScale = 1.f;

    // What the sprite renderer was sized for. Two floats rather than a Rect
    // because Rect has no equality operator, and rebuilding a SpriteRenderer
    // every frame is not free — it bakes its logical size at construction.
    float surfaceWidth = 0.f;
    float surfaceHeight = 0.f;

    std::optional<eacp::Sprites::SpriteRenderer> sprites;
    eacp::OwningPointer<eacp::Text::GlyphAtlas> atlas;
    std::optional<eacp::Text::GlyphRenderer> glyphs;
    std::optional<TextRenderer> renderer;

    // Two ticks a second, so the caret is on for half of each. The widget asks
    // for a frame only when the phase actually flips, and shows no caret at all
    // while unfocused — so an editor nobody is typing in costs nothing.
    eacp::Threads::Timer blink {[this] { widget.tickCaretBlink(); }, 2};
};
} // namespace ecode
