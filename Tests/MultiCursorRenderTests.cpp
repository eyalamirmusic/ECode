#include <ECodeUI/EditorWidget.h>
#include <ECodeUI/WidgetHost.h>

#include <NanoTest/NanoTest.h>

#include <eacp/GPU/GPU.h>
#include <eacp/Text/Text.h>

#include <algorithm>
#include <optional>

// More than one cursor, drawn: a real EditorWidget in a real WidgetHost, driven
// by synthesized ⌥-clicks and keys, rendered off-screen and read back.
//
// The model tests say where the cursors are. They cannot say whether the
// renderer draws all of them, and that is the half a person notices first — a
// multi-cursor edit whose extra carets are invisible looks like the keystroke
// going somewhere at random. The current-line band is the other half, and it is
// a case no model test can reach: it is correct per cursor and wrong for the set
// only because two carets on one line paint it twice.

using namespace nano;
using namespace eacp;
using namespace ecode;

namespace
{
constexpr auto viewWidth = 600.f;
constexpr auto viewHeight = 300.f;

// Short lines, so everything to the right of the text is bare background and a
// caret placed at a line's end has nothing beside it to be confused with.
constexpr auto sampleText = "ab\nab\nab\nab\n";

struct MultiCursorTestView final : GPU::GPUView
{
    MultiCursorTestView()
    {
        setSampleCount(1);
        setBounds({0.f, 0.f, viewWidth, viewHeight});

        open.file.editor().setDocument(Document::fromText(sampleText));

        root.addChild(editor);
        host.setRoot(root);
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

        const auto area = Graphics::Rect {0.f, 0.f, viewWidth, viewHeight};

        root.setBounds(area);
        editor.setBounds(area);

        // Focus is what lights the caret, and an unfocused editor deliberately
        // shows none at all.
        host.setFocus(&editor);

        return true;
    }

    Editor& model() { return open.file.editor(); }

    void render(GPU::Frame& frame) override
    {
        auto pass = frame.beginPass({textTheme.background});

        if (!atlas || !glyphs || !renderer)
            return;

        auto sprites =
            Sprites::SpriteRenderer {{viewWidth, viewHeight}, sampleCount()};

        host.prepare(*atlas);
        atlas->commit();

        auto context = PaintContext {
            pass, sprites, *glyphs, *atlas, {0.f, 0.f, viewWidth, viewHeight}, 1.f};

        host.paint(context);
    }

    void clickAt(const Graphics::Point& point, bool withAlt)
    {
        auto event = Graphics::MouseEvent {};

        event.pos = point;
        event.clickCount = 1;
        event.modifiers.alt = withAlt;

        host.mouseDown(event);
        host.mouseUp(event);
    }

    void press(std::uint16_t code)
    {
        auto event = Graphics::KeyEvent {};
        event.keyCode = code;

        host.keyDown(event);
    }

    float leftOfText() const
    {
        return renderer->gutterWidth(open.file.document().lineCount()) + 8.f;
    }

    // Where a column on a line lands on screen. Through the renderer's own
    // columnToX rather than assumed, for the reason FindRenderTests records:
    // a band placed by arithmetic that disagrees with the renderer's reads the
    // background and reports the thing missing.
    Graphics::Point pointAt(std::size_t line, std::size_t column) const
    {
        const auto text = open.file.document().line(line);

        return {leftOfText() + renderer->columnToX(text, column),
                (static_cast<float>(line) + 0.5f) * renderer->rowHeight()};
    }

    // A narrow strip where a caret parked at the end of a line sits. It reaches
    // back over the last glyph's edge, which is deliberate — see bluestIn for
    // why that is safe and what happens to a test that assumes it is not there.
    Graphics::Rect caretBandAt(std::size_t line) const
    {
        const auto at = pointAt(line, open.file.document().line(line).size());
        const auto height = renderer->rowHeight();

        return {
            at.x - 1.f, static_cast<float>(line) * height + 2.f, 4.f, height - 4.f};
    }

