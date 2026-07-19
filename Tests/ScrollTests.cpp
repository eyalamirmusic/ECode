#include <ECodeUI/ListView.h>
#include <ECodeUI/ScrollView.h>
#include <ECodeUI/WidgetHost.h>

#include <NanoTest/NanoTest.h>

// Scrolling, and the virtualisation that makes a long list affordable.
//
// All of it is arithmetic over rectangles, so none of it needs a device. The
// two things worth aiming at: that a list only touches the rows on screen —
// the property the whole one-GPUView architecture was chosen for — and that
// the thumb and the content agree about where they are, which is the bug that
// makes a scrollbar feel almost right.

using namespace nano;
using namespace ecode;
using namespace eacp;

namespace
{
constexpr auto testRowHeight = 20.f;
constexpr auto viewportHeight = 100.f;

// A list that records which rows it was asked to draw.
struct CountingList final : ListView
{
    CountingList()
    {
        setRowHeight(testRowHeight);

        paintRow = [this](PaintContext&, std::size_t row, const Graphics::Rect&, bool)
        { painted.push_back(row); };
    }

    mutable Vector<std::size_t> painted;
};

Graphics::MouseEvent mouseAt(float x, float y)
{
    auto event = Graphics::MouseEvent {};
    event.pos = Graphics::Point {x, y};

    return event;
}

Graphics::MouseEvent wheelBy(float x, float y, float delta)
{
    auto event = mouseAt(x, y);

    event.type = Graphics::MouseEventType::Wheel;
    event.preciseScrolling = true;
    event.delta = Graphics::Point {0.f, delta};

    return event;
}
} // namespace

// --- virtualisation ---------------------------------------------------------

// The claim the architecture rests on, stated as a count: rows off screen are
// not touched. Written against a list far longer than the viewport, because a
// list that fits is exactly the case that cannot tell the difference.
auto tOnlyVisibleRowsAreDrawn = test("ListView/onlyTheVisibleRowsAreDrawn") = []
{
    auto list = CountingList {};

    list.setRowCount(5000);

    // Laid out at full height, as a ScrollView would: 100,000 points of rows.
    list.setBounds({0.f, 0.f, 200.f, list.preferredHeight(200.f)});

    // The visible band is the first 100 points of it.
    const auto visible = Graphics::Rect {0.f, 0.f, 200.f, viewportHeight};

    check(list.firstRowIn(visible) == 0);

    // Five rows fit; the sixth is the partly-visible one at the edge.
    check(list.lastRowIn(visible) == 5);
};

// Scrolled down, the list is positioned *above* its parent — a negative y —
// and the visible band is a slice out of its middle.
auto tScrolledListDrawsTheMiddle = test("ListView/aScrolledListDrawsItsMiddleRows") = []
{
    auto list = CountingList {};

    list.setRowCount(1000);

    // Scrolled by 500 points, so the list's top is 500 above the viewport.
    list.setBounds({0.f, -500.f, 200.f, list.preferredHeight(200.f)});

    const auto visible = Graphics::Rect {0.f, 0.f, 200.f, viewportHeight};

    check(list.firstRowIn(visible) == 25);
    check(list.lastRowIn(visible) == 30);
};

// A band entirely above the list draws nothing.
//
// Worth being precise about what this does *not* prove: the negative-row
// conversion inside is undefined behaviour that arm64 saturates to zero, so
// this passes with the signed intermediate removed. It pins the empty range,
// not the clamp. The clamp that is genuinely load-bearing is the one on
// lastRowIn below, which is what keeps an out-of-range row from reaching the
// owner's paint callback.
auto tNegativeRowsAreClamped = test("ListView/aBandAboveTheListDrawsNothing") = []
{
    auto list = CountingList {};

    list.setRowCount(10);
    list.setBounds({0.f, 200.f, 200.f, list.preferredHeight(200.f)});

    // A band entirely above the list.
    const auto above = Graphics::Rect {0.f, 0.f, 200.f, 50.f};

    check(list.firstRowIn(above) == 0);
    check(list.lastRowIn(above) == 0);
};

// Past the end the range stops at the row count, rather than at whatever the
// arithmetic produced. This is the clamp that costs something to lose: the
// paint loop would hand rows that do not exist to the owner's callback, which
// indexes its own model with them.
auto tRowsPastTheEndAreClamped = test("ListView/rowsBelowTheListClampToTheCount") = []
{
    auto list = CountingList {};

    list.setRowCount(10);
    list.setBounds({0.f, 0.f, 200.f, list.preferredHeight(200.f)});

    const auto beyond = Graphics::Rect {0.f, 0.f, 200.f, 100000.f};

    check(list.lastRowIn(beyond) == 10);
};

