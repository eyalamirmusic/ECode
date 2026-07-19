#include <ECodeUI/Chrome.h>
#include <ECodeUI/CommandPalette.h>
#include <ECodeUI/WidgetHost.h>

#include <NanoTest/NanoTest.h>

#include <eacp/GPU/GPU.h>
#include <eacp/Text/Text.h>

#include <cmath>
#include <optional>

// What the palette actually puts on screen.
//
// CommandPaletteTests covers what it is offering and what a key does to it, and
// none of that can tell you whether a single pixel reached the drawable. Two
// things here have no CPU-side observable at all and are the kind that pass
// every logic test while being obviously wrong the moment anyone looks:
//
//   - the backdrop is *translucent*, so the file stays legible under it. An
//     alpha that is not being blended gives either an opaque black sheet or no
//     dimming whatever, and both are one wrong blend state away.
//   - the matched characters are tinted differently from the rest of the title,
//     which is the only thing distinguishing "it matched" from "it matched
//     *here*".
//
// So these render off-screen and read the pixels back.

using namespace nano;
using namespace eacp;
using namespace ecode;

namespace
{
constexpr auto viewWidth = 700.f;
constexpr auto viewHeight = 400.f;

// The palette over a flat panel standing in for the editor underneath it.
struct PaletteTestView final : GPU::GPUView
{
    PaletteTestView()
    {
        setSampleCount(1);
        setBounds({0.f, 0.f, viewWidth, viewHeight});

        registry.add({"workbench.showPalette", "Show All Commands"});
        registry.add({"file.save", "File: Save"});
        registry.add({"edit.undo", "Edit: Undo"});
        registry.add({"edit.selectAll", "Edit: Select All"});

        keymap.bind("cmd+s", "file.save");

        root.addChild(background);
        root.addChild(palette);

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

        const auto area = Graphics::Rect {0.f, 0.f, viewWidth, viewHeight};

        root.setBounds(area);
        background.setBounds(area);
        palette.setBounds(area);

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
    CommandRegistry registry;
    Keymap keymap;

    Widget root;
    Panel background {theme.sidebar};
    CommandPalette palette {theme, registry, keymap};

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

// Pixels where two renders of the same size disagree. Used where a region's
// own background cannot be the reference — the selected row is tinted across
// its whole width, so counting ink against the box colour there returns every
// pixel in both images and compares nothing.
int differingPixels(const Graphics::Image& a,
                    const Graphics::Image& b,
                    const Graphics::Rect& area)
{
    auto total = 0;

    for (auto y = static_cast<int>(area.y); y < static_cast<int>(area.bottom()); ++y)
        for (auto x = static_cast<int>(area.x); x < static_cast<int>(area.right());
             ++x)
            if (!isColor(a, x, y, b.at(x, y)))
                ++total;

    return total;
}

// The match tint is blue and everything else in the box is near-white or the
// background, so blue running well ahead of red can only be a highlighted
// glyph. Measured as a margin rather than an exact colour because a glyph is
// antialiased: only the very centre of a stem is ever the full tint.
bool hasBlueTintedPixel(const Graphics::Image& image, const Graphics::Rect& area)
{
    for (auto y = static_cast<int>(area.y); y < static_cast<int>(area.bottom()); ++y)
    {
        for (auto x = static_cast<int>(area.x); x < static_cast<int>(area.right());
             ++x)
        {
            const auto pixel = image.at(x, y);

            if (pixel.b - pixel.r > 0.15f)
                return true;
        }
    }

    return false;
}
} // namespace

// The palette is drawn at all, on top of what was there, and only when open.
auto tPaletteDrawsOverTheWindow =
    test("PaletteRender/theBoxIsDrawnOverWhatIsBehindIt") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = PaletteTestView {};

    if (!view.build())
        return;

    const auto closed = view.renderToImage(1.f);

    if (!closed.isValid())
        return;

    // Closed, the panel behind is all there is.
    const auto box = view.palette.boxBounds();
    const auto middle = Graphics::Point {box.x + box.w * 0.5f, box.y + 6.f};

    check(isColor(closed,
                  static_cast<int>(middle.x),
                  static_cast<int>(middle.y),
                  view.theme.sidebar));

    view.palette.show();

    const auto open = view.renderToImage(1.f);

    if (!open.isValid())
        return;

    // Open, the box's own background is what shows there instead — so the
    // palette paints after the panel rather than under it.
    check(isColor(open,
                  static_cast<int>(middle.x),
                  static_cast<int>(middle.y),
                  view.theme.paletteBackground));
};

// The query field draws a caret, and only while it holds focus.
//
// Worth a test of its own because the caret changed hands: the palette used to
// draw one unconditionally, and now it belongs to a TextField that shows it only
// when focused. That is right — two fields both showing a caret is worse than
// none — but it means the palette's caret is now downstream of the application
// remembering to focus `keyboardTarget()` rather than the palette itself. Get
// that wrong and the palette opens with no caret and no other symptom.
auto tQueryCaretFollowsFocus =
    test("PaletteRender/theQueryDrawsACaretOnlyWhileFocused") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = PaletteTestView {};

