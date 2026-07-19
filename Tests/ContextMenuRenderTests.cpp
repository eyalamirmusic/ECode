#include <ECodeUI/Chrome.h>
#include <ECodeUI/ContextMenu.h>
#include <ECodeUI/WidgetHost.h>

#include <NanoTest/NanoTest.h>

#include <eacp/GPU/GPU.h>
#include <eacp/Text/Text.h>

#include <algorithm>
#include <cmath>
#include <optional>

// What the context menu actually puts on screen.
//
// ContextMenuTests covers what it offers and what a click does to it, and none
// of that can see the one mistake that would matter most here: the menu's
// *bounds* are the whole window while the thing it draws is a small box inside
// them. Painting bounds() instead of boxBounds() passes every logic test in the
// suite and fills the window with a grey sheet.
//
// The rest is the same class of thing — a highlight bar, a separator rule and
// the greying of an unavailable row all have no CPU-side observable at all.

using namespace nano;
using namespace eacp;
using namespace ecode;

namespace
{
constexpr auto viewWidth = 640.f;
constexpr auto viewHeight = 420.f;

// Where every test opens the menu. Far enough from both edges that the box is
// placed at the anchor rather than flipped, so the geometry below is simple.
constexpr auto anchorX = 120.f;
constexpr auto anchorY = 90.f;

// The menu over a flat panel standing in for the editor underneath it.
struct MenuTestView final : GPU::GPUView
{
    MenuTestView()
    {
        setSampleCount(1);
        setBounds({0.f, 0.f, viewWidth, viewHeight});

        registry.add({"edit.cut", "Cut", [] {}, [this] { return cutEnabled; }});
        registry.add({"edit.copy", "Copy"});
        registry.add({"edit.paste", "Paste"});
        registry.add({"edit.selectAll", "Select All"});

        keymap.bind("cmd+x", "edit.cut");

        root.addChild(background);
        root.addChild(menu);

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
        menu.setBounds(area);

        return true;
    }

