#include <ECodeWorkbench/EditorGroupView.h>
#include <ECodeWidgets/WidgetHost.h>
#include <ECodeCore/EditorGroups.h>
#include <ECodeSyntax/SyntaxHighlighter.h>

#include <NanoTest/NanoTest.h>

#include <eacp/GPU/GPU.h>
#include <eacp/Text/Text.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

// What two editor groups actually put on screen.
//
// EditorGroupTests covers the arrangement — which group holds what, what a move
// does to the indices. None of it can say whether a second pane was drawn at
// all, whether one pane's text stays inside it, or whether the strips say which
// pane the keyboard is in. Those are the questions pixels answer, and PLAN.md §9
// is a list of the times they turned out to be different questions.
//
// And one question pixels cannot answer either: each pane keeps its own
// laid-out rows, which is an *absence* of work. A cache thrashing between two
// panes draws exactly what one that never thrashes draws, so that one reads the
// counters — the pair is what makes it real.

using namespace nano;
using namespace eacp;
using namespace ecode;

namespace
{
constexpr auto viewWidth = 800.f;
constexpr auto viewHeight = 300.f;

// The editor groups laid out side by side, which is the part of Main.cpp's
// WindowLayout that groups are about: one pane each, sharing the width evenly,
// remade when the number of groups changes.
struct GroupsTestView final : GPU::GPUView
{
    GroupsTestView()
    {
        setSampleCount(1);
        setBounds({0.f, 0.f, viewWidth, viewHeight});

        host.setRoot(root);

        groups.onGroupsChanged = [this] { rebuild(); };
        groups.onChanged = [this] { refresh(); };

        rebuild();
    }

    void rebuild()
    {
        // Cleared while the panes are still there to be told, exactly as
        // Main.cpp does it: dropping a hover afterwards would say so to freed
        // memory.
        host.forgetTargets();

        views.clear();
        root.removeAllChildren();

        for (auto index = 0; index < groups.count(); ++index)
            root.addChild(views.createNew(theme, groups.at(index)));

        layout();

        if (atlas)
            setAtlas();

        refresh();
    }

    void setAtlas()
    {
        for (auto& view: views)
            view->setAtlas(atlas.get(), textTheme, 1.f);
    }

    void refresh()
    {
        for (auto index = 0; index < views.size(); ++index)
        {
            views[index]->refresh();
            views[index]->setGroupActive(index == groups.activeIndex());
        }
    }

    void layout()
    {
        root.setBounds({0.f, 0.f, viewWidth, viewHeight});

        const auto width = viewWidth / static_cast<float>(std::max(1, views.size()));

        for (auto index = 0; index < views.size(); ++index)
            views[index]->setBounds(
                {static_cast<float>(index) * width, 0.f, width, viewHeight});
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

        glyphs.emplace();
        glyphs->setViewportSize({viewWidth, viewHeight});

        setAtlas();

        return true;
    }

    // Puts text into whichever file a group is showing. Avoids the filesystem:
    // what is under test is the drawing, not the reading.
    void setText(int group, std::string_view text)
    {
        groups.at(group).editor().setDocument(
            Document::fromText(std::string {text}));
    }

    void render(GPU::Frame& frame) override
    {
        auto pass = frame.beginPass({textTheme.background});

        if (!atlas || !glyphs)
            return;

        host.prepare(*atlas);
        atlas->commit();

        auto sprites =
            Sprites::SpriteRenderer {{viewWidth, viewHeight}, sampleCount()};

        auto context = PaintContext {
            pass, sprites, *glyphs, *atlas, {0.f, 0.f, viewWidth, viewHeight}, 1.f};

        host.paint(context);
    }

    const RowCache& rowsOf(int group) const
    {
        return views[group]->textRenderer()->rows();
    }

    float rowHeightOf(int group) const
    {
        return views[group]->textRenderer()->rowHeight();
    }

    // A pane's text area with the *first* row left out.
    //
    // The row a caret is on is filled edge to edge with the current-line band,
    // so a region that includes it answers "is this pane drawing its own
    // furniture" rather than the question being asked. PLAN.md §9 — a region
    // assertion is only as good as its region, and both tests below had
    // something bright already living in theirs. The left inset clears the
    // line-number gutter for the same reason.
    Graphics::Rect belowTheCaretRow(int group) const
    {
        const auto pane = paneOf(group);
        const auto top =
            pane.y + EditorGroupView::tabBarHeight + rowHeightOf(group) * 1.5f;

        return {pane.x + 80.f, top, pane.w - 80.f, pane.bottom() - top - 2.f};
    }

