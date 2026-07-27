#include <ECodeUI/Chrome.h>
#include <ECodeUI/EditorWidget.h>
#include <ECodeUI/WidgetHost.h>
#include <ECodeSyntax/SyntaxHighlighter.h>

#include <NanoTest/NanoTest.h>

#include <eacp/GPU/GPU.h>
#include <eacp/Text/Text.h>

#include <cmath>
#include <optional>

// What more than one open file actually puts on screen.
//
// TabBarTests covers the arithmetic — which tab a point is in, what a press
// arms. None of that can say whether a second tab was drawn at all, whether the
// × appears where it should, or whether switching away from a file and back
// gives the same picture. Those are the questions pixels answer, and PLAN.md §9
// is a list of the times they turned out to be different questions from the
// ones the logic tests were asking.

using namespace nano;
using namespace eacp;
using namespace ecode;

namespace
{
constexpr auto viewWidth = 600.f;
constexpr auto viewHeight = 240.f;
constexpr auto tabHeight = 35.f;

// The tab strip alone, across the top of an otherwise empty view.
struct StripTestView final : GPU::GPUView
{
    StripTestView()
    {
        setSampleCount(1);
        setBounds({0.f, 0.f, viewWidth, viewHeight});

        root.addChild(tabs);
        host.setRoot(root);

        root.setBounds({0.f, 0.f, viewWidth, viewHeight});
        tabs.setBounds({0.f, 0.f, viewWidth, tabHeight});
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

    void moveTo(const Graphics::Point& point)
    {
        auto event = Graphics::MouseEvent {};
        event.pos = point;

        host.mouseMoved(event);
    }

    ChromeTheme theme;

    Widget root;
    TabBar tabs {theme};

    WidgetHost host;

    OwningPointer<Text::GlyphAtlas> atlas;
    std::optional<Text::GlyphRenderer> glyphs;
};

// An editor over a workspace, wired the way Main.cpp wires it: the widget is
// pointed at whichever file the workspace made active, and nothing else.
struct SwitchTestView final : GPU::GPUView
{
    SwitchTestView()
    {
        setSampleCount(1);
        setBounds({0.f, 0.f, viewWidth, viewHeight});

        workspace.onChanged = [this] { editor.setFile(workspace.active()); };

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

    // A second untitled buffer holding this text, activated. Avoids the
    // filesystem: what is under test is the switch, not the reading.
    void addBuffer(std::string_view text)
    {
        auto& entry = workspace.addUntitled();

        entry.file.editor().setDocument(Document::fromText(std::string {text}));
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

    Workspace workspace {[]
                         {
                             auto syntax = makeOwned<SyntaxHighlighter>();

                             if (!syntax->isValid())
                                 return OwningPointer<Highlighter> {};

                             return OwningPointer<Highlighter> {std::move(syntax)};
                         }};

    Widget root;
    EditorWidget editor {workspace.active()};

    WidgetHost host;

    OwningPointer<Text::GlyphAtlas> atlas;
    std::optional<TextRenderer> renderer;
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

// The brightest pixel across a band, as a stand-in for "is a line drawn here".
// A peak rather than a named pixel, because sub-pixel placement means the lit
// column can be either side of a half-pixel — PLAN.md §9 has the three tests
// that failed against correct code for exactly that.
float peakIn(const Graphics::Image& image, const Graphics::Rect& area)
{
    auto peak = 0.f;

    for (auto y = static_cast<int>(area.y); y < static_cast<int>(area.bottom()); ++y)
        for (auto x = static_cast<int>(area.x); x < static_cast<int>(area.right());
             ++x)
        {
            const auto pixel = image.at(x, y);

            peak = std::max(peak, pixel.r + pixel.g + pixel.b);
        }

    return peak;
}

int differingPixels(const Graphics::Image& a, const Graphics::Image& b)
{
    if (a.width() != b.width() || a.height() != b.height())
        return -1;

    auto total = 0;

    for (auto y = 0; y < a.height(); ++y)
        for (auto x = 0; x < a.width(); ++x)
            if (!isColor(a, x, y, b.at(x, y)))
                ++total;

    return total;
}

Vector<TabItem> named(int count)
{
    auto items = Vector<TabItem> {};

    for (auto index = 0; index < count; ++index)
    {
        auto item = TabItem {};
        item.title = "file" + std::to_string(index) + ".cpp";

        items.add(std::move(item));
    }

    return items;
}

// A long enough document that it can be scrolled past a screenful.
std::string longText()
{
    auto text = std::string {};

    for (auto line = 0; line < 200; ++line)
        text += "int value" + std::to_string(line) + " = " + std::to_string(line)
                + ";\n";

    return text;
}
} // namespace

// Three tabs, three names, side by side. The strip drew one hardcoded tab
// before this; that it now draws several is the whole feature, and nothing
// short of the image says so.
auto tSeveralTabsAreDrawn = test("TabRender/everyOpenFileGetsATab") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = StripTestView {};

    if (!view.build())
        return;

    view.tabs.setTabs(named(3));

    const auto image = view.renderToImage(1.f);

    if (!image.isValid())
        return;

    // A title in each of the three slots, not just the first.
    for (auto index = 0; index < 3; ++index)
    {
        const auto area = view.tabs.boundsOfTab(index);
        const auto background =
            index == 0 ? view.theme.activeTab : view.theme.tabBar;

        check(inkIn(image, area.inset(4.f, 4.f), background) > 0);
    }
};

// The active tab has to be tellable from the others without counting. Asserted
// against the second tab as well, so this cannot pass by finding the accent on
// a strip where every tab is drawn identically.
auto tActiveTabIsDistinct = test("TabRender/theActiveTabIsFilledDifferently") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = StripTestView {};

