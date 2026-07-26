#include "Common.h"

#include <ECodeRender/TextRenderer.h>
#include <ECodeCore/LineMap.h>

// Damage tracking: what a frame is allowed to *not* do.
//
// The thing under test here is an absence, and an absence is invisible to every
// test that only looks at the result — a renderer that lays every row out again
// on every frame draws exactly the same picture as one that reuses them.
// PLAN.md §9: an oracle proves the answer, never the path. So these read the
// cache's own counters as well as the pixels, and the pair is what makes them
// real: the counters say the work was skipped, the pixels say skipping it was
// safe.

using namespace nano;
using namespace eacp;
using namespace ecode;

namespace
{
constexpr auto viewWidth = 420.f;
constexpr auto viewHeight = 200.f;

// Spans that can be changed underneath the renderer, which is what a reparse
// does: the same bytes come back a different colour.
struct StubHighlighter final : Highlighter
{
    const LineStyle& lineStyle(std::size_t line) override
    {
        return line == 0 ? firstLine : plain;
    }

    std::uint64_t version() const override { return revision; }

    void recolour(TokenKind kind)
    {
        firstLine = {{0, 3, kind}};
        ++revision;
    }

    LineStyle firstLine {{0, 3, TokenKind::Keyword}};
    LineStyle plain;
    std::uint64_t revision = 1;
};

Document manyLines(int count)
{
    auto text = std::string {};

    for (auto line = 0; line < count; ++line)
        text += "int value" + std::to_string(line) + " = 0; // a line of code\n";

    return Document::fromText(std::move(text));
}

// Draws one frame the way EditorWidget does: the highlighter first, then the
// glyph prepass, the upload, and the draw.
struct DamageView final : GPU::GPUView
{
    DamageView()
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

        auto rasterizer = makeOwned<Text::GlyphRasterizer>(request);

        if (!rasterizer->isValid())
            return false;

        atlas = makeOwned<Text::GlyphAtlas>(
            OwningPointer<Text::GlyphSource> {std::move(rasterizer)}, 512, 2048);

        renderer.emplace(*atlas, theme, 1.f);
        glyphs.emplace();
        glyphs->setViewportSize({viewWidth, viewHeight});

        return true;
    }

    Graphics::Rect area() const { return {0.f, 0.f, viewWidth, viewHeight}; }

    DocumentView documentView() { return {document, lines, &highlighter}; }

    void render(GPU::Frame& frame) override
    {
        auto pass = frame.beginPass({theme.background});

        if (!renderer || !atlas || !glyphs)
            return;

        auto sprites =
            Sprites::SpriteRenderer {{viewWidth, viewHeight}, sampleCount()};

        const auto view = documentView();

        highlighter.update(document, 0, document.lineCount());

        renderer->prepare(view, area(), scrollY);
        atlas->commit();

        auto context = PaintContext {pass, sprites, *glyphs, *atlas, area(), 1.f};

        renderer->draw(context, view, {}, area(), scrollY);
    }

    const RowCache& rows() const { return renderer->rows(); }

    std::size_t visibleRows()
    {
        return renderer->lastVisibleRow(documentView(), area(), scrollY)
               - renderer->firstVisibleRow(scrollY);
    }

    TextTheme theme;
    Document document = manyLines(200);
    LineMap lines;
    StubHighlighter highlighter;

    float scrollY = 0.f;

    OwningPointer<Text::GlyphAtlas> atlas;
    std::optional<TextRenderer> renderer;
    std::optional<Text::GlyphRenderer> glyphs;
};

// Image::operator== is an exact comparison — identical dimensions, identical
// pixels — which is what these want. Both frames came back through the same
// 8-bit target, so two frames that drew the same thing agree bit for bit, and a
// tolerance would be a licence for the replayed layout to be slightly off.

// Pixels brighter than the background, which for these purposes is the text.
int inkIn(const Graphics::Image& image)
{
    auto total = 0;

    for (auto y = 0; y < image.height(); ++y)
        for (auto x = 0; x < image.width(); ++x)
            if (image.at(x, y).r > 0.3f || image.at(x, y).g > 0.3f)
                ++total;

    return total;
}
} // namespace

