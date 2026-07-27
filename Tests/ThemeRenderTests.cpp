#include <ECodeWorkbench/Chrome.h>
#include <ECodeWorkbench/FindBar.h>
#include <ECodeWorkbench/Settings.h>
#include <ECodeWorkbench/Themes.h>
#include <ECodeWidgets/WidgetHost.h>

#include <NanoTest/NanoTest.h>

#include <eacp/GPU/GPU.h>
#include <eacp/Text/Text.h>

#include <cmath>
#include <optional>

// Whether changing the theme changes what is on screen.
//
// SettingsTests proves the file resolves to the right palette, and it could go
// on proving that forever while the window stayed the colour it started. Almost
// every widget reads its colours through a reference to the theme, so an
// assignment is all it takes — but two shapes cannot work that way, and both
// were built on the assumption that a theme is decided once and never moves:
//
//   - a Panel *is* a colour, and used to hold a copy of one;
//   - a TextField is handed its colours, because the same field sits on the
//     palette's background in one place and the find bar's in another.
//
// Neither has any CPU-side observable. A stale copy draws perfectly, in last
// week's colour, and the only place it shows up is a pixel.

using namespace nano;
using namespace eacp;
using namespace ecode;

namespace
{
constexpr auto viewWidth = 400.f;
constexpr auto viewHeight = 240.f;

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

// Counts by hue rather than by brightness. Antialiasing turns every glyph edge
// into a ramp towards whatever is behind it, so a single-channel threshold reads
// a point on that ramp and says yes to almost anything; the two channels that
// have to stay *down* are what make this a question about the colour.
//
// Over a region, and the region is load-bearing. The find bar draws its own
// button labels in `theme.findText` too, and reads that one live off the theme
// — so counting the whole image asks a question three widgets can answer, and
// the two that were never in doubt answer it first. Scoped to the field, the
// only thing that can turn red is the copy the field is holding.
int redIn(const Graphics::Image& image, const Graphics::Rect& area)
{
    auto total = 0;

    for (auto y = static_cast<int>(area.y); y < static_cast<int>(area.bottom()); ++y)
        for (auto x = static_cast<int>(area.x); x < static_cast<int>(area.right());
             ++x)
        {
            const auto pixel = image.at(x, y);

            if (pixel.r > 0.6f && pixel.g < 0.25f && pixel.b < 0.25f)
                ++total;
        }

    return total;
}

// Pixels inside `area` that are not the background — "was anything drawn here",
// without depending on which glyph landed where.
int inkIn(const Graphics::Image& image,
          const Graphics::Rect& area,
          const Graphics::Color& background)
{
    auto total = 0;

    for (auto y = static_cast<int>(area.y); y < static_cast<int>(area.bottom()); ++y)
        for (auto x = static_cast<int>(area.x); x < static_cast<int>(area.right());
             ++x)
            if (!isColor(image, x, y, background))
                ++total;

    return total;
}

int pixelsOfColor(const Graphics::Image& image, const Graphics::Color& expected)
{
    auto total = 0;

    for (auto y = 0; y < image.height(); ++y)
        for (auto x = 0; x < image.width(); ++x)
            if (isColor(image, x, y, expected))
                ++total;

    return total;
}

// A panel filling the view, which is the sidebar's arrangement and the shape the
// copied-colour bug lived in.
struct PanelTestView final : GPU::GPUView
{
    PanelTestView()
    {
        setSampleCount(1);
        setBounds({0.f, 0.f, viewWidth, viewHeight});

        root.addChild(body);
        root.setBounds({0.f, 0.f, viewWidth, viewHeight});
        body.setBounds({0.f, 0.f, viewWidth, viewHeight});

        host.setRoot(root);
    }

    void setTheme(const ChromeTheme& newTheme)
    {
        theme = newTheme;
        root.themeChangedTree();
    }

