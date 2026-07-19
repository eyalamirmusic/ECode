#include <ECodeUI/EditorWidget.h>
#include <ECodeUI/Splitter.h>
#include <ECodeUI/WidgetHost.h>

#include <NanoTest/NanoTest.h>

// The splitter, and the cursor plumbing it exists to exercise.
//
// The splitter holds a position rather than the panes, so all of this is
// arithmetic over one float plus the question of who answers for the pointer's
// shape — which matters more than it looks, because the whole window is one
// Graphics::View and so has exactly one cursor.

using namespace nano;
using namespace ecode;
using namespace eacp;

namespace
{
// A vertical splitter straddling x = 200 in an 800x600 window.
struct Fixture
{
    Fixture()
    {
        splitter.setLimits(100.f, 700.f);
        splitter.setPosition(200.f);
        splitter.setBounds({200.f - Splitter::grabThickness * 0.5f,
                            0.f,
                            Splitter::grabThickness,
                            600.f});

        splitter.onMoved = [this](float position)
        {
            ++moves;
            lastPosition = position;
        };
    }

    ChromeTheme theme;
    Splitter splitter {theme, Splitter::Orientation::Vertical};

    int moves = 0;
    float lastPosition = 0.f;
};

Graphics::MouseEvent mouseAt(float x, float y)
{
    auto event = Graphics::MouseEvent {};
    event.pos = {x, y};

    return event;
}
} // namespace

auto tPositionIsHeld = test("Splitter/positionIsHeld") = []
{
    auto fixture = Fixture {};

    check(fixture.splitter.position() == 200.f);
};

// The grab band is wider than the line and centred on it, so the divider is as
// easy to reach from either side.
auto tGrabBandStraddlesTheLine = test("Splitter/grabBandStraddlesTheLine") = []
{
    auto fixture = Fixture {};

    const auto band = fixture.splitter.grabBounds();

    check(band.w == Splitter::grabThickness);
    check(band.w > Splitter::lineThickness);

    // Centre of the band is the divider itself.
    check(std::abs(band.x + band.w * 0.5f - 200.f) < 0.01f);

    // Reachable from a few points either side of the line.
    check(band.contains({198.f, 300.f}));
    check(band.contains({202.f, 300.f}));
    check(!band.contains({190.f, 300.f}));
};

auto tDragMovesThePosition = test("Splitter/dragMovesThePosition") = []
{
    auto fixture = Fixture {};

    fixture.splitter.mouseDown(mouseAt(200.f, 300.f));
    fixture.splitter.mouseDragged(mouseAt(320.f, 300.f));

    check(fixture.splitter.position() == 320.f);
    check(fixture.moves == 1);
    check(fixture.lastPosition == 320.f);
};

// The divider keeps its distance from the pointer instead of snapping its
// centre to it. Grabbing near the band's edge and having the line jump up to
// half its thickness sideways is a twitch visible on every drag.
auto tDragKeepsTheGrabOffset = test("Splitter/dragKeepsTheGrabOffset") = []
{
    auto fixture = Fixture {};

    // Pressed 3 points right of the divider, still inside the band.
    fixture.splitter.mouseDown(mouseAt(203.f, 300.f));

    // The press alone must not move anything.
    check(fixture.splitter.position() == 200.f);
    check(fixture.moves == 0);

    fixture.splitter.mouseDragged(mouseAt(303.f, 300.f));

    // Moved by the drag distance, not to the pointer.
    check(fixture.splitter.position() == 300.f);
};

auto tDragIsClampedToLimits = test("Splitter/dragIsClampedToLimits") = []
{
    auto fixture = Fixture {};

    fixture.splitter.mouseDown(mouseAt(200.f, 300.f));

    fixture.splitter.mouseDragged(mouseAt(-500.f, 300.f));
    check(fixture.splitter.position() == 100.f);

    fixture.splitter.mouseDragged(mouseAt(5000.f, 300.f));
    check(fixture.splitter.position() == 700.f);
};

