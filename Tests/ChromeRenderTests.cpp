#include <ECodeUI/Chrome.h>
#include <ECodeUI/WidgetHost.h>

#include <NanoTest/NanoTest.h>

#include <eacp/GPU/GPU.h>
#include <eacp/Text/Text.h>

#include <cmath>
#include <optional>

// What the widget tree actually puts on screen.
//
// WidgetTests covers the arithmetic — who owns which rectangle, who gets the
// click. None of that can tell you whether anything was drawn, or drawn in the
// right place: the tab bar sat along the *bottom* of this window for months
// while every layout number was correct, because the convention underneath the
// arithmetic was inverted. §9 of PLAN.md is the record of that.
//
// So these render off-screen and read pixels back. They exist mainly for the
// two things in the paint path that have no CPU-side observable and are easy to
// get silently wrong: clipping a child to its parent, and the interleaving of
// the sprite and glyph pipelines, where a flush leaves the wrong pipeline bound
// and the next rectangle comes out drawn through the glyph shader.

using namespace nano;
using namespace eacp;
using namespace ecode;

namespace
{
constexpr auto viewWidth = 400.f;
constexpr auto viewHeight = 240.f;
constexpr auto statusHeight = 22.f;
constexpr auto tabHeight = 35.f;

// A tab strip along the top, a status bar along the bottom, a panel filling
// what is left — the window's shape, minus the editor.
struct ChromeTestView final : GPU::GPUView
{
    ChromeTestView()
    {
        setSampleCount(1);
        setBounds({0.f, 0.f, viewWidth, viewHeight});

        root.addChild(status);
        root.addChild(body);
        root.addChild(tabs);

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

        layout();

        return true;
    }

    void layout()
    {
        auto area = Graphics::Rect {0.f, 0.f, viewWidth, viewHeight};

        root.setBounds(area);

        status.setBounds(area.removeFromBottom(statusHeight));
        tabs.setBounds(area.removeFromTop(tabHeight));
        body.setBounds(area);
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

        auto context = PaintContext {pass,
                                     sprites,
                                     *glyphs,
                                     *atlas,
                                     {0.f, 0.f, viewWidth, viewHeight},
                                     1.f};

        host.paint(context);
    }

    ChromeTheme theme;

    Widget root;
    Panel body {theme.sidebar};
    TabBar tabs {theme};
    StatusBar status {theme};

    WidgetHost host;

    OwningPointer<Text::GlyphAtlas> atlas;
    std::optional<Text::GlyphRenderer> glyphs;
};

bool near(float a, float b)
{
    return std::abs(a - b) < 0.02f;
}

bool isColor(const Graphics::Image& image,
             int x,
             int y,
             const Graphics::Color& expected)
{
    const auto pixel = image.at(x, y);

    return near(pixel.r, expected.r) && near(pixel.g, expected.g)
           && near(pixel.b, expected.b);
}

// Pixels differing from the background within a region — "was anything drawn
// here", without depending on exactly which glyph landed where.
int inkIn(const Graphics::Image& image,
          const Graphics::Rect& area,
          const Graphics::Color& background)
{
    auto total = 0;

    for (auto y = static_cast<int>(area.y); y < static_cast<int>(area.bottom());
         ++y)
        for (auto x = static_cast<int>(area.x); x < static_cast<int>(area.right());
             ++x)
            if (!isColor(image, x, y, background))
                ++total;

    return total;
}

// Rect's constructor is not constexpr, so this is an ordinary constant.
const auto containerBounds = Graphics::Rect {100.f, 60.f, 120.f, 80.f};

// A small panel holding a child laid out far larger than itself.
//
// The arrangement that separates "the clip narrows" from "the clip intersects
// with its parent's". Anything wholly inside its parent looks identical either
// way, which is why the tab-title case below cannot stand in for this one.
struct NestedClipTestView final : GPU::GPUView
{
    NestedClipTestView()
    {
        setSampleCount(1);
        setBounds({0.f, 0.f, viewWidth, viewHeight});

        root.addChild(container);
        container.addChild(overflowing);

        root.setBounds({0.f, 0.f, viewWidth, viewHeight});
        container.setBounds(containerBounds);

        // Four times its parent, hanging off every edge.
        overflowing.setBounds({0.f, 0.f, viewWidth, viewHeight});

        host.setRoot(root);
    }

