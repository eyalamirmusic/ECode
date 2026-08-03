#include "Common.h"

#include <ECodeRender/TextRenderer.h>
#include <ECodeCore/Cursor.h>
#include <ECodeCore/LineMap.h>

// Soft wrap, drawn.
//
// LineMapTests already pin which text lands on which row; nothing there can say
// whether any of it reaches the screen, or reaches it in the right place. That
// is the same gap PLAN.md §9 keeps recording — two correct halves composing
// into a wrong whole — and the answer it settled on is to render the thing
// off-screen and read the pixels back rather than to launch a window.

using namespace nano;
using namespace eacp;
using namespace ecode;

namespace
{
constexpr auto viewWidth = 320.f;
constexpr auto viewHeight = 200.f;

// One line, far too long for the view, made of short words so word wrapping has
// somewhere to break it.
constexpr auto longLine =
    "one two three four five six seven eight nine ten eleven twelve thirteen";

struct WrapTestView final : GPU::GPUView
{
    WrapTestView()
    {
        setSampleCount(1);
        setBounds({0.f, 0.f, viewWidth, viewHeight});
    }

    bool build()
    {
        auto request = Text::FontRequest {};
        request.family = "Menlo";
        request.pointSize = 13.f;
        request.scale = 1.f;

        if (!Text::GlyphRasterizer {request}.isValid())
            return false;

        atlas = makeOwned<Text::GlyphAtlas>(Text::rasterizerFaceFactory(),
                                            request,
                                            512,
                                            2048);

        renderer.emplace(*atlas, theme, 1.f);
        glyphs.emplace();
        glyphs->setViewportSize({viewWidth, viewHeight});

        return true;
    }

    // The width the editor itself would wrap at, so these tests exercise the
    // same arithmetic the app does rather than a number picked here.
    void wrapToView()
    {
        lines.setWrapColumns(
            document, renderer->wrapColumnsFor(bounds(), document.lineCount()));
    }

    Graphics::Rect bounds() const { return {0.f, 0.f, viewWidth, viewHeight}; }

    DocumentView documentView() const { return {document, lines, nullptr}; }

    void render(GPU::Frame& frame) override
    {
        auto pass = frame.beginPass({theme.background});

        if (!renderer || !atlas || !glyphs)
            return;

        const auto view = documentView();

        renderer->prepare(view, bounds(), 0.f);
        atlas->commit();

        auto sprites =
            Sprites::SpriteRenderer {{viewWidth, viewHeight}, sampleCount()};

        auto context = PaintContext {pass, sprites, *glyphs, *atlas, bounds(), 1.f};

        auto overlay = EditorOverlay {};
        overlay.cursors = showCursors ? &cursors : nullptr;
        overlay.caretVisible = showCursors;

        renderer->draw(context, view, overlay, bounds(), {});
    }

    // Shows exactly this cursor, with its caret lit. Every test here wants one
    // or none, so the set is built for them.
    void setCursor(Cursor caret)
    {
        cursors.reset(caret);
        showCursors = true;
    }

    void hideCursors() { showCursors = false; }

    TextTheme theme;
    Document document;
    LineMap lines;

    CursorSet cursors;
    bool showCursors = false;

    OwningPointer<Text::GlyphAtlas> atlas;
    std::optional<TextRenderer> renderer;
    std::optional<Text::GlyphRenderer> glyphs;
};

int inkIn(const Graphics::Image& image, const Graphics::Rect& area)
{
    auto total = 0;

    const auto x1 = std::min(static_cast<int>(area.right()), image.width());
    const auto y1 = std::min(static_cast<int>(area.bottom()), image.height());

    for (auto y = std::max(0, static_cast<int>(area.y)); y < y1; ++y)
        for (auto x = std::max(0, static_cast<int>(area.x)); x < x1; ++x)
            if (image.at(x, y).r > 0.3f || image.at(x, y).g > 0.3f)
                ++total;

    return total;
}

// The whole strip a row occupies, gutter included.
Graphics::Rect rowBand(const TextRenderer& renderer, std::size_t row)
{
    return {0.f, renderer.rowTop(row) + 1.f, viewWidth, renderer.rowHeight() - 2.f};
}

Graphics::Rect gutterBand(const TextRenderer& renderer,
                          const Document& document,
                          std::size_t row)
{
    auto band = rowBand(renderer, row);
    band.w = renderer.gutterWidth(document.lineCount());

    return band;
}
} // namespace

// The claim at its plainest: a line too long for the view puts nothing on the
// second row until wrapping is turned on, and then it does.
auto tWrappingFillsTheRowsBelow =
    test("WrapRender/aWrappedLineDrawsOnSeveralRows") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto straight = WrapTestView {};
    auto wrapped = WrapTestView {};

    if (!straight.build() || !wrapped.build())
        return;

    straight.document = Document::fromText(longLine);
    wrapped.document = Document::fromText(longLine);
    wrapped.wrapToView();

    check(wrapped.lines.rowCount(wrapped.document) > 2);

    const auto straightImage = straight.renderToImage(1.f);
    const auto wrappedImage = wrapped.renderToImage(1.f);

    check(straightImage.isValid() && wrappedImage.isValid());

    // Row 0 has text either way. The second row is where they differ: without
    // wrapping there is no second line and nothing to draw on it.
    const auto band = rowBand(*straight.renderer, 1);

    check(inkIn(straightImage, band) == 0);
    check(inkIn(wrappedImage, band) > 0);
};

