#include <ECodeUI/EditorGroupView.h>
#include <ECodeUI/WidgetHost.h>
#include <ECodeCore/EditorGroups.h>
#include <ECodeRender/FontSettings.h>
#include <ECodeSyntax/SyntaxHighlighter.h>

#include <NanoTest/NanoTest.h>

#include <eacp/GPU/GPU.h>
#include <eacp/Text/Text.h>

#include <cmath>
#include <optional>
#include <string>

// The editor's font size, from the drawing side.
//
// FontSettingsTests covers the arithmetic. What it cannot see is the reason
// there are two atlases at all: the document is drawn at whatever size ⌘+ has
// left it at and the chrome is not, and a GlyphAtlas is one face at one size, so
// the two are different objects and every glyph batch has to be flushed against
// the one it was queued from. Get that wrong and the text is drawn from whatever
// texels sit at those coordinates in the other atlas — a screenful of the wrong
// letters, which no CPU-side check can tell from the right ones.

using namespace nano;
using namespace eacp;
using namespace ecode;

namespace
{
constexpr auto viewWidth = 500.f;
constexpr auto viewHeight = 300.f;

// The chrome font, which is what the window's own atlas is built at and what
// nothing here changes.
constexpr auto chromeSize = 13.f;

// One editor group — a tab strip drawn from the chrome's atlas over a document
// drawn from its own — which is the smallest arrangement in which the two sizes
// are visible at once.
struct FontTestView final : GPU::GPUView
{
    FontTestView()
    {
        setSampleCount(1);
        setBounds({0.f, 0.f, viewWidth, viewHeight});

        root.setBounds({0.f, 0.f, viewWidth, viewHeight});
        root.addChild(group);
        group.setBounds({0.f, 0.f, viewWidth, viewHeight});

        host.setRoot(root);
    }

    // `oneAtlasForBoth` draws the document through the chrome's atlas, which is
    // what the window did before the font was settable. It is the control for
    // the split case rather than a mode anything uses.
    bool build(float editorSize, bool oneAtlasForBoth)
    {
        auto chrome = FontSettings {};
        chrome.pointSize = chromeSize;

        uiAtlas = makeGlyphAtlas(chrome, 1.f);

        if (!uiAtlas)
            return false;

        if (!oneAtlasForBoth && !buildEditorAtlas(editorSize))
            return false;

        glyphs.emplace();
        glyphs->setViewportSize({viewWidth, viewHeight});

        group.setAtlas(&documentAtlas(), textTheme, 1.f);
        group.refresh();

        return true;
    }

    bool buildEditorAtlas(float pointSize)
    {
        auto font = FontSettings {};
        font.pointSize = pointSize;

        auto built = makeGlyphAtlas(font, 1.f);

        if (!built)
            return false;

        editorAtlas = std::move(built);

        return true;
    }

    // The whole of what Main.cpp does when the size changes: a new atlas, and
    // every pane handed a renderer built from it.
    bool setEditorFontSize(float pointSize)
    {
        if (!buildEditorAtlas(pointSize))
            return false;

        group.setAtlas(editorAtlas.get(), textTheme, 1.f);

        return true;
    }

    // The window as it was before the size was settable: one atlas for the
    // chrome and the document both, so changing the size changes everything.
    // The control the test below needs, and nothing uses it otherwise.
    bool useOneAtlasAt(float pointSize)
    {
        auto font = FontSettings {};
        font.pointSize = pointSize;

        auto built = makeGlyphAtlas(font, 1.f);

        if (!built)
            return false;

        // Let the panes go of the old atlas before it is destroyed, which is
        // the same order Main.cpp's rebuild is careful about.
        group.setAtlas(nullptr, textTheme, 1.f);

        uiAtlas = std::move(built);
        editorAtlas = {};

        group.setAtlas(uiAtlas.get(), textTheme, 1.f);

        return true;
    }