    void render(GPU::Frame& frame) override
    {
        auto pass = frame.beginPass({Graphics::Color::black()});

        auto sprites =
            Sprites::SpriteRenderer {{viewWidth, viewHeight}, sampleCount()};

        auto context = PaintContext {pass,
                                     sprites,
                                     glyphs,
                                     *atlas,
                                     {0.f, 0.f, viewWidth, viewHeight},
                                     1.f};

        host.paint(context);
    }

    bool build()
    {
        auto request = Text::FontRequest {};
        request.family = "Menlo";
        request.scale = 1.f;

        auto rasterizer = makeOwned<Text::GlyphRasterizer>(request);

        if (!rasterizer->isValid())
            return false;

        atlas = makeOwned<Text::GlyphAtlas>(
            OwningPointer<Text::GlyphSource> {std::move(rasterizer)}, 256, 512);

        return true;
    }

    ChromeTheme theme;

    Widget root;
    Panel container {theme.sidebar};
    Panel overflowing {theme.conflict};

    WidgetHost host;
    Text::GlyphRenderer glyphs;
    OwningPointer<Text::GlyphAtlas> atlas;
};

TabItem tab(std::string title, bool modified = false, bool conflicted = false)
{
    auto item = TabItem {};

    item.title = std::move(title);
    item.modified = modified;
    item.conflicted = conflicted;

    return item;
}
} // namespace

// The one the y-down fix was about. Arithmetic cannot answer it; only the image
// can. Asserted against the *rendered* rows rather than against bounds().
auto tBarsLandAtTheRightEnds = test("ChromeRender/tabBarIsAtTheTopAndStatusAtTheBottom") =
    []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = ChromeTestView {};

    if (!view.build())
        return;

    view.tabs.setTabs({tab("main.cpp")});

    const auto image = view.renderToImage(1.f);

    if (!image.isValid())
        return;

    // Top row is the tab strip, bottom row is the status bar, and the middle
    // is neither.
    check(isColor(image, 300, 2, view.theme.tabBar));
    check(isColor(image, 300, image.height() - 2, view.theme.statusBar));
    check(isColor(image, 300, image.height() / 2, view.theme.sidebar));
};

// The status bar is laid out last but painted over the full width, so its
// colour must reach the left edge rather than stopping where the body does.
auto tStatusBarSpansTheWidth = test("ChromeRender/statusBarSpansTheWholeWidth") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = ChromeTestView {};

    if (!view.build())
        return;

    const auto image = view.renderToImage(1.f);

    if (!image.isValid())
        return;

    const auto row = image.height() - 2;

    check(isColor(image, 2, row, view.theme.statusBar));
    check(isColor(image, image.width() / 2, row, view.theme.statusBar));
    check(isColor(image, image.width() - 2, row, view.theme.statusBar));
};

// The dot is the whole of a file's unsaved state on screen, and it is drawn by
// a different path from the title text. Compares against the same tab without
// it, so this cannot pass by finding the filename's own pixels.
auto tUnsavedDotIsDrawn = test("ChromeRender/theUnsavedDotIsDrawn") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto clean = ChromeTestView {};
    auto dirty = ChromeTestView {};

    if (!clean.build() || !dirty.build())
        return;

    clean.tabs.setTabs({tab("main.cpp")});
    dirty.tabs.setTabs({tab("main.cpp", true)});

    const auto cleanImage = clean.renderToImage(1.f);
    const auto dirtyImage = dirty.renderToImage(1.f);

    if (!cleanImage.isValid() || !dirtyImage.isValid())
        return;

    const auto strip = Graphics::Rect {0.f, 0.f, 180.f, tabHeight};

    // Strictly more ink with the dot than without it.
    check(inkIn(dirtyImage, strip, clean.theme.activeTab)
          > inkIn(cleanImage, strip, clean.theme.activeTab));
};