    // The far right of a row, past every glyph and every caret: nothing lives
    // there but the current-line band, so it answers about the band alone.
    // §9's rule — when a test samples an area, check nothing else bright is in
    // it, or it will answer about the wrong thing.
    Graphics::Rect emptyEndOfRow(std::size_t line) const
    {
        const auto height = renderer->rowHeight();

        return {viewWidth - 60.f,
                static_cast<float>(line) * height + 2.f,
                40.f,
                height - 4.f};
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

// How blue a region gets, measured against its own red so that brightness
// alone cannot answer.
//
// The caret is (0.55, 0.78, 0.98) and the text is (0.85, 0.87, 0.91). In blue
// alone that is 0.98 against 0.91, and a half-covered antialiased glyph edge
// closes the gap entirely — the first version of these tests thresholded on
// blue and reported a caret on every line that had a "b" on it. Blue minus red
// is 0.43 against 0.06, with the page at 0.03 and the current-line band over it
// nearer still. That needs no clearance around the caret to work.
float bluestIn(const Graphics::Image& image, const Graphics::Rect& area)
{
    auto peak = 0.f;

    const auto x1 = std::min(static_cast<int>(area.right()), image.width());
    const auto y1 = std::min(static_cast<int>(area.bottom()), image.height());

    for (auto y = std::max(0, static_cast<int>(area.y)); y < y1; ++y)
    {
        for (auto x = std::max(0, static_cast<int>(area.x)); x < x1; ++x)
        {
            const auto pixel = image.at(x, y);

            peak = std::max(peak, pixel.b - pixel.r);
        }
    }

    return peak;
}

float brightestIn(const Graphics::Image& image, const Graphics::Rect& area)
{
    auto peak = 0.f;

    const auto x1 = std::min(static_cast<int>(area.right()), image.width());
    const auto y1 = std::min(static_cast<int>(area.bottom()), image.height());

    for (auto y = std::max(0, static_cast<int>(area.y)); y < y1; ++y)
        for (auto x = std::max(0, static_cast<int>(area.x)); x < x1; ++x)
            peak = std::max(peak, image.at(x, y).b);

    return peak;
}

float averageBlueIn(const Graphics::Image& image, const Graphics::Rect& area)
{
    auto total = 0.f;
    auto count = 0;

    const auto x1 = std::min(static_cast<int>(area.right()), image.width());
    const auto y1 = std::min(static_cast<int>(area.bottom()), image.height());

    for (auto y = std::max(0, static_cast<int>(area.y)); y < y1; ++y)
    {
        for (auto x = std::max(0, static_cast<int>(area.x)); x < x1; ++x)
        {
            total += image.at(x, y).b;
            ++count;
        }
    }

    return count == 0 ? 0.f : total / static_cast<float>(count);
}

// Halfway between the glyphs' 0.06 and the caret's 0.43, and nowhere near
// either — a margin quantisation cannot supply and a partly-covered caret pixel
// still clears, since a two-point-wide caret always covers at least one pixel
// whole.
bool hasCaret(const Graphics::Image& image, const Graphics::Rect& band)
{
    return bluestIn(image, band) > 0.2f;
}
} // namespace

// Three cursors draw three carets. The two added rows are the test: a renderer
// handed only the primary draws exactly the same first row, so a picture of row
// zero says nothing at all. Line 3 never holds one and is the control.
auto tEveryCursorDrawsACaret =
    test("MultiCursorRender/everyCursorDrawsItsOwnCaret") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = MultiCursorTestView {};

    if (!view.build())
        return;

    // One caret at the end of line 0, and nothing on lines 1 and 2 yet.
    view.model().placeCaret(2);

    const auto one = view.renderToImage(1.f);
    check(one.isValid());

    check(hasCaret(one, view.caretBandAt(0)));
    check(!hasCaret(one, view.caretBandAt(1)));
    check(!hasCaret(one, view.caretBandAt(2)));

    view.model().addCursorBelow();
    view.model().addCursorBelow();

    const auto three = view.renderToImage(1.f);
    check(three.isValid());

    check(hasCaret(three, view.caretBandAt(0)));
    check(hasCaret(three, view.caretBandAt(1)));
    check(hasCaret(three, view.caretBandAt(2)));
    check(!hasCaret(three, view.caretBandAt(3)));
};

// The fold this whole section exists for. The band is per *line*, not per
// caret, and two carets on one line have to leave it looking exactly as one
// does — at 3.5% white a second pass over the same rectangle is a visible step
// in brightness, and it appears only in the arrangement no per-cursor test can
// produce.
//
// Sampled past the end of the text so the extra caret itself is not in the
// region: a test that included it would pass on the caret's own ink whether or
// not the band doubled.
auto tTwoCaretsOnALineDrawOneBand =
    test("MultiCursorRender/twoCaretsOnOneLineDoNotDoubleItsHighlight") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = MultiCursorTestView {};

    if (!view.build())
        return;

    view.model().placeCaret(0); // line 0, column 0

    const auto single = view.renderToImage(1.f);
    check(single.isValid());

    const auto band = view.emptyEndOfRow(0);
    const auto lit = averageBlueIn(single, band);

    // The band is lit at all — otherwise the comparison below is between two
    // unlit strips and holds for the wrong reason.
    check(lit > averageBlueIn(single, view.emptyEndOfRow(1)) + 0.005f);

    // A second caret on the same line, two columns along.
    view.model().toggleCursorAt(2);

    check(view.model().cursors().count() == 2);
    check(view.model().document().lineAt(view.model().cursors()[1].head) == 0);

    const auto doubled = view.renderToImage(1.f);
    check(doubled.isValid());

    // Identical, not merely similar. The band is drawn from the same colour
    // into the same rectangle, so anything but equality within one 8-bit step
    // means it was painted more than once.
    check(std::abs(averageBlueIn(doubled, band) - lit) < 0.005f);
};

