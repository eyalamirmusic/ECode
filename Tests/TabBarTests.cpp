#include <ECodeUI/Chrome.h>
#include <ECodeUI/WidgetHost.h>

#include <NanoTest/NanoTest.h>

// The tab strip as a control: which tab a point is in, what a press arms, what
// a release does with it, and where the strip scrolls when it overflows.
//
// None of this needs a device — it is rectangles and hit tests, which is the
// whole reason the widget layer is separate from the view that draws it.
// TabRenderTests answers the questions pixels have to answer instead.

using namespace nano;
using namespace eacp;
using namespace ecode;

namespace
{
constexpr auto stripWidth = 600.f;
constexpr auto stripHeight = 35.f;

TabItem tab(std::string title)
{
    auto item = TabItem {};
    item.title = std::move(title);

    return item;
}

Vector<TabItem> named(int count)
{
    auto items = Vector<TabItem> {};

    for (auto index = 0; index < count; ++index)
        items.add(tab("file" + std::to_string(index) + ".cpp"));

    return items;
}

Graphics::MouseEvent at(const Graphics::Point& point,
                        Graphics::MouseButton button = Graphics::MouseButton::Left)
{
    auto event = Graphics::MouseEvent {};

    event.pos = point;
    event.button = button;

    return event;
}

Graphics::Point centreOf(const Graphics::Rect& area)
{
    return {area.x + area.w * 0.5f, area.y + area.h * 0.5f};
}

// A strip on its own, sized like the real one.
struct Strip
{
    Strip(int tabCount = 3)
    {
        bar.setBounds({0.f, 0.f, stripWidth, stripHeight});
        bar.setTabs(named(tabCount));

        bar.onTabSelected = [this](int index) { selected = index; };
        bar.onTabClosed = [this](int index) { closed = index; };
    }

    ChromeTheme theme;
    TabBar bar {theme};

    int selected = -1;
    int closed = -1;
};
} // namespace

// Comfortable width while they fit, shared evenly once they do not, and never
// below a floor — past which the strip overflows instead of shrinking further.
auto tWidthsShareTheStrip = test("TabBar/tabsShareTheWidthDownToAFloor") = []
{
    auto few = Strip {2};

    // 600 / 2 is wider than the preferred width, so both stay at it.
    check(few.bar.boundsOfTab(0).w == 180.f);
    check(few.bar.boundsOfTab(1).x == 180.f);

    auto several = Strip {5};

    // 600 / 5 is inside the range, so they share it exactly and tile the strip
    // with no gap and nothing over the end.
    check(several.bar.boundsOfTab(0).w == 120.f);
    check(several.bar.boundsOfTab(4).right() == stripWidth);

    auto many = Strip {20};

    // Below the floor the strip stops shrinking and starts overflowing.
    check(many.bar.boundsOfTab(0).w == 116.f);
    check(many.bar.boundsOfTab(19).right() > stripWidth);
};

auto tHitTestFindsTheTab = test("TabBar/aPointFindsTheTabItIsIn") = []
{
    auto strip = Strip {3};

    check(strip.bar.tabAt({10.f, 10.f}) == 0);
    check(strip.bar.tabAt({190.f, 10.f}) == 1);
    check(strip.bar.tabAt({370.f, 10.f}) == 2);

    // Past the last tab is the bare strip, not the nearest tab.
    check(strip.bar.tabAt({560.f, 10.f}) == -1);
};

// A tab scrolled off the left still *has* a rectangle — boundsOfTab is
// unclipped — and in the window that rectangle reaches back under the sidebar.
// So the strip's own bounds have to be asked first, or a click in the file tree
// switches editors.
//
// Needs a strip that does not start at x = 0 and tabs that overflow it: with
// the origin at zero there is nowhere to the left for a tab to be, and every
// per-tab rectangle rejects the point on its own.
auto tPointsLeftOfTheStripAreNotTabs =
    test("TabBar/aPointLeftOfTheStripIsNotTheTabScrolledUnderIt") = []
{
    auto strip = Strip {20};

    // Where it sits in the window: to the right of an activity bar and sidebar.
    strip.bar.setBounds({288.f, 0.f, stripWidth, stripHeight});
    strip.bar.setActiveTab(19);

    // Scrolled far enough that the first tabs are off the left edge.
    check(strip.bar.boundsOfTab(0).x < 288.f);

    check(strip.bar.tabAt({100.f, 10.f}) == -1);

    // And below the strip is nothing either.
    check(strip.bar.tabAt({400.f, 100.f}) == -1);
};

auto tPressSelects = test("TabBar/pressingATabSelectsIt") = []
{
    auto strip = Strip {3};

    strip.bar.mouseDown(at(centreOf(strip.bar.boundsOfTab(2))));

    check(strip.bar.activeTab() == 2);
    check(strip.selected == 2);
};

// A press on the × must not also switch to the tab. Closing the file you were
// not looking at should not first bring it forward.
auto tPressingCloseDoesNotSelect =
    test("TabBar/pressingTheCloseButtonDoesNotSelect") = []
{
    auto strip = Strip {3};

    strip.bar.mouseDown(at(centreOf(strip.bar.closeBoundsOfTab(2))));

    check(strip.bar.activeTab() == 0);
    check(strip.selected == -1);
};

