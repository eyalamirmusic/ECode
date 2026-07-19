#include <ECodeUI/Chrome.h>
#include <ECodeUI/Splitter.h>
#include <ECodeUI/WidgetHost.h>

#include <NanoTest/NanoTest.h>

#include <eacp/GPU/GPU.h>
#include <eacp/Text/Text.h>

#include <algorithm>
#include <cmath>
#include <optional>

// What the splitter draws.
//
// SplitterTests covers the arithmetic and who answers for the cursor, and none
// of it can see the two things that only exist as pixels: that the *line* is
// thin even though the grab band is thick, and that hovering lights it. A
// divider drawn as wide as it is grabbable is a bar through the chrome, and a
// divider that never lights gives no sign it can be dragged at all — the cursor
// is the other half of that signal, and it is not in the picture.

using namespace nano;
using namespace eacp;
using namespace ecode;

namespace
{
constexpr auto viewWidth = 400.f;
constexpr auto viewHeight = 200.f;

// Where the divider sits in every test here.
constexpr auto dividerX = 160.f;

// Two panes either side of a splitter, which is the arrangement it is for.
struct SplitTestView final : GPU::GPUView
{
    SplitTestView()
    {
        setSampleCount(1);
        setBounds({0.f, 0.f, viewWidth, viewHeight});

        root.addChild(left);
        root.addChild(right);
        root.addChild(splitter);

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

        glyphs.emplace();
        glyphs->setViewportSize({viewWidth, viewHeight});

        root.setBounds({0.f, 0.f, viewWidth, viewHeight});

        splitter.setLimits(40.f, viewWidth - 40.f);
        splitter.setPosition(dividerX);

        left.setBounds({0.f, 0.f, dividerX, viewHeight});
        right.setBounds({dividerX, 0.f, viewWidth - dividerX, viewHeight});
        splitter.setBounds({dividerX - Splitter::grabThickness * 0.5f,
                            0.f,
                            Splitter::grabThickness,
                            viewHeight});

        return true;
    }

    void render(GPU::Frame& frame) override
    {
        auto pass = frame.beginPass({Graphics::Color::black()});

        if (!atlas || !glyphs)
            return;

        auto sprites =
            Sprites::SpriteRenderer {{viewWidth, viewHeight}, sampleCount()};

        host.prepare(*atlas);
        atlas->commit();

        auto context = PaintContext {
            pass, sprites, *glyphs, *atlas, {0.f, 0.f, viewWidth, viewHeight}, 1.f};

        host.paint(context);
    }

    ChromeTheme theme;

    Widget root;

    // Deliberately the same colour on both sides. A real window has a sidebar
    // and an editor that already differ, and that difference would draw a seam
    // whether or not the splitter drew anything — so these panes are identical
    // and every pixel of edge in the picture belongs to the splitter.
    Panel left {theme.sidebar};
    Panel right {theme.sidebar};

    Splitter splitter {theme, Splitter::Orientation::Vertical};

    WidgetHost host;

    OwningPointer<Text::GlyphAtlas> atlas;
    std::optional<Text::GlyphRenderer> glyphs;
};

float brightnessAt(const Graphics::Image& image, int x, int y)
{
    const auto pixel = image.at(x, y);

    return (pixel.r + pixel.g + pixel.b) / 3.f;
}

// Brightest pixel across the divider's band on one row.
//
// Sampling the divider's own column would be wrong by a pixel: the line is one
// point wide centred in an 8-point band, so it lands at dividerX - 0.5 and the
// lit column is the one to its left. A peak over the band answers regardless of
// which side of the half-pixel it falls.
float peakAcrossBand(const Graphics::Image& image, int y)
{
    auto peak = 0.f;

    for (auto x = (int) dividerX - 6; x <= (int) dividerX + 6; ++x)
        peak = std::max(peak, brightnessAt(image, x, y));

    return peak;
}

// Columns across the divider that differ from the pane colour.
int litColumns(const Graphics::Image& image, const Graphics::Color& pane)
{
    const auto paneBrightness = (pane.r + pane.g + pane.b) / 3.f;

    auto total = 0;

    for (auto x = (int) dividerX - 6; x <= (int) dividerX + 6; ++x)
        if (std::abs(brightnessAt(image, x, 100) - paneBrightness) > 0.01f)
            ++total;

    return total;
}

Graphics::MouseEvent mouseAt(float x, float y)
{
    auto event = Graphics::MouseEvent {};
    event.pos = {x, y};

    return event;
}
} // namespace