// Conflict and unsaved mean opposite things — work to save versus a question to
// answer — so they must not render identically.
auto tConflictLooksDifferent = test("ChromeRender/aConflictIsNotTheSameAsUnsaved") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto unsaved = ChromeTestView {};
    auto conflicted = ChromeTestView {};

    if (!unsaved.build() || !conflicted.build())
        return;

    unsaved.tabs.setTabs({tab("main.cpp", true, false)});
    conflicted.tabs.setTabs({tab("main.cpp", true, true)});

    const auto unsavedImage = unsaved.renderToImage(1.f);
    const auto conflictedImage = conflicted.renderToImage(1.f);

    if (!unsavedImage.isValid() || !conflictedImage.isValid())
        return;

    // Somewhere in the dot's slot the two must disagree.
    auto differs = false;

    for (auto y = 10; y < 26 && !differs; ++y)
        for (auto x = 10; x < 30 && !differs; ++x)
            if (!isColor(conflictedImage, x, y, unsavedImage.at(x, y)))
                differs = true;

    check(differs);
};

// A child bigger than its parent is cut at the parent's edge.
//
// The GPU has one scissor rect and no stack, so a child's clip has to be
// *intersected* with its parent's on the way down. Replacing it instead looks
// right for every widget that fits inside its parent — which is nearly all of
// them, and is why this needs a case that deliberately does not.
auto tChildIsClippedToItsParent =
    test("ChromeRender/aChildIsClippedToItsParentsBounds") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = NestedClipTestView {};

    if (!view.build())
        return;

    const auto image = view.renderToImage(1.f);

    if (!image.isValid())
        return;

    const auto area = containerBounds;

    // Inside the parent, the child's colour is what shows.
    check(isColor(image,
                  static_cast<int>(area.x + area.w / 2.f),
                  static_cast<int>(area.y + area.h / 2.f),
                  view.theme.conflict));

    // Outside it, on all four sides, the child must not have reached.
    check(!isColor(image, static_cast<int>(area.x) - 6, 100, view.theme.conflict));
    check(!isColor(image,
                   static_cast<int>(area.right()) + 6,
                   100,
                   view.theme.conflict));
    check(!isColor(image, 150, static_cast<int>(area.y) - 6, view.theme.conflict));
    check(
        !isColor(image, 150, static_cast<int>(area.bottom()) + 6, view.theme.conflict));
};

// A title too long for its tab must stop at the tab's edge. Narrower than the
// case above: this one only shows that the clip is applied at all.
auto tLongTitleIsClipped = test("ChromeRender/aLongTitleIsClippedToItsTab") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = ChromeTestView {};

    if (!view.build())
        return;

    view.tabs.setTabs(
        {tab("a-file-with-a-very-long-name-that-cannot-possibly-fit.cpp")});

    const auto image = view.renderToImage(1.f);

    if (!image.isValid())
        return;

    // Past the tab's 180pt width, the strip must be bare tab-bar colour.
    const auto beyond = Graphics::Rect {190.f, 0.f, viewWidth - 190.f, tabHeight};

    check(inkIn(image, beyond, view.theme.tabBar) == 0);
};

// The status bar draws a rectangle, then text, then the tab bar draws its own
// rectangle. If a glyph flush leaves its pipeline bound, that later rectangle
// comes out through the glyph shader — which samples an R8 mask and turns the
// fill into garbage. The bug is invisible until two widgets both draw text and
// fills in the same frame, which is exactly this arrangement.
auto tFillsSurviveTextBeforeThem =
    test("ChromeRender/aFillAfterTextIsStillTheRightColour") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = ChromeTestView {};

    if (!view.build())
        return;

    // Text in both bars, so the tab strip's fill is issued after a glyph flush.
    view.status.setText("Ln 1, Col 1", "UTF-8");
    view.tabs.setTabs({tab("main.cpp")});

    const auto image = view.renderToImage(1.f);

    if (!image.isValid())
        return;

    // Well right of the tab, where only the strip's own fill can be.
    check(isColor(image, image.width() - 4, 4, view.theme.tabBar));

    // And the status bar's fill, which is drawn before its own text.
    check(isColor(image, image.width() - 4, image.height() - 4, view.theme.statusBar));
};