// A wrapped line is one line, so the gutter numbers it once. Repeating the
// number down the continuation rows is the single most obvious way to make
// wrapping look broken, and it is what falls out of a gutter loop that still
// thinks in lines.
auto tContinuationRowsHaveNoNumber =
    test("WrapRender/onlyTheFirstRowOfALineIsNumbered") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = WrapTestView {};

    if (!view.build())
        return;

    view.document = Document::fromText(longLine);
    view.wrapToView();

    const auto image = view.renderToImage(1.f);
    check(image.isValid());

    check(inkIn(image, gutterBand(*view.renderer, view.document, 0)) > 0);
    check(inkIn(image, gutterBand(*view.renderer, view.document, 1)) == 0);
    check(inkIn(image, gutterBand(*view.renderer, view.document, 2)) == 0);
};

// Nothing may cross the right edge. A renderer that wrapped the *model* and
// went on drawing each line as one run would pass every LineMap test and still
// print the tail of every line over the top of its own beginning.
auto tNothingOverflowsTheView = test("WrapRender/wrappedTextStaysInsideTheView") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = WrapTestView {};

    if (!view.build())
        return;

    view.document = Document::fromText(longLine);
    view.wrapToView();

    const auto image = view.renderToImage(1.f);
    check(image.isValid());

    // The column the wrap width promised nothing would reach, and everything to
    // the right of it.
    const auto rightOfWrap = view.renderer->gutterWidth(1) + 8.f
                             + static_cast<float>(view.lines.wrapColumns())
                                   * view.renderer->columnToX("m", 1);

    check(rightOfWrap < viewWidth);
    check(inkIn(image, {rightOfWrap, 0.f, viewWidth - rightOfWrap, viewHeight})
          == 0);
};

// The caret is placed from the row map too, and on a continuation row it has to
// come out at the left margin rather than at the column its offset would give
// on the logical line.
auto tCaretLandsOnItsRow = test("WrapRender/theCaretIsDrawnOnItsOwnRow") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = WrapTestView {};

    if (!view.build())
        return;

    view.document = Document::fromText(longLine);
    view.wrapToView();

    // The first character of the second row.
    const auto second = view.lines.row(view.document, 1);

    auto cursor = Cursor {};
    cursor.moveTo(view.document.offsetAt(second.line, second.start));

    view.setCursor(cursor);

    const auto image = view.renderToImage(1.f);
    check(image.isValid());

    // A narrow column at the left of the text area, on row 1 and on row 0. The
    // caret is on row 1; row 0's left column holds only the "o" of "one", so
    // comparing the two says the caret is where the row map put it rather than
    // wherever the glyphs happen to be.
    const auto left = view.renderer->gutterWidth(view.document.lineCount()) + 8.f;

    const auto onRow = Graphics::Rect {left - 1.f,
                                       view.renderer->rowTop(1) + 1.f,
                                       3.f,
                                       view.renderer->rowHeight() - 2.f};

    auto without = WrapTestView {};

    if (!without.build())
        return;

    without.document = Document::fromText(longLine);
    without.wrapToView();

    const auto plain = without.renderToImage(1.f);

    check(inkIn(image, onRow) > inkIn(plain, onRow));
};

// A selection spanning a wrap point is two bands, and the row above it has to
// be filled to where its own text ends rather than to where the selection does.
auto tSelectionSpansRows = test("WrapRender/aSelectionAcrossAWrapFillsBothRows") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = WrapTestView {};

    if (!view.build())
        return;

    view.document = Document::fromText(longLine);
    view.wrapToView();

    const auto second = view.lines.row(view.document, 1);
    const auto start = view.document.offsetAt(second.line, second.start);

    auto cursor = Cursor {};
    cursor.anchor = start > 4 ? start - 4 : 0;
    cursor.head = start + 4;

    view.setCursor(cursor);

    const auto selected = view.renderToImage(1.f);

    view.hideCursors();
    const auto plain = view.renderToImage(1.f);

    check(selected.isValid() && plain.isValid());

    // The selection is drawn under the text, so it shows as the background
    // going from the panel's colour to the selection's. Compared between two
    // renders of the same text rather than against a threshold, since the
    // glyphs on top are identical in both.
    const auto differs = [&](const Graphics::Rect& area)
    {
        auto changed = 0;

        for (auto y = static_cast<int>(area.y); y < static_cast<int>(area.bottom());
             ++y)
            for (auto x = static_cast<int>(area.x);
                 x < static_cast<int>(area.right());
                 ++x)
                if (std::abs(selected.at(x, y).b - plain.at(x, y).b) > 0.05f)
                    ++changed;

        return changed;
    };

    const auto left = view.renderer->gutterWidth(view.document.lineCount()) + 8.f;
    const auto width = 60.f;

    // The end of row 0 and the start of row 1, which is where the two halves of
    // the selection are.
    check(differs({viewWidth - width - 8.f,
                   view.renderer->rowTop(0) + 1.f,
                   width,
                   view.renderer->rowHeight() - 2.f})
          > 0);

    check(differs({left,
                   view.renderer->rowTop(1) + 1.f,
                   width,
                   view.renderer->rowHeight() - 2.f})
          > 0);

    // And row 2 is past the selection, so it is untouched.
    check(differs({left,
                   view.renderer->rowTop(2) + 1.f,
                   width,
                   view.renderer->rowHeight() - 2.f})
          == 0);
};
