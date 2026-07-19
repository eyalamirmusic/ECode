#include <ECodeUI/FindBar.h>

#include <NanoTest/NanoTest.h>

// What the bar reports and what a key does to it. The bar holds no matches — the
// search lives in EditorWidget — so what is testable here is exactly the seam:
// the query it publishes, the callbacks it fires, and the keys it claims.

using namespace nano;
using namespace ecode;
using namespace eacp;

namespace
{
// A bar wide enough for every control to get a real rect, so the hotspot tests
// are hitting laid-out geometry rather than a degenerate strip.
const auto barBounds = Graphics::Rect {600.f, 40.f, 460.f, 68.f};

struct Fixture
{
    Fixture()
    {
        bar.setBounds(barBounds);

        bar.onQueryChanged = [this] { ++queryChanges; };
        bar.onFindNext = [this] { ++nexts; };
        bar.onFindPrevious = [this] { ++previouses; };
        bar.onReplace = [this] { ++replaces; };
        bar.onReplaceAll = [this] { ++replaceAlls; };
        bar.onClosed = [this] { ++closes; };
        bar.onFocusRequested = [this](Widget& widget) { focusAskedFor = &widget; };
    }

    ChromeTheme theme;
    FindBar bar {theme};

    int queryChanges = 0;
    int nexts = 0;
    int previouses = 0;
    int replaces = 0;
    int replaceAlls = 0;
    int closes = 0;

    Widget* focusAskedFor = nullptr;
};

Graphics::KeyEvent pressed(std::uint16_t code)
{
    auto event = Graphics::KeyEvent {};
    event.keyCode = code;

    return event;
}

Graphics::MouseEvent clickAt(const Graphics::Point& point)
{
    auto event = Graphics::MouseEvent {};
    event.pos = point;

    return event;
}
} // namespace

// --- opening and closing ----------------------------------------------------

auto tStartsClosed = test("FindBar/startsClosed") = []
{
    const auto fixture = Fixture {};

    check(!fixture.bar.isOpen());
};

auto tShowSeedsTheQuery = test("FindBar/showSeedsTheQueryFromWhatItIsGiven") = []
{
    auto fixture = Fixture {};

    fixture.bar.show("needle", false);

    check(fixture.bar.isOpen());
    check(fixture.bar.query().text == "needle");
    check(!fixture.bar.isReplaceShown());
};

// An empty seed leaves the previous query alone, which is what makes ⌘F with
// nothing selected a way back to what was last searched for rather than a wipe.
auto tEmptySeedKeepsTheQuery = test("FindBar/anEmptySeedKeepsThePreviousQuery") = []
{
    auto fixture = Fixture {};

    fixture.bar.show("needle", false);
    fixture.bar.hide();
    fixture.bar.show("", false);

    check(fixture.bar.query().text == "needle");
};

auto tShowWithReplaceIsTaller = test("FindBar/theReplaceRowMakesItTaller") = []
{
    auto fixture = Fixture {};

    fixture.bar.show("x", false);
    const auto oneRow = fixture.bar.barHeight();

    fixture.bar.show("x", true);

    check(fixture.bar.isReplaceShown());
    check(fixture.bar.barHeight() > oneRow);
};

auto tHideReportsOnce = test("FindBar/hidingReportsExactlyOnce") = []
{
    auto fixture = Fixture {};

    fixture.bar.show("x", false);

    fixture.bar.hide();
    fixture.bar.hide(); // already closed

    check(!fixture.bar.isOpen());
    check(fixture.closes == 1);
};

// --- keys -------------------------------------------------------------------

auto tEscapeCloses = test("FindBar/escapeCloses") = []
{
    auto fixture = Fixture {};

    fixture.bar.show("x", false);

    check(fixture.bar.keyDown(pressed(Graphics::KeyCode::Escape)));
    check(!fixture.bar.isOpen());
    check(fixture.closes == 1);
};

auto tReturnFindsNext = test("FindBar/returnFindsTheNextHit") = []
{
    auto fixture = Fixture {};

    fixture.bar.show("x", false);
    fixture.bar.keyDown(pressed(Graphics::KeyCode::Return));

    check(fixture.nexts == 1);
    check(fixture.previouses == 0);
};

auto tShiftReturnFindsPrevious = test("FindBar/shiftReturnFindsThePreviousHit") = []
{
    auto fixture = Fixture {};

    fixture.bar.show("x", false);

    auto event = pressed(Graphics::KeyCode::Return);
    event.modifiers.shift = true;

    fixture.bar.keyDown(event);

    check(fixture.previouses == 1);
    check(fixture.nexts == 0);
};

// Tab is claimed even with nowhere to go. Letting it through would reach the
// editor behind the bar and indent the file, which is a document edit caused by
// a key pressed in a search box.
auto tTabIsClaimedWithNoReplaceRow =
    test("FindBar/tabIsSwallowedWhenThereIsNowhereToGo") = []
{
    auto fixture = Fixture {};

    fixture.bar.show("x", false);

    check(fixture.bar.keyDown(pressed(Graphics::KeyCode::Tab)));
    check(fixture.focusAskedFor == nullptr);
};

auto tTabAsksForTheOtherField = test("FindBar/tabAsksForTheOtherField") = []
{
    auto fixture = Fixture {};

    fixture.bar.show("x", true);

    auto& findField = fixture.bar.keyboardTarget();

    // With nothing focused the request is for the find field, which is where a
    // person who has clicked a button and pressed Tab expects to land.
    fixture.bar.keyDown(pressed(Graphics::KeyCode::Tab));
    check(fixture.focusAskedFor == &findField);

    // And with the find field focused it is the *other* one — the half a
    // version that always answered "find field" would get wrong.
    findField.focusGained();
    fixture.bar.keyDown(pressed(Graphics::KeyCode::Tab));

    check(fixture.focusAskedFor != nullptr);
    check(fixture.focusAskedFor != &findField);
};

