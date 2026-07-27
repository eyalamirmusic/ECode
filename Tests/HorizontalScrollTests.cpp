#include <ECodeEditor/EditorWidget.h>
#include <ECodeWidgets/WidgetHost.h>

#include <NanoTest/NanoTest.h>

#include <eacp/GPU/GPU.h>
#include <eacp/Text/Text.h>

#include <cmath>
#include <optional>
#include <string>

// Scrolling across a line that is wider than the window.
//
// Soft wrap is off by default — code is written to a column limit and wrapping
// hides the indentation that says what a line belongs to — so without this the
// right-hand end of a long line cannot be reached at all.
//
// The questions worth aiming at are the ones arithmetic alone cannot answer.
// Whether the text moved and the gutter did not is a picture; so is whether the
// end of a long line ever becomes visible. And the range has to come from the
// widest line rather than from a constant, which only a comparison between two
// documents can tell.

using namespace nano;
using namespace eacp;
using namespace ecode;

namespace
{
constexpr auto viewWidth = 420.f;
constexpr auto viewHeight = 200.f;

// Wide enough that its far end is nowhere near the window, so "did the view
// move" and "is the end visible" are different questions.
std::string longLine()
{
    return std::string(150, ' ') + "MMMM";
}

struct AcrossTestView final : GPU::GPUView
{
    AcrossTestView()
    {
        setSampleCount(1);
        setBounds({0.f, 0.f, viewWidth, viewHeight});

        root.addChild(editor);
        host.setRoot(root);

        root.setBounds({0.f, 0.f, viewWidth, viewHeight});
        editor.setBounds({0.f, 0.f, viewWidth, viewHeight});
    }

    bool build()
    {
        auto request = Text::FontRequest {};
        request.family = "Menlo";
        request.pointSize = 13.f;
        request.scale = 1.f;

        auto rasterizer = makeOwned<Text::GlyphRasterizer>(request);

        if (!rasterizer->isValid())
            return false;

        atlas = makeOwned<Text::GlyphAtlas>(
            OwningPointer<Text::GlyphSource> {std::move(rasterizer)}, 512, 2048);

        renderer.emplace(*atlas, textTheme, 1.f);
        glyphs.emplace();
        glyphs->setViewportSize({viewWidth, viewHeight});

        editor.setRenderer(&renderer.value());

        return true;
    }

    void setText(std::string text)
    {
        open.file.editor().setDocument(Document::fromText(std::move(text)));
    }

    const Document& document() const { return open.file.document(); }

    // A trackpad swipe across, in points. Negative moves the text left, which
    // is the direction that reveals the end of a long line.
    void swipeAcross(float points)
    {
        auto event = Graphics::MouseEvent {};

        event.pos = {viewWidth * 0.5f, viewHeight * 0.5f};
        event.type = Graphics::MouseEventType::Wheel;
        event.preciseScrolling = true;
        event.delta = {points, 0.f};

        editor.mouseWheel(event);
    }

    // Where the text begins, which is also where the gutter ends.
    float gutter() const { return renderer->gutterWidth(document().lineCount()); }

    Graphics::Rect textAreaOfRow(std::size_t row) const
    {
        // Two points clear of the gutter's edge rule, which is drawn in a
        // colour of its own and is not what any of this is asking about.
        return {gutter() + 2.f,
                renderer->rowTop(row),
                viewWidth - gutter() - 2.f,
                renderer->rowHeight()};
    }

    void render(GPU::Frame& frame) override
    {
        auto pass = frame.beginPass({textTheme.background});

        if (!atlas || !glyphs || !renderer)
            return;

        host.prepare(*atlas);
        atlas->commit();

        auto sprites =
            Sprites::SpriteRenderer {{viewWidth, viewHeight}, sampleCount()};

        auto context = PaintContext {
            pass, sprites, *glyphs, *atlas, {0.f, 0.f, viewWidth, viewHeight}, 1.f};

        host.paint(context);
    }

    TextTheme textTheme;

    OpenFile open;

    Widget root;
    EditorWidget editor {open};

    WidgetHost host;