    Text::GlyphAtlas& documentAtlas()
    {
        return editorAtlas ? *editorAtlas : *uiAtlas;
    }

    void setText(std::string_view text)
    {
        groups.editor().setDocument(Document::fromText(std::string {text}));
        group.refresh();
    }

    void render(GPU::Frame& frame) override
    {
        auto pass = frame.beginPass({textTheme.background});

        if (!uiAtlas || !glyphs)
            return;

        host.prepare(*uiAtlas);

        uiAtlas->commit();

        if (editorAtlas)
            editorAtlas->commit();

        auto sprites =
            Sprites::SpriteRenderer {{viewWidth, viewHeight}, sampleCount()};

        auto context = PaintContext {pass,
                                     sprites,
                                     *glyphs,
                                     *uiAtlas,
                                     {0.f, 0.f, viewWidth, viewHeight},
                                     1.f};

        host.paint(context);
    }

    // Two frames: the first lays out the visible rows, the second settles the
    // highlighter's spans. What anything here looks at is the third.
    Graphics::Image settledImage()
    {
        renderToImage(1.f);
        renderToImage(1.f);

        return renderToImage(1.f);
    }

    float rowHeight() const { return group.textRenderer()->rowHeight(); }

    TextTheme textTheme;
    ChromeTheme theme;

    EditorGroups groups {[]
                         {
                             auto syntax = makeOwned<SyntaxHighlighter>();

                             if (!syntax->isValid())
                                 return OwningPointer<Highlighter> {};

                             return OwningPointer<Highlighter> {std::move(syntax)};
                         }};

    Widget root;
    EditorGroupView group {theme, groups.active()};

    WidgetHost host;

    OwningPointer<Text::GlyphAtlas> uiAtlas;
    OwningPointer<Text::GlyphAtlas> editorAtlas;
    std::optional<Text::GlyphRenderer> glyphs;
};

bool near(float a, float b)
{
    return std::abs(a - b) < 0.02f;
}

int differencesIn(const Graphics::Image& a,
                  const Graphics::Image& b,
                  const Graphics::Rect& area)
{
    auto total = 0;

    for (auto y = static_cast<int>(area.y); y < static_cast<int>(area.bottom()); ++y)
        for (auto x = static_cast<int>(area.x); x < static_cast<int>(area.right());
             ++x)
        {
            const auto left = a.at(x, y);
            const auto right = b.at(x, y);

            if (!near(left.r, right.r) || !near(left.g, right.g)
                || !near(left.b, right.b))
                ++total;
        }

    return total;
}

Graphics::Rect tabStrip()
{
    return {0.f, 0.f, viewWidth, EditorGroupView::tabBarHeight};
}

Graphics::Rect textArea()
{
    return {0.f,
            EditorGroupView::tabBarHeight,
            viewWidth,
            viewHeight - EditorGroupView::tabBarHeight};
}

std::string lines(std::string_view text, int count)
{
    auto out = std::string {};

    for (auto index = 0; index < count; ++index)
    {
        out += text;
        out += '\n';
    }

    return out;
}
} // namespace

// The one that catches a batch flushed against the wrong atlas.
//
// At the same size, a document drawn through its own atlas has to come out
// pixel-for-pixel identical to the same document drawn through the chrome's —
// the glyphs are the same bitmaps at the same destinations, and which texture
// they were packed into is not something anyone can see. Unless the flush names
// the other one, in which case every glyph in the document is drawn from
// whichever letter the chrome happened to pack at those coordinates.
auto tTwoAtlasesDrawWhatOneDoes =
    test("FontRender/aSecondAtlasDrawsWhatOneAtlasDraws") = []
{
    const auto text = lines("int value = 42;", 12);

    auto shared = FontTestView {};

    if (!shared.build(chromeSize, true))
        return;

    shared.setText(text);

    auto split = FontTestView {};

    if (!split.build(chromeSize, false))
        return;

    split.setText(text);

    const auto one = shared.settledImage();
    const auto two = split.settledImage();

    check(differencesIn(one, two, {0.f, 0.f, viewWidth, viewHeight}) == 0);
};