// The claim at its plainest. A caret blink, a hover, a scrollbar fading: the
// text has not changed, so nothing about it should be worked out again.
auto tIdleFrameLaysOutNothing = test("Damage/anUnchangedFrameLaysOutNothing") = []
{
    auto view = DamageView {};

    if (!view.build())
        return;

    view.renderToImage(1.f);

    const auto afterFirst = view.rows().layouts();

    check(afterFirst > 0);
    check(afterFirst == view.visibleRows());

    view.renderToImage(1.f);

    check(view.rows().layouts() == afterFirst);
};

// And the other half, without which the first is a licence to draw anything:
// the frame that did no work has to come out identical to the frame that did.
//
// The ink count is not decoration. Equality alone cannot fail for the bug worth
// worrying about — a cache that hands back nothing makes *both* frames blank,
// and two blank frames are equal. So this asks for the text as well: the frame
// that skipped the work still has to have drawn something.
auto tCachedFrameIsIdentical = test("Damage/theReusedFrameDrawsTheSamePixels") = []
{
    auto view = DamageView {};

    if (!view.build())
        return;

    const auto first = view.renderToImage(1.f);
    const auto second = view.renderToImage(1.f);

    check(first.isValid() && second.isValid());

    check(inkIn(first) > 100);
    check(inkIn(second) > 100);

    check(first == second);
};

// Aimed at the expensive direction: a cache that failed to notice an edit shows
// text that is not in the file, which is far worse than being slow.
auto tEditRedraws = test("Damage/anEditLaysTheChangedRowsOutAgain") = []
{
    auto view = DamageView {};

    if (!view.build())
        return;

    const auto before = view.renderToImage(1.f);
    const auto afterFirst = view.rows().layouts();

    view.document.replace(0, 3, "XYZ");

    const auto after = view.renderToImage(1.f);

    check(view.rows().layouts() > afterFirst);
    check(before != after);
};

// The case the document's own revision cannot see: the same bytes, recoloured
// by a parse that finished. Without Highlighter::version in the stamp the row
// keeps the colour it was first drawn in, and nothing else notices.
auto tReparseRedraws = test("Damage/aRecolouringLaysTheRowsOutAgain") = []
{
    auto view = DamageView {};

    if (!view.build())
        return;

    const auto before = view.renderToImage(1.f);

    view.highlighter.recolour(TokenKind::String);

    const auto after = view.renderToImage(1.f);

    check(before.isValid() && after.isValid());

    // The first three characters of line one change colour, and the rest of the
    // window does not.
    check(before != after);
};

// Wrapping breaks the same text into different rows, so every cached row
// describes a strip that is no longer there.
auto tWrapChangeRedraws = test("Damage/aWrapWidthChangeLaysOutAgain") = []
{
    auto view = DamageView {};

    if (!view.build())
        return;

    const auto before = view.renderToImage(1.f);
    const auto afterFirst = view.rows().layouts();

    view.lines.setWrapColumns(view.document, 12);

    const auto after = view.renderToImage(1.f);

    check(view.rows().layouts() > afterFirst);
    check(before != after);
};

// A scroll moves rows rather than changing them, so the rows still on screen
// keep their layout and only the ones that arrived are worked out.
auto tScrollReusesRows = test("Damage/scrollingOnlyLaysOutWhatArrived") = []
{
    auto view = DamageView {};

    if (!view.build())
        return;

    view.renderToImage(1.f);

    const auto onScreen = view.rows().layouts();
    const auto rowHeight = view.renderer->rowHeight();

    // Two rows up, so more than one arrives and the count cannot be confused
    // with an off-by-one.
    view.scrollY = -2.f * rowHeight;

    view.renderToImage(1.f);

    const auto laidOut = view.rows().layouts() - onScreen;

    check(laidOut > 0);
    check(laidOut <= 3);
};

// Scrolling a long file must not accumulate a row per row visited: the cache is
// bounded by the viewport, which is the only reason it can be kept at all.
auto tCacheStaysBounded = test("Damage/theCacheHoldsOnlyWhatIsOnScreen") = []
{
    auto view = DamageView {};

    if (!view.build())
        return;

    const auto rowHeight = view.renderer->rowHeight();

    for (auto screen = 0; screen < 20; ++screen)
    {
        view.scrollY = -static_cast<float>(screen) * 10.f * rowHeight;
        view.renderToImage(1.f);
    }

    check(view.rows().rowsHeld() > 0);
    check(view.rows().rowsHeld() <= view.visibleRows() + 1);
};