    if (!view.build())
        return;

    view.tabs.setTabs(named(3));
    view.tabs.setActiveTab(1);

    const auto image = view.renderToImage(1.f);

    if (!image.isValid())
        return;

    const auto activeArea = view.tabs.boundsOfTab(1);
    const auto idleArea = view.tabs.boundsOfTab(2);

    // Sampled low in each tab, clear of the accent strip along the top and of
    // the title text.
    const auto row = static_cast<int>(tabHeight) - 3;

    check(isColor(image,
                  static_cast<int>(activeArea.right()) - 30,
                  row,
                  view.theme.activeTab));
    check(isColor(
        image, static_cast<int>(idleArea.right()) - 30, row, view.theme.tabBar));
};

// Two inactive tabs share a fill with the strip behind them, so without a rule
// between them a pair reads as one wide tab with two names in it.
auto tSeparatorsDivideTabs = test("TabRender/aRuleIsDrawnBetweenTwoTabs") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = StripTestView {};

    if (!view.build())
        return;

    view.tabs.setTabs(named(3));

    // Tab 1 rather than tab 0, so both sides of the seam are inactive and the
    // active tab's fill cannot supply the difference.
    view.tabs.setActiveTab(0);

    const auto image = view.renderToImage(1.f);

    if (!image.isValid())
        return;

    const auto seam = view.tabs.boundsOfTab(1).right();

    // Well below the title's baseline and clear of the close button's slot, so
    // nothing else bright lives in either band — PLAN.md §9 on the separator
    // test that passed with the rule deleted because the border supplied the
    // peak.
    const auto band = Graphics::Rect {seam - 2.f, tabHeight - 6.f, 4.f, 4.f};
    const auto bare = Graphics::Rect {seam - 40.f, tabHeight - 6.f, 4.f, 4.f};

    check(peakIn(image, band) > peakIn(image, bare) + 0.02f);
};

// The × is on the tab being worked in and on the one under the pointer, and
// nowhere else: a × on every tab turns a row of filenames into a row of
// buttons.
auto tCloseButtonFollowsTheState =
    test("TabRender/theCloseButtonIsDrawnWhereItShouldBe") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = StripTestView {};

    if (!view.build())
        return;

    view.tabs.setTabs(named(3));
    view.tabs.setActiveTab(0);

    const auto image = view.renderToImage(1.f);

    if (!image.isValid())
        return;

    const auto onActive = view.tabs.closeBoundsOfTab(0);
    const auto onIdle = view.tabs.closeBoundsOfTab(2);

    check(inkIn(image, onActive, view.theme.activeTab) > 0);
    check(inkIn(image, onIdle, view.theme.tabBar) == 0);
};

// Hovering an inactive tab has to bring its × up, which is the only way to
// close a tab you are not in without middle-clicking it.
auto tHoverRevealsTheCloseButton =
    test("TabRender/hoveringAnIdleTabRevealsItsClose") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = StripTestView {};

    if (!view.build())
        return;

    view.tabs.setTabs(named(3));
    view.tabs.setActiveTab(0);

    const auto idle = view.tabs.boundsOfTab(2);
    const auto button = view.tabs.closeBoundsOfTab(2);

    const auto before = view.renderToImage(1.f);

    // Onto the tab but not onto its button, so what changes is the × appearing
    // rather than the button's own highlight.
    view.moveTo({idle.x + 12.f, idle.y + idle.h * 0.5f});

    const auto after = view.renderToImage(1.f);

    if (!before.isValid() || !after.isValid())
        return;

    check(inkIn(before, button, view.theme.tabBar) == 0);
    check(inkIn(after, button, view.theme.tabBar) > 0);

    // And the pointer leaving the strip puts it back. Nothing but the host's
    // exit can do this: mouseMoved never reaches a widget the pointer is off.
    view.host.mouseExited();

    const auto gone = view.renderToImage(1.f);

    if (!gone.isValid())
        return;

    check(differingPixels(gone, before) == 0);

    // Both frames having drawn something, so the comparison above cannot be two
    // blank images agreeing.
    check(inkIn(before, view.tabs.boundsOfTab(2), view.theme.tabBar) > 0);
};