// The release decides, not the press — the rule the context menu already
// follows, and what lets a press on the wrong × be backed out of.
auto tCloseNeedsTheRelease = test("TabBar/theCloseHappensOnTheRelease") = []
{
    auto strip = Strip {3};

    const auto button = centreOf(strip.bar.closeBoundsOfTab(1));

    strip.bar.mouseDown(at(button));
    check(strip.closed == -1);

    strip.bar.mouseUp(at(button));
    check(strip.closed == 1);
};

auto tReleasingElsewhereCancels =
    test("TabBar/releasingAwayFromTheButtonCancels") = []
{
    auto strip = Strip {3};

    strip.bar.mouseDown(at(centreOf(strip.bar.closeBoundsOfTab(1))));

    // Dragged off the button before letting go.
    strip.bar.mouseUp(at({10.f, 10.f}));

    check(strip.closed == -1);
};

// Middle-click closes wherever on the tab it lands: it was never aimed at the
// button, so requiring the release to be on one would make the gesture fail
// almost every time.
auto tMiddleClickCloses = test("TabBar/middleClickingATabClosesIt") = []
{
    auto strip = Strip {3};

    const auto middle = centreOf(strip.bar.boundsOfTab(2));

    strip.bar.mouseDown(at(middle, Graphics::MouseButton::Middle));

    // And it does not select on the way, either.
    check(strip.selected == -1);

    strip.bar.mouseUp(at(middle, Graphics::MouseButton::Middle));

    check(strip.closed == 2);
};

// The pointer has not moved, but what is under it has: the tab that slid left
// into the closed one's place must not inherit its highlight, and an index past
// the new end must not be kept at all.
auto tSetTabsClearsTheHover =
    test("TabBar/replacingTheTabsForgetsWhatWasHovered") = []
{
    auto strip = Strip {3};

    strip.bar.mouseMoved(at(centreOf(strip.bar.boundsOfTab(2))));

    check(strip.bar.hoveredTab() == 2);

    strip.bar.setTabs(named(1));

    // Kept, it would name a tab that no longer exists — and after a close it
    // would name whichever tab slid left into the closed one's place, lighting
    // a tab the pointer was never on.
    check(strip.bar.hoveredTab() == -1);

    check(strip.bar.activeTab() == 0);
    check(strip.bar.tabCount() == 1);
};

// The only thing that makes an overflowing strip usable from the keyboard: a
// tab switched to by ⌃Tab has to come into view on its own.
auto tActivatingScrollsIntoView =
    test("TabBar/activatingAnOffscreenTabScrollsToIt") = []
{
    auto strip = Strip {20};

    // The last tab starts well past the right edge.
    check(strip.bar.boundsOfTab(19).x > stripWidth);

    strip.bar.setActiveTab(19);

    const auto shown = strip.bar.boundsOfTab(19);

    check(shown.right() <= stripWidth + 0.01f);
    check(shown.x >= 0.f);

    // And back the other way.
    strip.bar.setActiveTab(0);

    check(strip.bar.boundsOfTab(0).x == 0.f);
};

// A strip that fits hands the wheel back rather than swallowing it, so a
// gesture aimed past it is not silently eaten.
auto tWheelOnlyWhenItOverflows =
    test("TabBar/theWheelIsOnlyTakenWhenThereIsOverflow") = []
{
    auto fits = Strip {3};
    auto overflows = Strip {20};

    auto wheel = at({10.f, 10.f});
    wheel.delta = {0.f, -40.f};

    check(!fits.bar.mouseWheel(wheel));
    check(overflows.bar.mouseWheel(wheel));

    // Scrolled left by the delta, and clamped at both ends rather than running
    // off into empty strip.
    check(overflows.bar.boundsOfTab(0).x == -40.f);

    auto far = at({10.f, 10.f});
    far.delta = {0.f, 10000.f};

    overflows.bar.mouseWheel(far);
    check(overflows.bar.boundsOfTab(0).x == 0.f);
};

// The host is what notices a widget has been left, because only it knows what
// was under the pointer a moment ago. Without this a tab lit on the way in
// stays lit for good — mouseMoved never reaches a widget the pointer is not on.
auto tHoverIsHandedOver = test("TabBar/theHostTellsAWidgetThePointerHasLeft") = []
{
    struct Tracked final : Widget
    {
        bool wantsMouse() const override { return true; }
        void mouseExited() override { ++exits; }

        int exits = 0;
    };

    auto root = Widget {};
    auto left = Tracked {};
    auto right = Tracked {};

    root.setBounds({0.f, 0.f, 200.f, 100.f});
    left.setBounds({0.f, 0.f, 100.f, 100.f});
    right.setBounds({100.f, 0.f, 100.f, 100.f});

    root.addChild(left);
    root.addChild(right);

    auto host = WidgetHost {};
    host.setRoot(root);

    host.mouseMoved(at({50.f, 50.f}));
    check(host.hovered() == &left);
    check(left.exits == 0);

    // Moving within the same widget is not a crossing.
    host.mouseMoved(at({60.f, 50.f}));
    check(left.exits == 0);

    host.mouseMoved(at({150.f, 50.f}));
    check(host.hovered() == &right);
    check(left.exits == 1);
    check(right.exits == 0);

    // Off the window entirely, which no move can report.
    host.mouseExited();
    check(host.hovered() == nullptr);
    check(right.exits == 1);
};