// What the whole two-atlas arrangement is for: ⌘+ enlarges the code and leaves
// the tab strip exactly where it was. One atlas cannot do this — the strip's
// text would grow with the document's and take a slice out of it.
auto tTheEditorFontLeavesTheChromeAlone =
    test("FontRender/aBiggerEditorFontDoesNotMoveTheChrome") = []
{
    const auto text = lines("int value = 42;", 12);

    auto view = FontTestView {};

    if (!view.build(chromeSize, false))
        return;

    view.setText(text);

    const auto before = view.settledImage();
    const auto smallRows = view.rowHeight();

    if (!view.setEditorFontSize(22.f))
        return;

    const auto after = view.settledImage();

    check(view.rowHeight() > smallRows);

    // The strip is drawn from the chrome's atlas, so not one pixel of it moved.
    check(differencesIn(before, after, tabStrip()) == 0);

    // And the document did.
    check(differencesIn(before, after, textArea()) > 0);

    // The control, because "these pixels did not change" is also what a frame
    // that redrew nothing at all would report. One atlas for the whole window
    // is the arrangement this replaced, and there the same step moves the strip
    // as well — so the assertion above is about where the sizes are decided,
    // not about a step that did nothing.
    auto shared = FontTestView {};

    if (!shared.build(chromeSize, true))
        return;

    shared.setText(text);

    const auto sharedBefore = shared.settledImage();

    if (!shared.useOneAtlasAt(22.f))
        return;

    const auto sharedAfter = shared.settledImage();

    check(differencesIn(sharedBefore, sharedAfter, tabStrip()) > 0);
};

// A size change moves the row height, so the scroll offset — which is in points
// — points at a different line afterwards. What someone is reading is the line
// at the top, so that is what survives rather than the number expressing it.
auto tZoomKeepsTheTopLine =
    test("FontRender/changingTheSizeKeepsTheLineAtTheTop") = []
{
    auto view = FontTestView {};

    if (!view.build(chromeSize, false))
        return;

    view.setText(lines("int value = 42;", 400));
    view.settledImage();

    view.group.editor().scrollToTopLine(120);

    check(view.group.editor().topVisibleLine() == 120);

    if (!view.setEditorFontSize(22.f))
        return;

    check(view.group.editor().topVisibleLine() == 120);

    // And back down again, which is the direction that can push the end of the
    // document off the bottom of the viewport if the offset is not re-derived.
    if (!view.setEditorFontSize(8.f))
        return;

    check(view.group.editor().topVisibleLine() == 120);
};

// The other half of that, and the half a test could not have predicted: every
// command wakes the editor, and waking it used to bring the caret into view
// whether or not anything had moved. Zooming with the caret on line 1 and the
// reader on line 120 threw the file back to the top on every press.
auto tWakingWithoutAChangeDoesNotScroll =
    test("FontRender/aCommandThatChangesNothingLeavesTheViewAlone") = []
{
    auto view = FontTestView {};

    if (!view.build(chromeSize, false))
        return;

    view.setText(lines("int value = 42;", 400));
    view.settledImage();

    auto& editor = view.group.editor();

    // One wake to settle. Loading the text bumped the document's revision, and
    // that is a change the view *should* follow — the caret's surroundings are
    // not what they were. What is under test is the wake after it, where
    // nothing whatever has moved.
    editor.wake();

    // Deliberately far from the caret, which is still at the top of the file.
    editor.scrollToTopLine(120);

    const auto offset = editor.scrollOffset();

    editor.wake();

    check(editor.scrollOffset() == offset);

    // But a caret that really did move is still followed, or this would have
    // broken typing off the bottom of the screen instead.
    editor.editor().moveDown(false, 200);
    editor.wake();

    check(editor.scrollOffset() != offset);
};