    void openClipboardMenu()
    {
        menu.show({anchorX, anchorY},
                  {"edit.cut", "edit.copy", "edit.paste", {}, "edit.selectAll"});
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
    ContextMenu menu {theme, registry, keymap};

    WidgetHost host;

    bool cutEnabled = true;

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

// Brightest pixel in a region. Used instead of counting ink because two rows
// hold different words, so their glyph counts differ and a total would compare
// the text rather than its colour. A solid glyph stem reaches the text colour,
// so the peak is the colour.
float peakBrightness(const Graphics::Image& image, const Graphics::Rect& area)
{
    auto peak = 0.f;

    for (auto y = (int) area.y; y < (int) area.bottom(); ++y)
        for (auto x = (int) area.x; x < (int) area.right(); ++x)
        {
            const auto pixel = image.at(x, y);

            peak = std::max(peak, (pixel.r + pixel.g + pixel.b) / 3.f);
        }

    return peak;
}

int countMatching(const Graphics::Image& image,
                  const Graphics::Rect& area,
                  const Graphics::Color& color)
{
    auto total = 0;

    for (auto y = (int) area.y; y < (int) area.bottom(); ++y)
        for (auto x = (int) area.x; x < (int) area.right(); ++x)
            if (isColor(image, x, y, color))
                ++total;

    return total;
}
} // namespace

// The box is drawn where it says it is, and — the point of the test — the rest
// of the window is left completely alone. A context menu has no backdrop: it is
// a small popup, not the palette's modal panel, so nothing outside its box may
// change even though its bounds cover the whole window.
auto tOnlyTheBoxIsPainted = test("ContextMenuRender/onlyTheBoxIsPainted") = []
{
    auto view = MenuTestView {};

    if (!view.build())
        return;

    view.openClipboardMenu();

    const auto image = view.renderToImage(1.f);
    const auto box = view.menu.boxBounds();

    // Inside: the menu's own background.
    check(isColor(image,
                  (int) (box.x + box.w * 0.5f),
                  (int) (box.y + box.h * 0.5f),
                  view.theme.menuBackground));

    // Outside, on all four sides: the panel underneath, untouched. Undimmed and
    // uncovered — this is what fails if the widget paints its bounds.
    check(isColor(image, 10, 10, view.theme.sidebar));
    check(isColor(image, (int) viewWidth - 10, 10, view.theme.sidebar));
    check(isColor(image, 10, (int) viewHeight - 10, view.theme.sidebar));
    check(isColor(
        image, (int) viewWidth - 10, (int) viewHeight - 10, view.theme.sidebar));

    // And just outside each edge of the box itself, which a box drawn one row
    // too tall or too wide would fail while the corners above still passed.
    check(isColor(image, (int) box.x - 3, (int) (box.y + 10.f), view.theme.sidebar));
    check(isColor(
        image, (int) box.right() + 3, (int) (box.y + 10.f), view.theme.sidebar));
    check(isColor(
        image, (int) (box.x + 10.f), (int) box.bottom() + 3, view.theme.sidebar));
};

// A closed menu draws nothing whatever.
auto tClosedMenuDrawsNothing = test("ContextMenuRender/closedMenuDrawsNothing") = []
{
    auto view = MenuTestView {};

    if (!view.build())
        return;

    const auto image = view.renderToImage(1.f);

    check(
        isColor(image, (int) anchorX + 20, (int) anchorY + 20, view.theme.sidebar));
    check(isColor(
        image, (int) viewWidth / 2, (int) viewHeight / 2, view.theme.sidebar));
};

// The highlight is a filled bar, and it is on the row the pointer is over
// rather than on all of them or none.
auto tHighlightDrawsOnHoveredRow =
    test("ContextMenuRender/highlightDrawsOnHoveredRow") = []
{
    auto view = MenuTestView {};

    if (!view.build())
        return;

    view.openClipboardMenu();

    // Needs one render first, so the box has been measured before a point
    // inside it is chosen.
    view.renderToImage(1.f);

    const auto box = view.menu.boxBounds();

    auto hover = Graphics::MouseEvent {};
    hover.pos = {box.x + 20.f, box.y + 14.f};

    view.menu.mouseMoved(hover);
    check(view.menu.highlightedRow() == 0);

    const auto image = view.renderToImage(1.f);

    const auto firstRow = Graphics::Rect {box.x, box.y + 10.f, box.w, 24.f};
    const auto secondRow = Graphics::Rect {box.x, box.y + 34.f, box.w, 24.f};

    // The bar is a solid fill, so it covers most of its row.
    check(countMatching(image, firstRow, view.theme.menuHighlight) > 400);

    // And the row below it is untouched.
    check(countMatching(image, secondRow, view.theme.menuHighlight) == 0);
};

// The separator draws a rule. Thin, so it is checked by looking for its colour
// rather than by counting — a rule with zero height is the failure this catches,
// and it is one every logic test is blind to.
auto tSeparatorDrawsARule = test("ContextMenuRender/separatorDrawsARule") = []
{
    auto view = MenuTestView {};

    if (!view.build())
        return;

    view.openClipboardMenu();

    const auto image = view.renderToImage(1.f);
    const auto box = view.menu.boxBounds();

    // Three 24pt rows, then the separator's band — inset past the box's own
    // left and right borders, which are brighter than the background and would
    // otherwise supply the peak whether or not a rule was ever drawn. That is
    // not hypothetical: the first version of this test spanned the full width
    // and passed with the rule deleted.
    const auto band =
        Graphics::Rect {box.x + 12.f, box.y + 10.f + 72.f, box.w - 24.f, 9.f};

    // Against the box's top padding strip, which is nothing but background —
    // inside the border, above the first row. A rule that failed to draw leaves
    // the band identical to it.
    const auto emptyStrip =
        Graphics::Rect {box.x + 12.f, box.y + 2.f, box.w - 24.f, 6.f};

    check(near(peakBrightness(image, emptyStrip),
               (view.theme.menuBackground.r + view.theme.menuBackground.g
                + view.theme.menuBackground.b)
                   / 3.f));

    check(peakBrightness(image, band) > peakBrightness(image, emptyStrip) + 0.04f);
};

// An unavailable row is visibly greyer than the same row when it is available.
//
// Two renders compared against each other rather than against the colour that
// was written: an 8-bit drawable makes a strict inequality against a written
// value true on rounding alone, and the same row in both images holds the same
// word, so the only difference between them is the colour it was drawn in.
auto tDisabledRowIsGreyed = test("ContextMenuRender/disabledRowIsGreyed") = []
{
    auto view = MenuTestView {};

    if (!view.build())
        return;

    view.openClipboardMenu();

    const auto enabledImage = view.renderToImage(1.f);
    const auto box = view.menu.boxBounds();
    const auto firstRow = Graphics::Rect {box.x, box.y + 10.f, box.w, 24.f};

    const auto enabledPeak = peakBrightness(enabledImage, firstRow);

    view.cutEnabled = false;

    const auto disabledPeak = peakBrightness(view.renderToImage(1.f), firstRow);

    // 0.88 against 0.42 in the theme, so a fifth is a margin quantisation
    // cannot account for and the effect comfortably clears.
    check(enabledPeak > disabledPeak + 0.2f);
};

// The highlight bar stops short of the box's own border, so the outline stays
// unbroken along the row the eye is currently on.
//
// Found by rendering one and looking at it: every logic test passed, and the
// bar simply painted over both edges. Kept as a test so it stays fixed.
auto tHighlightDoesNotCoverTheBorder =
    test("ContextMenuRender/highlightDoesNotCoverTheBorder") = []
{
    auto view = MenuTestView {};

    if (!view.build())
        return;

    view.openClipboardMenu();
    view.renderToImage(1.f);

    const auto box = view.menu.boxBounds();

    auto hover = Graphics::MouseEvent {};
    hover.pos = {box.x + 20.f, box.y + 14.f};

    view.menu.mouseMoved(hover);

    const auto image = view.renderToImage(1.f);

    // The single-pixel border columns on the highlighted row hold no highlight.
    const auto leftEdge = Graphics::Rect {box.x, box.y + 12.f, 1.f, 8.f};
    const auto rightEdge =
        Graphics::Rect {box.right() - 1.f, box.y + 12.f, 1.f, 8.f};

    check(countMatching(image, leftEdge, view.theme.menuHighlight) == 0);
    check(countMatching(image, rightEdge, view.theme.menuHighlight) == 0);

    // And the bar is genuinely there between them, so this is not passing
    // because nothing was drawn at all.
    const auto inside = Graphics::Rect {box.x + 4.f, box.y + 12.f, 20.f, 8.f};

    check(countMatching(image, inside, view.theme.menuHighlight) > 0);
};

// A disabled row greys its shortcut as well as its title. The shortcut colour is
// *lighter* than the disabled one, so the row would otherwise print ⌘X more
// brightly than the Cut it belongs to — which is what it did until someone
// looked at one.
auto tDisabledRowGreysItsShortcut =
    test("ContextMenuRender/disabledRowGreysItsShortcut") = []
{
    auto view = MenuTestView {};

    if (!view.build())
        return;

    view.cutEnabled = false;
    view.openClipboardMenu();

    const auto image = view.renderToImage(1.f);
    const auto box = view.menu.boxBounds();

    const auto row = Graphics::Rect {box.x, box.y + 10.f, box.w, 24.f};

    // The shortcut is right-aligned, the title left-aligned, so halves of the
    // row separate them.
    const auto titleHalf = Graphics::Rect {row.x + 2.f, row.y, row.w * 0.5f, row.h};
    const auto shortcutHalf =
        Graphics::Rect {row.x + row.w * 0.5f, row.y, row.w * 0.5f - 2.f, row.h};

    // Neither brighter than the other by any margin that could read as "one of
    // these is greyed and the other is not".
    check(std::abs(peakBrightness(image, titleHalf)
                   - peakBrightness(image, shortcutHalf))
          < 0.06f);
};

// The box grows to fit its widest row rather than to a fixed width, so a menu
// of long titles is not truncated and one of short titles is not padded out.
auto tBoxWidthFollowsContent = test("ContextMenuRender/boxWidthFollowsContent") = []
{
    auto view = MenuTestView {};

    if (!view.build())
        return;

    view.menu.show({anchorX, anchorY}, {"edit.copy"});
    view.renderToImage(1.f);

    const auto narrow = view.menu.boxBounds().w;

    view.registry.add(
        {"edit.longCommandName", "A Command With A Considerably Longer Title"});

    view.menu.show({anchorX, anchorY}, {"edit.longCommandName"});
    view.renderToImage(1.f);

    check(view.menu.boxBounds().w > narrow);
};