    void render(GPU::Frame& frame) override
    {
        auto pass = frame.beginPass({Graphics::Color::black()});

        auto sprites =
            Sprites::SpriteRenderer {{viewWidth, viewHeight}, sampleCount()};

        auto context = PaintContext {
            pass, sprites, glyphs, *atlas, {0.f, 0.f, viewWidth, viewHeight}, 1.f};

        host.paint(context);
    }

    // Nothing here draws a glyph, but PaintContext takes an atlas and a batch,
    // so both have to exist. Neither is ever asked for a slot.
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
    Panel body {theme.sidebar};

    WidgetHost host;

    Text::GlyphRenderer glyphs;
    OwningPointer<Text::GlyphAtlas> atlas;
};

// The find bar with something typed into it, so the field draws real text in
// `findText` rather than a placeholder in `findHintText`.
struct FindBarTestView final : GPU::GPUView
{
    FindBarTestView()
    {
        setSampleCount(1);
        setBounds({0.f, 0.f, viewWidth, viewHeight});

        root.addChild(bar);
        root.setBounds({0.f, 0.f, viewWidth, viewHeight});

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

        bar.show("WWWWWWWWWW", false);
        bar.setBounds(
            {0.f, 0.f, std::min(bar.barWidth(), viewWidth), bar.barHeight()});

        return true;
    }

    void setTheme(const ChromeTheme& newTheme)
    {
        theme = newTheme;
        root.themeChangedTree();
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
    FindBar bar {theme};

    WidgetHost host;

    OwningPointer<Text::GlyphAtlas> atlas;
    std::optional<Text::GlyphRenderer> glyphs;
};
} // namespace

// The bug the pointer in Panel exists to prevent. A panel that copied its colour
// draws the startup theme forever, and every unit test in the suite agrees the
// theme changed.
auto tPanelFollowsTheTheme = test("ThemeRender/aPanelFollowsAThemeChange") = []
{
    auto view = PanelTestView {};

    if (!view.build())
        return;

    const auto dark = themeByName("dark").chrome;
    const auto light = themeByName("light").chrome;

    // The whole point of the case: the two palettes disagree here. A pair of
    // themes that happened to share this colour could not tell a live panel from
    // a stale one.
    check(!near(dark.sidebar.r, light.sidebar.r));

    const auto before = view.renderToImage(1.f);

    if (!before.isValid())
        return;

    check(isColor(before, 10, 10, dark.sidebar));

    view.setTheme(light);

    const auto after = view.renderToImage(1.f);

    check(after.isValid());
    check(isColor(after, 10, 10, light.sidebar));

    // The whole panel, not one lucky pixel — a fill drawn at the wrong size
    // would satisfy the corner and leave most of the view behind.
    check(pixelsOfColor(after, light.sidebar)
          > pixelsOfColor(before, light.sidebar));
    check(pixelsOfColor(after, dark.sidebar) == 0);
};

// The other shape a theme change cannot reach by itself. The field is handed its
// colours, so the copy it is holding was taken when the bar was constructed.
//
// Red, and counted by hue: nothing in the palette is strongly red with its other
// two channels down, so the count starts at zero and cannot be reached by the
// antialiased edge of anything else.
auto tFieldFollowsTheTheme =
    test("ThemeRender/aTextFieldsOwnColoursFollowAThemeChange") = []
{
    auto view = FindBarTestView {};

    if (!view.build())
        return;

    const auto field = view.bar.keyboardTarget().bounds();
    const auto dark = ChromeTheme {};

    const auto before = view.renderToImage(1.f);

    if (!before.isValid())
        return;

    // Two frames of an empty field would compare equal and prove nothing, so
    // this is the check that the query is on screen at all before anything asks
    // what colour it is.
    check(inkIn(before, field, dark.findFieldBackground) > 50);
    check(redIn(before, field) == 0);

    auto red = dark;
    red.findText = Graphics::Color {1.f, 0.f, 0.f};

    view.setTheme(red);

    const auto after = view.renderToImage(1.f);

    check(after.isValid());
    check(redIn(after, field) > 20);
};