// A title too long for its tab must not reach into the close button's slot.
//
// Clipping alone does not give this: the tab's own clip stops the text at the
// tab's edge, which is *past* the ×, so a long name draws straight through it.
// Asserted by comparing the button's box against the same tab with a short
// title — the × is the only thing in that box in both, so any difference is the
// title having arrived there.
auto tALongTitleStaysOutOfTheCloseButton =
    test("TabRender/aLongTitleDoesNotRunUnderTheCloseButton") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto brief = StripTestView {};
    auto verbose = StripTestView {};

    if (!brief.build() || !verbose.build())
        return;

    auto shortName = TabItem {};
    shortName.title = "a.h";

    auto longName = TabItem {};
    longName.title = "a-file-whose-name-is-far-too-long-for-any-tab.cpp";

    brief.tabs.setTabs({shortName});
    verbose.tabs.setTabs({longName});

    const auto briefImage = brief.renderToImage(1.f);
    const auto verboseImage = verbose.renderToImage(1.f);

    if (!briefImage.isValid() || !verboseImage.isValid())
        return;

    const auto button = brief.tabs.closeBoundsOfTab(0);

    auto differs = 0;

    for (auto y = static_cast<int>(button.y); y < static_cast<int>(button.bottom());
         ++y)
        for (auto x = static_cast<int>(button.x);
             x < static_cast<int>(button.right());
             ++x)
            if (!isColor(verboseImage, x, y, briefImage.at(x, y)))
                ++differs;

    check(differs == 0);

    // And the × really is in there, so the comparison above is not two empty
    // boxes agreeing.
    check(inkIn(briefImage, button, brief.theme.activeTab) > 0);

    // The long name did get drawn — elided, not dropped.
    const auto label = Graphics::Rect {4.f, 4.f, button.x - 8.f, tabHeight - 8.f};

    check(inkIn(verboseImage, label, verbose.theme.activeTab) > 0);
};

// Switching away from a file and back gives the same picture, which is the
// whole promise of a tab: the text, the caret, the colours and the scroll
// offset all come back as they were.
auto tSwitchingBackRestoresTheView =
    test("TabRender/switchingAwayAndBackRedrawsTheSame") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = SwitchTestView {};

    if (!view.build())
        return;

    // The first tab is the untitled one the workspace starts with.
    view.workspace.active().file.editor().setDocument(
        Document::fromText(longText()));

    view.editor.setFile(view.workspace.active());

    // Scrolled well down, so a lost offset shows as a different picture rather
    // than as nothing at all.
    auto wheel = Graphics::MouseEvent {};
    wheel.pos = {100.f, 100.f};
    wheel.delta = {0.f, -700.f};
    wheel.preciseScrolling = true;

    view.editor.mouseWheel(wheel);

    const auto scrolled = view.editor.scrollOffset();

    check(scrolled.y < -100.f);

    const auto before = view.renderToImage(1.f);

    view.addBuffer("void other() {}\n");

    const auto elsewhere = view.renderToImage(1.f);

    view.workspace.activate(0);

    const auto after = view.renderToImage(1.f);

    if (!before.isValid() || !elsewhere.isValid() || !after.isValid())
        return;

    check(view.editor.scrollOffset() == scrolled);

    // Identical to the frame it left, and different from the file it went to —
    // without the second half, two blank frames would satisfy the first.
    check(differingPixels(after, before) == 0);
    check(differingPixels(elsewhere, before) > 0);

    check(inkIn(before, {0.f, 0.f, viewWidth, viewHeight}, view.textTheme.background)
          > 0);
};

// Each file keeps its own tree, so a switch costs no reparse — and, more to the
// point, the colours that come back are the ones that file had rather than the
// ones the other file left behind.
auto tEachFileKeepsItsColours =
    test("TabRender/eachFileIsColouredByItsOwnHighlighter") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = SwitchTestView {};

    if (!view.build())
        return;

    // Two documents with the same shape and different keywords, so a stale
    // highlighter shows up as the wrong word being coloured rather than as no
    // colour at all.
    view.workspace.active().file.editor().setDocument(
        Document::fromText(std::string {"struct Alpha { int x; };\n"}));

    view.editor.setFile(view.workspace.active());

    const auto first = view.renderToImage(1.f);

    view.addBuffer("struct Alpha { int x; };\n");

    const auto second = view.renderToImage(1.f);

    if (!first.isValid() || !second.isValid())
        return;

    // Same text through two separate highlighters must come out identical. A
    // shared one that was never told about the second document would colour it
    // from the first document's tree, and the two would differ.
    check(differingPixels(first, second) == 0);

    check(inkIn(first, {0.f, 0.f, viewWidth, 40.f}, view.textTheme.background) > 0);
};