// The line is thin. The grab band is 8 points wide and the line is one, so a
// splitter that filled its bounds would light 8 columns instead of about one.
auto tLineIsThinnerThanTheGrabBand =
    test("SplitterRender/lineIsThinnerThanTheGrabBand") = []
{
    auto view = SplitTestView {};

    if (!view.build())
        return;

    view.splitter.mouseMoved(mouseAt(dividerX, 100.f));

    const auto image = view.renderToImage(1.f);

    const auto lit = litColumns(image, view.theme.sidebar);

    check(lit > 0);
    check(lit < (int) Splitter::grabThickness);
};

// Hovering lights it, and that is checked by comparing two renders of the same
// scene rather than against the colour that was written — an 8-bit drawable
// makes a strict inequality against a written value true on rounding alone.
auto tHoverLightsTheLine = test("SplitterRender/hoverLightsTheLine") = []
{
    auto view = SplitTestView {};

    if (!view.build())
        return;

    const auto idle = view.renderToImage(1.f);
    const auto idleBrightness = peakAcrossBand(idle, 100);

    view.splitter.mouseMoved(mouseAt(dividerX, 100.f));

    const auto hovered = view.renderToImage(1.f);
    const auto hoveredBrightness = peakAcrossBand(hovered, 100);

    // 0.06 alpha white against a solid accent, so the gap is nowhere near
    // anything quantisation could account for.
    check(hoveredBrightness > idleBrightness + 0.1f);
};

// Moving the pointer off it puts it back, so the lit state tracks the pointer
// rather than latching the first time it is touched.
auto tLeavingUnlightsTheLine = test("SplitterRender/leavingUnlightsTheLine") = []
{
    auto view = SplitTestView {};

    if (!view.build())
        return;

    view.splitter.mouseMoved(mouseAt(dividerX, 100.f));

    const auto hoveredBrightness = peakAcrossBand(view.renderToImage(1.f), 100);

    view.splitter.mouseMoved(mouseAt(20.f, 100.f));

    const auto leftBrightness = peakAcrossBand(view.renderToImage(1.f), 100);

    check(leftBrightness < hoveredBrightness - 0.1f);
};

// Dragging lights it too, and keeps it lit while the pointer is nowhere near —
// the visual counterpart of the cursor holding through a drag.
auto tDragKeepsTheLineLit = test("SplitterRender/dragKeepsTheLineLit") = []
{
    auto view = SplitTestView {};

    if (!view.build())
        return;

    const auto idleBrightness = peakAcrossBand(view.renderToImage(1.f), 100);

    view.splitter.mouseDown(mouseAt(dividerX, 100.f));

    // The divider has not been relaid out by a parent here, so it still draws
    // at dividerX while the drag runs.
    const auto draggingBrightness = peakAcrossBand(view.renderToImage(1.f), 100);

    check(draggingBrightness > idleBrightness + 0.1f);
};

// The panes either side are left alone. A splitter that filled its bounds with
// the accent would eat 8 points out of the chrome, and the thinness check above
// would not see it if the fill happened to land on the sampled column.
auto tPanesAreUntouched = test("SplitterRender/panesAreUntouched") = []
{
    auto view = SplitTestView {};

    if (!view.build())
        return;

    view.splitter.mouseMoved(mouseAt(dividerX, 100.f));

    const auto image = view.renderToImage(1.f);
    const auto pane = view.theme.sidebar;
    const auto paneBrightness = (pane.r + pane.g + pane.b) / 3.f;

    // Just outside the grab band on both sides.
    for (const auto x: {(int) dividerX - 6, (int) dividerX + 6})
        check(std::abs(brightnessAt(image, x, 100) - paneBrightness) < 0.01f);
};