// A drag that does not actually move the divider — because it is already against
// a limit — reports nothing. Otherwise dragging past the edge would relay out
// the whole window on every mouse move for as long as someone held it there.
auto tClampedDragDoesNotReport = test("Splitter/clampedDragDoesNotReport") = []
{
    auto fixture = Fixture {};

    fixture.splitter.mouseDown(mouseAt(200.f, 300.f));
    fixture.splitter.mouseDragged(mouseAt(-500.f, 300.f));

    const auto movesAtLimit = fixture.moves;

    fixture.splitter.mouseDragged(mouseAt(-600.f, 300.f));
    fixture.splitter.mouseDragged(mouseAt(-700.f, 300.f));

    check(fixture.moves == movesAtLimit);
};

auto tDragIgnoredWithoutAPress = test("Splitter/dragIgnoredWithoutAPress") = []
{
    auto fixture = Fixture {};

    fixture.splitter.mouseDragged(mouseAt(400.f, 300.f));

    check(fixture.splitter.position() == 200.f);
    check(fixture.moves == 0);
};

// Tightening the limits pulls the divider in with them, so a window resized
// narrower does not leave the sidebar stranded outside its own bounds until
// someone happens to drag it.
auto tNewLimitsReclampThePosition = test("Splitter/newLimitsReclampThePosition") = []
{
    auto fixture = Fixture {};

    fixture.splitter.setPosition(600.f);
    fixture.splitter.setLimits(100.f, 300.f);

    check(fixture.splitter.position() == 300.f);
};

// A window too narrow for its own minimums crosses the limits over. std::clamp
// is undefined when that happens, so this is the case that would be a real bug
// rather than a wrong number.
auto tCrossedLimitsDoNotMisbehave = test("Splitter/crossedLimitsDoNotMisbehave") = []
{
    auto fixture = Fixture {};

    fixture.splitter.setLimits(400.f, 200.f);

    check(fixture.splitter.position() == 400.f);

    fixture.splitter.mouseDown(mouseAt(400.f, 300.f));
    fixture.splitter.mouseDragged(mouseAt(1000.f, 300.f));

    check(fixture.splitter.position() == 400.f);
};

// --- the cursor -------------------------------------------------------------

auto tCursorIsResizeOverTheBand = test("Splitter/cursorIsResizeOverTheBand") = []
{
    auto fixture = Fixture {};

    // Nothing has pointed at it yet.
    check(fixture.splitter.cursor() == Graphics::MouseCursor::Default);

    fixture.splitter.mouseMoved(mouseAt(200.f, 300.f));
    check(fixture.splitter.cursor() == Graphics::MouseCursor::ResizeLeftRight);

    fixture.splitter.mouseMoved(mouseAt(50.f, 300.f));
    check(fixture.splitter.cursor() == Graphics::MouseCursor::Default);
};

auto tHorizontalSplitterResizesUpDown =
    test("Splitter/horizontalSplitterResizesUpDown") = []
{
    auto theme = ChromeTheme {};
    auto splitter = Splitter {theme, Splitter::Orientation::Horizontal};

    splitter.setLimits(0.f, 600.f);
    splitter.setPosition(300.f);
    splitter.setBounds({0.f, 296.f, 800.f, Splitter::grabThickness});

    splitter.mouseMoved(mouseAt(400.f, 300.f));

    check(splitter.cursor() == Graphics::MouseCursor::ResizeUpDown);
};

// A horizontal splitter tracks y rather than x, which a copy-paste of the
// vertical case would get wrong while every other test still passed.
auto tHorizontalSplitterTracksY = test("Splitter/horizontalSplitterTracksY") = []
{
    auto theme = ChromeTheme {};
    auto splitter = Splitter {theme, Splitter::Orientation::Horizontal};

    splitter.setLimits(0.f, 600.f);
    splitter.setPosition(300.f);
    splitter.setBounds({0.f, 296.f, 800.f, Splitter::grabThickness});

    splitter.mouseDown(mouseAt(400.f, 300.f));
    splitter.mouseDragged(mouseAt(700.f, 420.f));

    // Followed the vertical movement and ignored the much larger horizontal one.
    check(splitter.position() == 420.f);
};