auto tRowBoundsTile = test("ListView/rowBoundsTileWithoutGaps") = []
{
    auto list = CountingList {};

    list.setRowCount(3);
    list.setBounds({10.f, 40.f, 200.f, list.preferredHeight(200.f)});

    check(list.boundsOfRow(0).y == 40.f);
    check(list.boundsOfRow(1).y == 60.f);
    check(list.boundsOfRow(0).bottom() == list.boundsOfRow(1).y);
    check(list.boundsOfRow(0).x == 10.f);
};

// Clicking empty space below a short list must select nothing rather than the
// row the arithmetic would have put there.
auto tClicksBelowTheRowsMiss = test("ListView/clicksBelowTheLastRowSelectNothing") = []
{
    auto list = CountingList {};

    list.setRowCount(2);
    list.setBounds({0.f, 0.f, 200.f, 400.f});

    check(list.rowAt({50.f, 10.f}) == 0);
    check(list.rowAt({50.f, 30.f}) == 1);
    check(list.rowAt({50.f, 300.f}) == -1);
};

// --- ScrollView -------------------------------------------------------------

auto tContentIsOffsetByTheScroll = test("ScrollView/contentIsOffsetByTheScrollPosition") = []
{
    auto theme = ChromeTheme {};
    auto view = ScrollView {theme};
    auto list = CountingList {};

    list.setRowCount(100);
    view.setContent(list);
    view.setBounds({0.f, 0.f, 200.f, viewportHeight});

    // Unscrolled, the content's top is the viewport's top.
    check(list.bounds().y == 0.f);

    view.setScrollPosition(300.f);

    // Scrolled, it is that far above — which is what the clip then cuts.
    check(list.bounds().y == -300.f);
};

auto tScrollIsClamped = test("ScrollView/scrollingStopsAtBothEnds") = []
{
    auto theme = ChromeTheme {};
    auto view = ScrollView {theme};
    auto list = CountingList {};

    list.setRowCount(10);   // 200 points of content in a 100-point viewport
    view.setContent(list);
    view.setBounds({0.f, 0.f, 200.f, viewportHeight});

    check(view.maxScroll() == 100.f);

    view.setScrollPosition(-50.f);
    check(view.scrollPosition() == 0.f);

    view.setScrollPosition(10000.f);
    check(view.scrollPosition() == 100.f);
};

// Content shorter than the viewport does not scroll, and must not report a
// range that would let a scrollbar appear.
auto tShortContentDoesNotScroll = test("ScrollView/contentThatFitsDoesNotScroll") = []
{
    auto theme = ChromeTheme {};
    auto view = ScrollView {theme};
    auto list = CountingList {};

    list.setRowCount(2);   // 40 points in a 100-point viewport
    view.setContent(list);
    view.setBounds({0.f, 0.f, 200.f, viewportHeight});

    check(view.maxScroll() == 0.f);

    view.setScrollPosition(50.f);
    check(view.scrollPosition() == 0.f);
};

// A list that shrinks under a scrolled view must not leave the view pointing
// past the new end.
auto tShrinkingContentReclampsScroll =
    test("ScrollView/shrinkingTheContentPullsTheScrollBack") = []
{
    auto theme = ChromeTheme {};
    auto view = ScrollView {theme};
    auto list = CountingList {};

    list.setRowCount(100);
    view.setContent(list);
    view.setBounds({0.f, 0.f, 200.f, viewportHeight});
    view.setScrollPosition(1000.f);

    check(view.scrollPosition() > 0.f);

    list.setRowCount(2);
    view.layout();

    check(view.scrollPosition() == 0.f);
};

// Positive wheel y means the content moves *down*, so the position decreases.
// Getting this backwards is invisible in the arithmetic and obvious in the
// hand; the platform has already applied the natural-scroll preference.
auto tWheelDirection = test("ScrollView/wheelDownMovesTowardsTheEnd") = []
{
    auto theme = ChromeTheme {};
    auto view = ScrollView {theme};
    auto list = CountingList {};
    auto host = WidgetHost {};

    list.setRowCount(100);
    view.setContent(list);

    host.setRoot(view);
    view.setBounds({0.f, 0.f, 200.f, viewportHeight});
    view.setScrollPosition(200.f);

    // Negative delta scrolls further into the document.
    host.mouseWheel(wheelBy(50.f, 50.f, -30.f));
    check(view.scrollPosition() == 230.f);

    host.mouseWheel(wheelBy(50.f, 50.f, 30.f));
    check(view.scrollPosition() == 200.f);
};

// The list does not handle the wheel, so it has to reach the ScrollView by
// bubbling — the routing rule that lets a list live inside a scrolling panel.
auto tWheelReachesTheViewThroughTheList =
    test("ScrollView/wheelOverTheListReachesTheScrollView") = []
{
    auto theme = ChromeTheme {};
    auto view = ScrollView {theme};
    auto list = CountingList {};
    auto host = WidgetHost {};

    list.setRowCount(100);
    view.setContent(list);

    host.setRoot(view);
    view.setBounds({0.f, 0.f, 200.f, viewportHeight});

    // The pointer is over the list, which is what widgetAt returns.
    check(host.mouseWheel(wheelBy(50.f, 50.f, -40.f)));
    check(view.scrollPosition() == 40.f);
};