// --- the cache on its own --------------------------------------------------
//
// No device needed: the window arithmetic and the stamp are plain logic, and
// the cases that matter are the ones a full-screen draw never reaches.

namespace
{
CachedRow oneGlyph()
{
    auto row = CachedRow {};
    row.text.push_back({{0.f, 0.f, 8.f, 12.f}, {0.f, 0.f, 8.f, 12.f}, {}, false});

    return row;
}

// A cache in the state a frame leaves it in: stamped for what it was drawn
// from, windowed onto the rows on screen, and holding every one of them.
RowCache filled(const RowCacheStamp& stamp, std::size_t first, std::size_t last)
{
    auto cache = RowCache {};

    cache.revalidate(stamp);
    cache.setWindow(first, last);

    for (auto row = first; row < last; ++row)
        cache.store(row, oneGlyph());

    return cache;
}

RowCache filled(std::size_t first, std::size_t last)
{
    return filled({}, first, last);
}
} // namespace

// Every input a row was derived from, one at a time. A stamp missing any one of
// these is a row drawn from something that has since changed, and the failure
// is silent — the picture looks entirely normal, it is just wrong.
auto tStampFields = test("Damage/everyStampFieldDropsTheRows") = []
{
    const auto base = RowCacheStamp {7, 3, 1, 0, 4};

    const auto changes = std::vector<RowCacheStamp> {{8, 3, 1, 0, 4},
                                                     {7, 4, 1, 0, 4},
                                                     {7, 3, 2, 0, 4},
                                                     {7, 3, 1, 80, 4},
                                                     {7, 3, 1, 0, 8}};

    for (const auto& changed: changes)
    {
        auto cache = filled(base, 0, 4);

        check(cache.rowsHeld() == 4);

        check(!cache.revalidate(changed));
        check(cache.rowsHeld() == 0);
    }

    // And the same stamp again keeps them, or nothing above is measuring
    // anything: a cache that dropped its rows unconditionally would pass every
    // case in the loop.
    auto cache = filled(base, 0, 4);

    check(cache.revalidate(base));
    check(cache.rowsHeld() == 4);

    // The first stamp of a fresh cache is not a rebuild. It reads as one if
    // clear() is called unconditionally, and then the counter the tests above
    // rely on counts a frame that never happened.
    auto fresh = RowCache {};

    check(!fresh.revalidate(base));
    check(fresh.discards() == 0);
};

auto tWindowKeepsOverlap = test("Damage/aMovedWindowKeepsWhatBothHold") = []
{
    auto cache = filled(10, 20);

    cache.setWindow(15, 25);

    check(cache.rowsHeld() == 5);

    for (auto row = 15; row < 20; ++row)
        check(cache.find(static_cast<std::size_t>(row)) != nullptr);

    // The rows that left are gone rather than merely unreachable, which is what
    // bounds the memory.
    check(cache.find(14) == nullptr);
    check(cache.find(20) == nullptr);
};

auto tDisjointWindow = test("Damage/aWindowThatSharesNothingKeepsNothing") = []
{
    auto cache = filled(10, 20);

    cache.setWindow(100, 110);

    check(cache.rowsHeld() == 0);
    check(cache.find(10) == nullptr);
};

auto tEmptyWindow = test("Damage/anEmptyWindowHoldsNothing") = []
{
    auto cache = filled(10, 20);

    cache.setWindow(5, 5);

    check(cache.rowsHeld() == 0);
};

// Storing outside the window is refused rather than resized into, or a stray
// row index would allocate the space between it and the window — which on a
// large file is the whole file.
auto tStoreRefusesOutside = test("Damage/aRowOutsideTheWindowIsRefused") = []
{
    auto cache = RowCache {};
    cache.setWindow(10, 20);

    auto row = oneGlyph();

    check(cache.store(9, std::move(row)) == nullptr);
    check(cache.store(20, std::move(row)) == nullptr);

    // Refused without taking it, so the caller can still draw what it built.
    check(row.text.size() == 1);

    check(cache.store(10, std::move(row)) != nullptr);
    check(cache.rowsHeld() == 1);
};