// While dragging, the shape holds wherever the pointer has got to. A pointer
// reverting to the arrow mid-drag reads as the drag having been dropped.
auto tCursorHoldsDuringADrag = test("Splitter/cursorHoldsDuringADrag") = []
{
    auto fixture = Fixture {};

    fixture.splitter.mouseMoved(mouseAt(200.f, 300.f));
    fixture.splitter.mouseDown(mouseAt(200.f, 300.f));
    fixture.splitter.mouseDragged(mouseAt(600.f, 300.f));

    check(fixture.splitter.isDragging());
    check(fixture.splitter.cursor() == Graphics::MouseCursor::ResizeLeftRight);
};

// And reverts when the button comes up somewhere else.
auto tCursorRevertsAfterADrag = test("Splitter/cursorRevertsAfterADrag") = []
{
    auto fixture = Fixture {};

    fixture.splitter.mouseMoved(mouseAt(200.f, 300.f));
    fixture.splitter.mouseDown(mouseAt(200.f, 300.f));
    fixture.splitter.mouseDragged(mouseAt(600.f, 300.f));
    fixture.splitter.mouseUp(mouseAt(600.f, 300.f));

    check(!fixture.splitter.isDragging());

    // The band moved with the divider, and 600 is where it now is — but the
    // splitter's *bounds* have not been relaid out by a parent here, so the
    // release counts as outside.
    check(fixture.splitter.cursor() == Graphics::MouseCursor::Default);
};

// --- the host's answer ------------------------------------------------------

auto tHostAsksTheWidgetUnderThePointer =
    test("Splitter/hostAsksTheWidgetUnderThePointer") = []
{
    auto theme = ChromeTheme {};
    auto host = WidgetHost {};
    auto root = Widget {};
    auto splitter = Splitter {theme, Splitter::Orientation::Vertical};

    root.setBounds({0.f, 0.f, 800.f, 600.f});
    splitter.setLimits(100.f, 700.f);
    splitter.setPosition(200.f);
    splitter.setBounds({196.f, 0.f, Splitter::grabThickness, 600.f});

    root.addChild(splitter);
    host.setRoot(root);

    // Over bare root: nothing claims a shape.
    check(host.cursorAt({500.f, 300.f}) == Graphics::MouseCursor::Default);

    // Over the splitter, once it knows the pointer is on it.
    splitter.mouseMoved(mouseAt(200.f, 300.f));
    check(host.cursorAt({200.f, 300.f}) == Graphics::MouseCursor::ResizeLeftRight);
};

// The captured widget answers wherever the pointer is, which is what keeps the
// resize shape up while the divider is dragged past its own band.
auto tCaptureOwnsTheCursor = test("Splitter/captureOwnsTheCursor") = []
{
    auto theme = ChromeTheme {};
    auto host = WidgetHost {};
    auto root = Widget {};
    auto splitter = Splitter {theme, Splitter::Orientation::Vertical};

    root.setBounds({0.f, 0.f, 800.f, 600.f});
    splitter.setLimits(100.f, 700.f);
    splitter.setPosition(200.f);
    splitter.setBounds({196.f, 0.f, Splitter::grabThickness, 600.f});

    root.addChild(splitter);
    host.setRoot(root);

    host.mouseDown(mouseAt(200.f, 300.f));

    // Far away from the band, and still the splitter's shape.
    check(host.cursorAt({650.f, 300.f}) == Graphics::MouseCursor::ResizeLeftRight);

    host.mouseUp(mouseAt(650.f, 300.f));

    check(host.cursorAt({650.f, 300.f}) == Graphics::MouseCursor::Default);
};

// A splitter is a mouse control. Putting it in the Tab order would land focus on
// something with no keyboard behaviour and nothing drawn to say it has focus.
auto tSplitterIsNotAFocusStop = test("Splitter/splitterIsNotAFocusStop") = []
{
    auto theme = ChromeTheme {};
    auto splitter = Splitter {theme, Splitter::Orientation::Vertical};

    check(!splitter.acceptsFocus());
    check(splitter.wantsMouse());
};