auto tScrollToShowMovesTheLeastItCan =
    test("ScrollView/scrollToShowMovesAsLittleAsPossible") = []
{
    auto theme = ChromeTheme {};
    auto view = ScrollView {theme};
    auto list = CountingList {};

    list.setRowCount(100);
    view.setContent(list);
    view.setBounds({0.f, 0.f, 200.f, viewportHeight});
    view.setScrollPosition(200.f);

    // Already on screen: nothing moves.
    view.scrollToShow(220.f, testRowHeight);
    check(view.scrollPosition() == 200.f);

    // Above the viewport: its top comes to the top.
    view.scrollToShow(100.f, testRowHeight);
    check(view.scrollPosition() == 100.f);

    // Below it: its bottom comes to the bottom, not its top to the top.
    view.scrollToShow(300.f, testRowHeight);
    check(view.scrollPosition() == 300.f + testRowHeight - viewportHeight);
};

// --- ScrollBar --------------------------------------------------------------

// The thumb is a size indicator as well as a position one: half the content on
// screen is half a track of thumb.
auto tThumbIsProportional = test("ScrollBar/theThumbSizeShowsHowMuchIsVisible") = []
{
    auto theme = ChromeTheme {};
    auto bar = ScrollBar {theme};

    bar.setBounds({0.f, 0.f, 10.f, 200.f});
    bar.setRange(400.f, 200.f);

    check(bar.thumbBounds().h == 100.f);

    bar.setRange(800.f, 200.f);
    check(bar.thumbBounds().h == 50.f);
};

// At either end the thumb sits flush, which is the only readout a person
// actually reads off a scrollbar.
auto tThumbReachesBothEnds = test("ScrollBar/theThumbReachesBothEndsOfTheTrack") = []
{
    auto theme = ChromeTheme {};
    auto bar = ScrollBar {theme};

    bar.setBounds({0.f, 0.f, 10.f, 200.f});
    bar.setRange(400.f, 200.f);

    bar.setPosition(0.f);
    check(bar.thumbBounds().y == 0.f);

    bar.setPosition(bar.maxScroll());
    check(bar.thumbBounds().bottom() == 200.f);
};

// Dragging the thumb by a distance moves the content by the *proportional*
// distance, not the same one. The conversion has to divide by the thumb's
// travel rather than by the track's length: with a 100-point thumb in a
// 200-point track, 100 points of travel must cover 200 points of scroll.
auto tDraggingMapsTravelToRange = test("ScrollBar/draggingMapsTheTravelOntoTheRange") = []
{
    auto theme = ChromeTheme {};
    auto bar = ScrollBar {theme};
    auto host = WidgetHost {};

    auto reported = 0.f;
    bar.onScrolled = [&reported](float y) { reported = y; };

    host.setRoot(bar);
    bar.setBounds({0.f, 0.f, 10.f, 200.f});
    bar.setRange(400.f, 200.f);   // 100pt thumb, 100pt travel, 200pt range

    // Grab the thumb at its top, then drag half way down the travel.
    host.mouseDown(mouseAt(5.f, 0.f));
    host.mouseDragged(mouseAt(5.f, 50.f));

    // Half the travel is half the range, not half the track.
    check(reported == 100.f);

    host.mouseDragged(mouseAt(5.f, 100.f));
    check(reported == 200.f);
};

// Grabbing the thumb anywhere but its top must not make it jump so its top
// lands under the pointer.
auto tThumbDoesNotJumpOnGrab = test("ScrollBar/grabbingTheThumbDoesNotMakeItJump") = []
{
    auto theme = ChromeTheme {};
    auto bar = ScrollBar {theme};
    auto host = WidgetHost {};

    host.setRoot(bar);
    bar.setBounds({0.f, 0.f, 10.f, 200.f});
    bar.setRange(400.f, 200.f);
    bar.setPosition(0.f);

    // Grab 40 points down a thumb whose top is at 0, and do not move.
    host.mouseDown(mouseAt(5.f, 40.f));

    check(bar.position() == 0.f);
    check(bar.thumbBounds().y == 0.f);
};

// A bar with nothing to scroll reports so, and the view hides it. A thumb
// filling its track says "there is more" when there is not.
auto tFullBarIsNotNeeded = test("ScrollBar/aBarWithNothingToScrollIsNotNeeded") = []
{
    auto theme = ChromeTheme {};
    auto bar = ScrollBar {theme};

    bar.setBounds({0.f, 0.f, 10.f, 200.f});
    bar.setRange(150.f, 200.f);

    check(!bar.isNeeded());

    bar.setRange(250.f, 200.f);
    check(bar.isNeeded());
};