    if (!view.build())
        return;

    view.palette.show();

    const auto unfocused = view.renderToImage(1.f);

    view.host.setFocus(&view.palette.keyboardTarget());

    const auto focused = view.renderToImage(1.f);

    check(unfocused.isValid() && focused.isValid());

    // An empty query, so the only thing that can differ in the input strip is
    // the caret — the placeholder is drawn either way.
    const auto strip = view.palette.inputBounds();

    check(differingPixels(focused, unfocused, strip) > 0);

    // And it is the caret rather than the placeholder having moved: outside the
    // strip the two renders are identical.
    check(differingPixels(focused, unfocused, view.palette.resultsBounds()) == 0);
};

// The backdrop dims rather than replaces. An unblended alpha gives a flat black
// sheet and a dropped one gives no dimming at all; the point of the palette
// being an overlay is that the work stays visible underneath it.
auto tBackdropIsTranslucent =
    test("PaletteRender/theBackdropDimsRatherThanCovers") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = PaletteTestView {};

    if (!view.build())
        return;

    view.palette.show();

    const auto image = view.renderToImage(1.f);

    if (!image.isValid())
        return;

    // Bottom-left, which the box never reaches.
    const auto pixel = image.at(8, static_cast<int>(viewHeight) - 8);
    const auto behind = view.theme.sidebar;

    // Meaningfully darker than the panel, not merely darker. "Merely" is a real
    // trap here and this test first passed against it: a colour written as
    // 0.102f comes back through an 8-bit drawable as 0.10196, so a plain `<`
    // holds by rounding alone and the test stays green with the backdrop
    // deleted outright. The backdrop is 45% black, so a fifth is a wide margin
    // over quantisation and nowhere near the real effect.
    check(pixel.g < behind.g * 0.8f);

    // ...but not black, and still carrying the panel's own hue underneath.
    // Both halves are needed: the check above alone passes for an opaque black
    // sheet, and these alone pass for no backdrop at all.
    check(pixel.g > 0.02f);
    check(pixel.b > pixel.r);
};

// The characters the query matched are tinted, which is the difference between
// a list that shows what it matched and one that only shows that it did.
//
// Paired against the same palette with an empty query: with nothing matched
// there must be no tinted pixel anywhere, so this cannot pass on some blue
// belonging to the chrome.
auto tMatchedCharactersAreTinted =
    test("PaletteRender/matchedCharactersAreDrawnTinted") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto unfiltered = PaletteTestView {};
    auto filtered = PaletteTestView {};

    if (!unfiltered.build() || !filtered.build())
        return;

    unfiltered.palette.show();

    filtered.palette.show();
    filtered.palette.setQuery("sa");

    const auto plain = unfiltered.renderToImage(1.f);
    const auto tinted = filtered.renderToImage(1.f);

    if (!plain.isValid() || !tinted.isValid())
        return;

    const auto rows = filtered.palette.resultsBounds();

    check(hasBlueTintedPixel(tinted, rows));
    check(!hasBlueTintedPixel(plain, unfiltered.palette.resultsBounds()));
};

// A hidden palette draws nothing whatever — not a backdrop, not a border.
auto tHiddenPaletteDrawsNothing =
    test("PaletteRender/aClosedPaletteLeavesNoTrace") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = PaletteTestView {};

    if (!view.build())
        return;

    view.palette.show();
    view.palette.hide();

    const auto image = view.renderToImage(1.f);

    if (!image.isValid())
        return;

    // The whole window is the panel's colour and nothing else.
    check(inkIn(image, {0.f, 0.f, viewWidth, viewHeight}, view.theme.sidebar) == 0);
};

// The shortcut column reaches the screen. Compared against the same command
// with nothing bound to it, so this cannot pass by counting the title's pixels.
auto tShortcutsAreDrawn = test("PaletteRender/theShortcutColumnIsDrawn") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto bound = PaletteTestView {};
    auto unbound = PaletteTestView {};

    if (!bound.build() || !unbound.build())
        return;

    // Both show one row, the same title; only one of them has a binding.
    unbound.keymap = Keymap {};

    bound.palette.show();
    bound.palette.setQuery("save");

    unbound.palette.show();
    unbound.palette.setQuery("save");

    const auto withChord = bound.renderToImage(1.f);
    const auto without = unbound.renderToImage(1.f);

    if (!withChord.isValid() || !without.isValid())
        return;

    // The right-hand end of the rows, where the shortcut is aligned and the
    // title never reaches. Compared between the two images rather than against
    // the box colour: the selected row is tinted edge to edge, so every pixel
    // there differs from the background in *both* and the count says nothing.
    auto rows = bound.palette.resultsBounds();
    const auto column = rows.removeFromRight(rows.w * 0.3f);

    check(differingPixels(withChord, without, column) > 0);

    // And the left-hand end, where both draw the same title, must be identical
    // — otherwise the difference above is the whole row moving rather than a
    // shortcut appearing.
    check(differingPixels(withChord, without, rows) == 0);
};