// Anything the bar has no use for goes back up, so an unhandled key still
// reaches the application's shortcuts rather than dying here.
auto tUnknownKeysBubble = test("FindBar/keysItHasNoUseForBubble") = []
{
    auto fixture = Fixture {};

    fixture.bar.show("x", false);

    check(!fixture.bar.keyDown(pressed(Graphics::KeyCode::F5)));
};

// --- the controls -----------------------------------------------------------

namespace
{
// Walks the bar's own geometry to find where a control landed, so the click
// tests do not hard-code coordinates that the layout constants would silently
// invalidate.
Graphics::Point
    pointOnControl(const FindBar& bar, float fromLeftOfControls, float row)
{
    // The controls follow the find field, which starts after the bar's padding.
    return {bar.bounds().x + fromLeftOfControls, bar.bounds().y + row};
}
} // namespace

// The toggles change what counts as a match, so flipping one has to be reported
// the same way typing does — the highlighting on screen is stale either way.
auto tTogglingCaseReportsAChange =
    test("FindBar/togglingCaseSensitivityReportsAQueryChange") = []
{
    auto fixture = Fixture {};

    fixture.bar.show("x", false);

    const auto before = fixture.queryChanges;

    // 8 padding + 200 field + 6 gap puts the case toggle at 214, mid-row at 17.
    fixture.bar.mouseDown(clickAt(pointOnControl(fixture.bar, 220.f, 17.f)));

    check(fixture.bar.query().caseSensitive);
    check(fixture.queryChanges == before + 1);

    fixture.bar.mouseDown(clickAt(pointOnControl(fixture.bar, 220.f, 17.f)));

    check(!fixture.bar.query().caseSensitive);
};

auto tTogglingWholeWordReportsAChange =
    test("FindBar/togglingWholeWordReportsAQueryChange") = []
{
    auto fixture = Fixture {};

    fixture.bar.show("x", false);

    const auto before = fixture.queryChanges;

    // The wide toggle sits directly after the 30pt case one, at 244.
    fixture.bar.mouseDown(clickAt(pointOnControl(fixture.bar, 260.f, 17.f)));

    check(fixture.bar.query().wholeWord);
    check(fixture.queryChanges == before + 1);
};

auto tClickingTheArrows = test("FindBar/theArrowsMoveBetweenHits") = []
{
    auto fixture = Fixture {};

    fixture.bar.show("x", false);

    // Right to left from the bar's right edge: close, next, previous, each 26
    // wide inside 8 of padding.
    const auto right = fixture.bar.bounds().w;

    fixture.bar.mouseDown(
        clickAt(pointOnControl(fixture.bar, right - 8.f - 65.f, 17.f)));
    check(fixture.previouses == 1);

    fixture.bar.mouseDown(
        clickAt(pointOnControl(fixture.bar, right - 8.f - 39.f, 17.f)));
    check(fixture.nexts == 1);
};

auto tClickingCloseCloses = test("FindBar/theCloseButtonCloses") = []
{
    auto fixture = Fixture {};

    fixture.bar.show("x", false);

    const auto right = fixture.bar.bounds().w;

    fixture.bar.mouseDown(
        clickAt(pointOnControl(fixture.bar, right - 8.f - 13.f, 17.f)));

    check(!fixture.bar.isOpen());
    check(fixture.closes == 1);
};

auto tReplaceButtonsOnlyExistWithTheRow =
    test("FindBar/theReplaceButtonsExistOnlyWhenTheRowDoes") = []
{
    auto fixture = Fixture {};

    fixture.bar.show("x", false);

    // Where the Replace button would be if the second row were up.
    fixture.bar.mouseDown(clickAt(pointOnControl(fixture.bar, 240.f, 51.f)));

    check(fixture.replaces == 0);

    fixture.bar.show("x", true);
    fixture.bar.mouseDown(clickAt(pointOnControl(fixture.bar, 240.f, 51.f)));

    check(fixture.replaces == 1);
};

auto tReplaceAllButton = test("FindBar/theAllButtonReplacesEverything") = []
{
    auto fixture = Fixture {};

    fixture.bar.show("x", true);

    // 8 padding + 200 field + 6 gap + 68 Replace + 6 gap puts All at 288.
    fixture.bar.mouseDown(clickAt(pointOnControl(fixture.bar, 300.f, 51.f)));

    check(fixture.replaceAlls == 1);
    check(fixture.replaces == 0);
};

auto tClickingNothingDoesNothing =
    test("FindBar/aClickOnTheBarItselfDoesNothing") = []
{
    auto fixture = Fixture {};

    fixture.bar.show("x", true);

    const auto changesBefore = fixture.queryChanges;

    // The gap between the field and the first toggle. Six points wide, which is
    // the point: a hotspot table built with an off-by-one in the gaps would
    // still catch this click, and the query-change count is what notices.
    fixture.bar.mouseDown(clickAt(pointOnControl(fixture.bar, 211.f, 17.f)));

    check(fixture.nexts == 0);
    check(fixture.previouses == 0);
    check(fixture.replaces == 0);
    check(fixture.replaceAlls == 0);
    check(fixture.queryChanges == changesBefore);
    check(fixture.bar.isOpen());
};