    OwningPointer<Text::GlyphAtlas> atlas;
    std::optional<TextRenderer> renderer;
    std::optional<Text::GlyphRenderer> glyphs;
};

bool near(float a, float b)
{
    return std::abs(a - b) < 0.02f;
}

int inkIn(const Graphics::Image& image,
          const Graphics::Rect& area,
          const Graphics::Color& background)
{
    auto total = 0;

    for (auto y = static_cast<int>(area.y); y < static_cast<int>(area.bottom()); ++y)
    {
        for (auto x = static_cast<int>(area.x); x < static_cast<int>(area.right());
             ++x)
        {
            const auto pixel = image.at(x, y);

            if (!near(pixel.r, background.r) || !near(pixel.g, background.g)
                || !near(pixel.b, background.b))
                ++total;
        }
    }

    return total;
}

int differingIn(const Graphics::Image& a,
                const Graphics::Image& b,
                const Graphics::Rect& area)
{
    auto total = 0;

    for (auto y = static_cast<int>(area.y); y < static_cast<int>(area.bottom()); ++y)
    {
        for (auto x = static_cast<int>(area.x); x < static_cast<int>(area.right());
             ++x)
        {
            const auto one = a.at(x, y);
            const auto two = b.at(x, y);

            if (!near(one.r, two.r) || !near(one.g, two.g) || !near(one.b, two.b))
                ++total;
        }
    }

    return total;
}
} // namespace

// --- the range --------------------------------------------------------------

// The width a scroll clamps against is *measured*, not multiplied out of a byte
// count: a tab is one byte and four columns.
//
// The two documents have the same number of bytes on their widest line and
// differ only in what those bytes are, so a contentWidth that reached for
// widestLine() rather than for the text would report them identical. Chosen for
// that reason — a line of tabs against a line of spaces is the input where the
// two implementations disagree and nothing else does.
auto tTabsMeasureWiderThanBytes =
    test("HorizontalScroll/aTabbedLineReachesFurtherThanItsByteCount") = []
{
    auto spaces = AcrossTestView {};

    if (!spaces.build())
        return;

    spaces.setText(std::string(40, ' ') + "\n");

    const auto plain = spaces.renderer->contentWidth(
        {spaces.document(), spaces.editor.editor().lineMap()});

    spaces.setText(std::string(40, '\t') + "\n");

    const auto tabbed = spaces.renderer->contentWidth(
        {spaces.document(), spaces.editor.editor().lineMap()});

    check(spaces.document().widestLine() == 40);

    // Four columns a tab, so comfortably past twice — a margin no rounding
    // reaches and a plain byte count cannot produce.
    check(tabbed > plain * 2.f);
};

// The range comes from the widest line rather than from anything constant. A
// wider document must scroll further, which is the comparison a fixed range
// cannot survive.
auto tTheRangeFollowsTheWidestLine =
    test("HorizontalScroll/aWiderDocumentScrollsFurther") = []
{
    auto narrow = AcrossTestView {};
    auto wide = AcrossTestView {};

    if (!narrow.build() || !wide.build())
        return;

    narrow.setText(std::string(200, 'x') + "\n");
    wide.setText(std::string(400, 'x') + "\n");

    narrow.swipeAcross(-100000.f);
    wide.swipeAcross(-100000.f);

    check(narrow.editor.scrollOffset().x < 0.f);
    check(wide.editor.scrollOffset().x < narrow.editor.scrollOffset().x);
};

// It stops, rather than running on for as long as the wheel is turned.
auto tScrollingStopsAtTheEnd =
    test("HorizontalScroll/scrollingAcrossStopsAtTheWidestLine") = []
{
    auto view = AcrossTestView {};

    if (!view.build())
        return;

    view.setText(longLine() + "\n");

    view.swipeAcross(-100000.f);

    const auto stopped = view.editor.scrollOffset().x;

    view.swipeAcross(-100000.f);

    check(view.editor.scrollOffset().x == stopped);

    // And the other end is the start of the line, not somewhere past it.
    view.swipeAcross(100000.f);

    check(view.editor.scrollOffset().x == 0.f);
};

// A document that fits has nothing to the right of the window, so the view must
// not be draggable off the text into blank space.
auto tShortLinesDoNotScroll =
    test("HorizontalScroll/aDocumentThatFitsDoesNotScrollAcross") = []
{
    auto view = AcrossTestView {};

    if (!view.build())
        return;

    view.setText("short\nlines only\n");

    view.swipeAcross(-500.f);

    check(view.editor.scrollOffset().x == 0.f);
};

// Wrapping breaks every line to fit, so there is nothing off to the right to
// scroll to. Turning it on while scrolled across has to put the text back:
// otherwise every wrapped row is drawn off the left edge and the window is
// blank, with the offset that did it invisible.
auto tWrappingPinsTheViewLeft =
    test("HorizontalScroll/turningOnWrappingReturnsToTheLeftEdge") = []
{
    auto view = AcrossTestView {};

    if (!view.build())
        return;

    view.setText(longLine() + "\n");

    view.swipeAcross(-400.f);
    check(view.editor.scrollOffset().x < 0.f);

    view.editor.setWordWrap(true);
    check(view.editor.scrollOffset().x == 0.f);

    // And it stays there while wrapping is on.
    view.swipeAcross(-400.f);
    check(view.editor.scrollOffset().x == 0.f);
};