    Graphics::Rect paneOf(int group) const { return views[group]->bounds(); }

    ChromeTheme theme;
    TextTheme textTheme;

    EditorGroups groups {[]
                         {
                             auto syntax = makeOwned<SyntaxHighlighter>();

                             if (!syntax->isValid())
                                 return OwningPointer<Highlighter> {};

                             return OwningPointer<Highlighter> {std::move(syntax)};
                         }};

    Widget root;
    OwnedVector<EditorGroupView> views;

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

// Blue *minus* red across a band, not brightness. PLAN.md §9: the accent and the
// muted accent differ by hue, and antialiasing turns every colour into a ramp
// towards the background — so a single-channel threshold reads a point on that
// ramp rather than the colour that was drawn.
float bluestIn(const Graphics::Image& image, const Graphics::Rect& area)
{
    auto peak = -1.f;

    for (auto y = static_cast<int>(area.y); y < static_cast<int>(area.bottom()); ++y)
        for (auto x = static_cast<int>(area.x); x < static_cast<int>(area.right());
             ++x)
        {
            const auto pixel = image.at(x, y);

            peak = std::max(peak, pixel.b - pixel.r);
        }

    return peak;
}

std::string lines(std::string_view text, int count)
{
    auto out = std::string {};

    for (auto index = 0; index < count; ++index)
    {
        out += text;
        out += '\n';
    }

    return out;
}
} // namespace

// The first question, and the one everything else assumes: is there a second
// pane on screen at all, showing something different from the first?
auto tBothPanesDraw = test("EditorGroupRender/twoPanesDrawTheirOwnFiles") = []
{
    auto view = GroupsTestView {};

    if (!view.build())
        return;

    view.groups.split();
    view.setText(0, lines("AAAA", 6));
    view.setText(1, lines("BBBB", 6));

    const auto image = view.renderToImage(1.f);

    const auto left = view.paneOf(0);
    const auto right = view.paneOf(1);

    check(inkIn(image, left, view.textTheme.background) > 0);
    check(inkIn(image, right, view.textTheme.background) > 0);

    // Different files, so the two halves cannot be the same picture. Compared
    // against each other rather than each against an expectation: what is under
    // test is that the panes are independent, not what either of them says.
    auto differences = 0;

    for (auto y = 0; y < static_cast<int>(viewHeight); ++y)
        for (auto x = 0; x < static_cast<int>(left.w); ++x)
            if (!isColor(image,
                         x + static_cast<int>(right.x),
                         y,
                         image.at(x + static_cast<int>(left.x), y)))
                ++differences;

    check(differences > 0);
};

// PLAN.md §7.8's note, made into a test. Two panes over one RowCache would find
// the stamp wrong on every frame and lay every visible row out again — and draw
// exactly the same picture doing it. Only the counters can see it.
auto tEachPaneKeepsItsOwnRows =
    test("EditorGroupRender/eachPaneKeepsItsOwnLaidOutRows") = []
{
    auto view = GroupsTestView {};

    if (!view.build())
        return;

    view.groups.split();
    view.setText(0, lines("left side", 40));
    view.setText(1, lines("right side", 40));

    // Two frames to warm up: the first lays every visible row out, the second
    // settles the highlighter's spans. What is under test is the third.
    view.renderToImage(1.f);
    view.renderToImage(1.f);

    const auto leftAfterWarmUp = view.rowsOf(0).layouts();
    const auto rightAfterWarmUp = view.rowsOf(1).layouts();

    check(leftAfterWarmUp > 0);
    check(rightAfterWarmUp > 0);

    view.renderToImage(1.f);

    // Nothing changed, so nothing is laid out again — in either pane. Sharing a
    // cache would cost a screenful of layouts per pane per frame.
    check(view.rowsOf(0).layouts() == leftAfterWarmUp);
    check(view.rowsOf(1).layouts() == rightAfterWarmUp);

    // And neither pane has thrown the other's rows away.
    check(view.rowsOf(0).rowsHeld() > 0);
    check(view.rowsOf(1).rowsHeld() > 0);
};

// A pane's text stops at the pane. The case that lands on the fold — PLAN.md §9
// — is a line far longer than the pane it is in, because a line that fits is
// inside its clip whether the clip narrows or is simply ignored.
auto tTextStaysInsideItsPane =
    test("EditorGroupRender/aLongLineStopsAtItsOwnPane") = []
{
    auto view = GroupsTestView {};

    if (!view.build())
        return;

    view.groups.split();

    // Lines wider than the whole window, and none of them on row 0: the row the
    // right pane's own caret sits on is the one region that cannot answer this,
    // so the overflow has to be somewhere else to be looked for.
    view.setText(0, "\n" + lines(std::string(400, 'X'), 12));

    // Deliberately empty, so anything found on the right came from the left.
    view.setText(1, lines("", 12));

    const auto image = view.renderToImage(1.f);

    check(inkIn(image, view.paneOf(0), view.textTheme.background) > 0);
    check(inkIn(image, view.belowTheCaretRow(1), view.textTheme.background) == 0);
};

// Which pane the keyboard is in, said on the strips. Split the window and every
// pane has an active tab; without this, every pane claims to be the one being
// worked in.
auto tOnlyTheActiveGroupIsAccented =
    test("EditorGroupRender/onlyTheActiveGroupsTabIsAccented") = []
{
    auto view = GroupsTestView {};

    if (!view.build())
        return;

    view.groups.split();
    view.setText(0, lines("left", 4));
    view.setText(1, lines("right", 4));

    view.groups.activate(0);

    const auto image = view.renderToImage(1.f);

    // The accent is a two-point band along the top of the active tab. Sampled a
    // little into each pane, clear of the very first column.
    const auto band = [](const Graphics::Rect& pane)
    { return Graphics::Rect {pane.x + 8.f, pane.y, 60.f, 3.f}; };

    const auto active = bluestIn(image, band(view.paneOf(0)));
    const auto inactive = bluestIn(image, band(view.paneOf(1)));

    // 0.85 − 0.35 against 0.40 − 0.32: half a unit apart, which no amount of
    // 8-bit rounding closes. PLAN.md §9 on demanding a margin the effect clears.
    check(active > 0.3f);
    check(inactive < 0.1f);

    // And it follows the focus rather than being fixed to the first pane.
    view.groups.activate(1);

    const auto moved = view.renderToImage(1.f);

    check(bluestIn(moved, band(view.paneOf(1))) > 0.3f);
    check(bluestIn(moved, band(view.paneOf(0))) < 0.1f);
};

// Closing the last file in a pane closes the pane, and what is left has to take
// the whole width back — not leave a column of background where it was.
auto tClosingAPaneGivesTheWidthBack =
    test("EditorGroupRender/closingAPaneReturnsItsWidth") = []
{
    auto view = GroupsTestView {};

    if (!view.build())
        return;

    view.setText(0, lines("kept", 40));

    view.groups.split();
    view.setText(1, lines("going", 40));

    check(view.groups.count() == 2);

    view.renderToImage(1.f);

    view.groups.closeFileDiscarding(1, 0);

    check(view.groups.count() == 1);

    const auto image = view.renderToImage(1.f);

    check(near(view.paneOf(0).w, viewWidth));

    // The surviving file is drawn across the whole window, including the half
    // the closed pane used to hold.
    const auto farSide = Graphics::Rect {viewWidth * 0.5f,
                                         EditorGroupView::tabBarHeight,
                                         viewWidth * 0.5f,
                                         viewHeight - EditorGroupView::tabBarHeight};

    check(inkIn(image, farSide, view.textTheme.background) > 0);
};

// Moving a file across is the gesture that fills a split, and the pixels are
// what say it arrived: the pane it left has to stop drawing it and the pane it
// went to has to start.
auto tMovingAFileMovesWhatIsDrawn =
    test("EditorGroupRender/movingAFileMovesWhatIsDrawn") = []
{
    auto view = GroupsTestView {};

    if (!view.build())
        return;

    view.setText(0, lines("stays here", 30));

    view.groups.active().addUntitled();
    view.setText(0, lines("travels", 30));

    check(view.groups.active().count() == 2);

    view.groups.split();
    view.groups.activate(0);
    view.groups.at(0).activate(1);

    const auto before = view.renderToImage(1.f);
    const auto rightText = view.belowTheCaretRow(1);

    // The new pane holds an empty untitled buffer, so its text area is bare.
    check(inkIn(before, rightText, view.textTheme.background) == 0);

    check(view.groups.moveActiveFile(1));

    const auto after = view.renderToImage(1.f);

    check(inkIn(after, rightText, view.textTheme.background) > 0);
};