// Every selection is painted, not just the primary's. Two occurrences on two
// different lines, so a renderer drawing one of them leaves the other line
// reading as bare page.
auto tEverySelectionIsPainted =
    test("MultiCursorRender/everySelectionIsPainted") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = MultiCursorTestView {};

    if (!view.build())
        return;

    const auto wordOnRow = [&](std::size_t line)
    {
        const auto height = view.renderer->rowHeight();
        const auto text = view.open.file.document().line(line);

        return Graphics::Rect {view.leftOfText(),
                               static_cast<float>(line) * height + 2.f,
                               view.renderer->columnToX(text, text.size()),
                               height - 4.f};
    };

    view.model().placeCaret(0);

    const auto plain = averageBlueIn(view.renderToImage(1.f), wordOnRow(2));

    view.model().selectNextOccurrence(); // selects "ab" on line 0
    view.model().selectAllOccurrences(); // and the one on every other line

    check(view.model().cursors().count() == 4);

    const auto image = view.renderToImage(1.f);

    check(image.isValid());

    // Line 2 is neither the primary's line nor the first match's, so it is lit
    // only if the renderer walked the whole set.
    check(averageBlueIn(image, wordOnRow(2)) > plain + 0.05f);
};

// ⌥-click, through the widget rather than through the model: the modifier has
// to survive the trip from the mouse event into the editor, and nothing else in
// the app had ever read MouseEvent::modifiers.alt on a click.
auto tAltClickAddsAVisibleCaret =
    test("MultiCursorRender/altClickingAddsACaretThatIsDrawn") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = MultiCursorTestView {};

    if (!view.build())
        return;

    view.clickAt(view.pointAt(0, 2), false);

    check(view.model().cursors().count() == 1);

    view.clickAt(view.pointAt(2, 2), true);

    check(view.model().cursors().count() == 2);

    const auto added = view.renderToImage(1.f);
    check(added.isValid());

    check(hasCaret(added, view.caretBandAt(0)));
    check(hasCaret(added, view.caretBandAt(2)));

    // And Escape takes it away again, which is the only way back to one cursor
    // from the keyboard.
    view.press(Graphics::KeyCode::Escape);

    check(view.model().cursors().count() == 1);

    const auto collapsed = view.renderToImage(1.f);
    check(collapsed.isValid());

    // The ⌥-clicked one is the primary, so it is the one that survives.
    check(!hasCaret(collapsed, view.caretBandAt(0)));
    check(hasCaret(collapsed, view.caretBandAt(2)));
};

// A plain click after ⌥-clicking is back to one caret. The pair matters: a
// widget that added a cursor on every click would pass the test above and make
// the editor unusable.
auto tAPlainClickClearsTheExtraCarets =
    test("MultiCursorRender/aPlainClickGoesBackToOneCaret") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = MultiCursorTestView {};

    if (!view.build())
        return;

    view.clickAt(view.pointAt(0, 2), false);
    view.clickAt(view.pointAt(2, 2), true);

    check(view.model().cursors().count() == 2);

    view.clickAt(view.pointAt(1, 2), false);

    check(view.model().cursors().count() == 1);

    const auto image = view.renderToImage(1.f);
    check(image.isValid());

    check(hasCaret(image, view.caretBandAt(1)));
    check(!hasCaret(image, view.caretBandAt(0)));
    check(!hasCaret(image, view.caretBandAt(2)));
};

// Typing at three carets puts the text in three places and draws all three.
// The end of the whole path, and the one thing that would be obvious to a
// person and invisible to every test that stops at the model.
auto tTypingShowsAtEveryCaret =
    test("MultiCursorRender/typingAtThreeCaretsChangesThreeLines") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = MultiCursorTestView {};

    if (!view.build())
        return;

    view.model().placeCaret(2);
    view.model().addCursorBelow();
    view.model().addCursorBelow();

    auto event = Graphics::KeyEvent {};
    event.characters = "X";
    event.charactersIgnoringModifiers = "X";

    view.host.keyDown(event);

    check(view.open.file.document().text() == "abX\nabX\nabX\nab\n");

    const auto image = view.renderToImage(1.f);

    check(image.isValid());

    // Row 3 was not typed into, so it is the control: every other row now has
    // ink where it does not.
    const auto height = view.renderer->rowHeight();
    const auto text = view.open.file.document().line(0);

    const auto columnOfX = Graphics::Rect {
        view.leftOfText() + view.renderer->columnToX(text, 2), 0.f, 10.f, height};

    const auto inkAt = [&](std::size_t line)
    {
        auto band = columnOfX;
        band.y = static_cast<float>(line) * height + 2.f;
        band.h = height - 4.f;

        return brightestIn(image, band);
    };

    check(inkAt(0) > inkAt(3) + 0.2f);
    check(inkAt(1) > inkAt(3) + 0.2f);
    check(inkAt(2) > inkAt(3) + 0.2f);
};