// --- what the mouse and the caret land on -----------------------------------

// A click is placed against the text where it is now drawn, not where it would
// have been drawn unscrolled.
//
// Stated as an offset between two clicks at the *same* point rather than as the
// formula offsetAtPoint already contains: scrolling by exactly forty columns
// has to move what is under the pointer by exactly forty columns. A version
// that ignored the horizontal offset would return the same column twice.
auto tClicksFollowTheScrolledText =
    test("HorizontalScroll/aClickLandsWhereTheTextIsDrawn") = []
{
    auto view = AcrossTestView {};

    if (!view.build())
        return;

    // No tabs: the point of this test is the offset, and a tab would make the
    // columns it is counted in stop being uniform.
    view.setText(std::string(300, 'x') + "\n");

    const auto at =
        Graphics::Point {viewWidth * 0.5f, view.renderer->rowHeight() * 0.5f};

    const auto documentView =
        DocumentView {view.document(), view.editor.editor().lineMap()};

    const auto before = view.renderer->offsetAtPoint(
        documentView, at, view.editor.bounds(), view.editor.scrollOffset());

    const auto columns = 40.f;

    view.swipeAcross(-columns * view.renderer->columnWidth());

    // Nothing clamped it: the line is long enough to take the whole swipe.
    check(
        near(view.editor.scrollOffset().x, -columns * view.renderer->columnWidth()));

    const auto after = view.renderer->offsetAtPoint(
        documentView, at, view.editor.bounds(), view.editor.scrollOffset());

    check(after == before + static_cast<std::size_t>(columns));
};

// Typing on past the right edge has to bring the caret back into view, the same
// way typing past the bottom brings it up. Without it the caret is somewhere off
// to the right and every keystroke lands where nothing can be seen.
auto tTheCaretIsFollowedAcross =
    test("HorizontalScroll/theViewFollowsTheCaretAcross") = []
{
    auto view = AcrossTestView {};

    if (!view.build())
        return;

    view.setText(longLine() + "\n");

    check(view.editor.scrollOffset().x == 0.f);

    view.editor.editor().placeCaret(view.document().line(0).size());
    view.editor.wake();

    check(view.editor.scrollOffset().x < 0.f);
};

// --- the picture ------------------------------------------------------------

// The end of a long line becomes visible, and is not visible before.
//
// The line is blank until its last four characters, so the row draws no ink at
// all until the view has moved across — an assertion that cannot be satisfied
// by the text merely being redrawn somewhere. The count afterwards is what
// keeps the first half honest: two empty rows would satisfy "nothing here"
// twice over (PLAN.md §7).
//
// On the second row, because the first carries the caret's current-line band,
// which fills its row edge to edge whatever the document says.
auto tScrollingRevealsTheEndOfALine =
    test("HorizontalScrollRender/scrollingAcrossRevealsTheEndOfALongLine") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = AcrossTestView {};

    if (!view.build())
        return;

    view.setText("caret sits here\n" + longLine() + "\n");

    const auto before = view.renderToImage(1.f);

    view.swipeAcross(-100000.f);

    const auto after = view.renderToImage(1.f);

    if (!before.isValid() || !after.isValid())
        return;

    check(inkIn(before, view.textAreaOfRow(1), view.textTheme.background) == 0);
    check(inkIn(after, view.textAreaOfRow(1), view.textTheme.background) > 0);
};

// The line numbers name the rows, so they stay put while the text slides under
// them. Byte-identical rather than "roughly", because there is no rounding
// involved in not moving at all.
auto tTheGutterDoesNotScroll =
    test("HorizontalScrollRender/theGutterStaysPutWhileTheTextMoves") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = AcrossTestView {};

    if (!view.build())
        return;

    view.setText("caret sits here\n" + longLine() + "\n");

    const auto before = view.renderToImage(1.f);

    view.swipeAcross(-100000.f);

    const auto after = view.renderToImage(1.f);

    if (!before.isValid() || !after.isValid())
        return;

    const auto numbers = Graphics::Rect {0.f, 0.f, view.gutter() - 1.f, viewHeight};

    check(differingIn(before, after, numbers) == 0);

    // The complementary half: something did move, or the check above is a test
    // of two identical frames.
    check(differingIn(before, after, view.textAreaOfRow(0)) > 0);
};
