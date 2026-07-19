#include <ECodeUI/ContextMenu.h>
#include <ECodeUI/WidgetHost.h>

#include <NanoTest/NanoTest.h>

#include <string>

// The in-window context menu. Everything here is logic over rectangles and a
// command registry, so none of it needs a device — what the menu *draws* is in
// ContextMenuRenderTests.
//
// The window is 800x600 throughout, so the flipping tests can name coordinates
// near its edges without arithmetic.

using namespace nano;
using namespace ecode;
using namespace eacp;

namespace
{
constexpr auto windowWidth = 800.f;
constexpr auto windowHeight = 600.f;

struct Fixture
{
    Fixture()
    {
        registry.add({"edit.cut",
                      "Cut",
                      [this] { ran = "edit.cut"; },
                      [this] { return hasSelection; }});

        registry.add({"edit.copy",
                      "Copy",
                      [this] { ran = "edit.copy"; },
                      [this] { return hasSelection; }});

        registry.add({"edit.paste", "Paste", [this] { ran = "edit.paste"; }});
        registry.add({"edit.selectAll", "Select All", [this] { ran = "edit.all"; }});

        keymap.bind("cmd+x", "edit.cut");
        keymap.bind("cmd+c", "edit.copy");
        keymap.bind("cmd+v", "edit.paste");

        menu.setBounds({0.f, 0.f, windowWidth, windowHeight});

        menu.onCommandChosen = [this](std::string_view id)
        { chosen = std::string {id}; };

        menu.onClosed = [this] { ++closes; };
    }

    Vector<std::string> clipboardMenu() const
    {
        return {"edit.cut", "edit.copy", "edit.paste", {}, "edit.selectAll"};
    }

    ChromeTheme theme;
    CommandRegistry registry;
    Keymap keymap;

    ContextMenu menu {theme, registry, keymap};

    std::string ran;
    std::string chosen;
    bool hasSelection = true;
    int closes = 0;
};

Graphics::MouseEvent mouseAt(Graphics::Point pos)
{
    auto event = Graphics::MouseEvent {};
    event.pos = pos;

    return event;
}

Graphics::KeyEvent keyEvent(std::uint16_t code)
{
    auto event = Graphics::KeyEvent {};
    event.keyCode = code;

    return event;
}
} // namespace

// --- what a menu is made of -------------------------------------------------

auto tShowResolvesRows = test("ContextMenu/showResolvesRows") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());

    check(fixture.menu.isOpen());

    // Four commands and the separator between Paste and Select All.
    check(fixture.menu.rows().size() == 5);
    check(fixture.menu.rows()[0].title == "Cut");
    check(fixture.menu.rows()[3].isSeparator);
    check(fixture.menu.rows()[4].title == "Select All");
};

// Titles and shortcuts come from the registry and the keymap, the same way the
// menu bar's do, so nothing is spelled out twice.
auto tRowsTakeShortcutFromKeymap =
    test("ContextMenu/rowsTakeShortcutFromKeymap") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());

    check(fixture.menu.rows()[0].shortcut == "⌘X");
    check(fixture.menu.rows()[1].shortcut == "⌘C");

    // Nothing bound to Select All, so it prints no shortcut rather than a
    // wrong one.
    check(fixture.menu.rows()[4].shortcut.empty());
};

auto tUnknownCommandIsDropped = test("ContextMenu/unknownCommandIsDropped") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, {"edit.copy", "edit.doesNotExist"});

    check(fixture.menu.rows().size() == 1);
    check(fixture.menu.rows()[0].title == "Copy");
};

// A separator with nothing above it would draw a rule against the box's top
// edge, and two together would draw two rules with nothing between them.
auto tSeparatorsAreCollapsed = test("ContextMenu/separatorsAreCollapsed") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f},
                      {{}, {}, "edit.copy", {}, {}, "edit.paste", {}, {}});

    check(fixture.menu.rows().size() == 3);
    check(!fixture.menu.rows()[0].isSeparator);
    check(fixture.menu.rows()[1].isSeparator);
    check(!fixture.menu.rows()[2].isSeparator);
};

// A separator-only menu collapses to nothing, and a menu with nothing to offer
// does not open — a popup appearing empty under the pointer reads as a glitch
// rather than as an answer.
auto tEmptyMenuDoesNotOpen = test("ContextMenu/emptyMenuDoesNotOpen") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, {{}, {}});
    check(!fixture.menu.isOpen());

    fixture.menu.show({100.f, 100.f}, {"edit.nope"});
    check(!fixture.menu.isOpen());

    fixture.menu.show({100.f, 100.f}, {});
    check(!fixture.menu.isOpen());
};

// Availability is the command's own, read live, so a menu built once still
// greys correctly if the state changed under it.
auto tRowEnablementFollowsCommand =
    test("ContextMenu/rowEnablementFollowsCommand") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());

    check(fixture.menu.isRowEnabled(0));

    fixture.hasSelection = false;

    check(!fixture.menu.isRowEnabled(0));
    check(!fixture.menu.isRowEnabled(1));
    check(fixture.menu.isRowEnabled(2));

    // A separator is never enabled, and neither is a row that is not there.
    check(!fixture.menu.isRowEnabled(3));
    check(!fixture.menu.isRowEnabled(-1));
    check(!fixture.menu.isRowEnabled(99));
};

// --- where the box lands ----------------------------------------------------

auto tBoxOpensAtThePointer = test("ContextMenu/boxOpensAtThePointer") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({120.f, 90.f}, fixture.clipboardMenu());

    const auto box = fixture.menu.boxBounds();

    check(box.x == 120.f);
    check(box.y == 90.f);
    check(box.w > 0.f);
    check(box.h > 0.f);
};

// Near the right edge the box grows *left* from the pointer rather than being
// clamped, which is what a native menu does — clamping would leave the pointer
// sitting in the middle of the box instead of at its corner.
auto tBoxFlipsAtTheRightEdge = test("ContextMenu/boxFlipsAtTheRightEdge") = []
{
    auto fixture = Fixture {};

    const auto anchorX = windowWidth - 10.f;

    fixture.menu.show({anchorX, 100.f}, fixture.clipboardMenu());

    const auto box = fixture.menu.boxBounds();

    check(box.right() <= anchorX);
    check(box.right() <= windowWidth);
};

auto tBoxFlipsAtTheBottomEdge = test("ContextMenu/boxFlipsAtTheBottomEdge") = []
{
    auto fixture = Fixture {};

    const auto anchorY = windowHeight - 10.f;

    fixture.menu.show({100.f, anchorY}, fixture.clipboardMenu());

    const auto box = fixture.menu.boxBounds();

    check(box.bottom() <= anchorY);
    check(box.bottom() <= windowHeight);
};

// Flipped *and* clamped: a corner is the case where flipping alone still leaves
// the box off-screen on one axis if the window is small.
auto tBoxStaysOnScreenInACorner = test("ContextMenu/boxStaysOnScreenInACorner") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({windowWidth - 2.f, windowHeight - 2.f},
                      fixture.clipboardMenu());

    const auto box = fixture.menu.boxBounds();

    check(box.x >= 0.f);
    check(box.y >= 0.f);
    check(box.right() <= windowWidth);
    check(box.bottom() <= windowHeight);
};

// --- pointer ----------------------------------------------------------------

auto tRowAtFindsRows = test("ContextMenu/rowAtFindsRows") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());

    const auto box = fixture.menu.boxBounds();

    // Outside the box entirely.
    check(fixture.menu.rowAt({10.f, 10.f}) == -1);

    // Inside, on the first row.
    check(fixture.menu.rowAt({box.x + 20.f, box.y + 14.f}) == 0);
};

// A separator is not a row you can land on, by pointer or by keyboard.
auto tSeparatorIsNotHittable = test("ContextMenu/separatorIsNotHittable") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());

    for (auto row = 0; row < fixture.menu.rows().size(); ++row)
        if (fixture.menu.rows()[row].isSeparator)
        {
            const auto box = fixture.menu.boxBounds();

            // Sweep the separator's whole band; none of it answers with its own
            // index.
            for (auto y = box.y; y < box.bottom(); y += 1.f)
                check(fixture.menu.rowAt({box.x + 20.f, y}) != row);
        }
};

auto tHoverHighlightsRow = test("ContextMenu/hoverHighlightsRow") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());

    // A menu opens with nothing highlighted: the pointer is at the box's
    // corner, not on a row, and pre-selecting would mean a stray Return ran
    // something nobody pointed at.
    check(fixture.menu.highlightedRow() == -1);

    const auto box = fixture.menu.boxBounds();

    fixture.menu.mouseMoved(mouseAt({box.x + 20.f, box.y + 14.f}));
    check(fixture.menu.highlightedRow() == 0);

    // Off the box again clears it.
    fixture.menu.mouseMoved(mouseAt({5.f, 5.f}));
    check(fixture.menu.highlightedRow() == -1);
};

auto tClickOutsideDismisses = test("ContextMenu/clickOutsideDismisses") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());
    fixture.menu.mouseDown(mouseAt({5.f, 5.f}));

    check(!fixture.menu.isOpen());
    check(fixture.closes == 1);
    check(fixture.chosen.empty());
};

// Release commits, so press-drag-release across the menu picks the row let go
// on — which is how a menu behaves when it is opened by holding the button.
auto tReleaseChoosesRow = test("ContextMenu/releaseChoosesRow") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());

    const auto box = fixture.menu.boxBounds();
    const auto onCopy = Graphics::Point {box.x + 20.f, box.y + 14.f + 24.f};

    fixture.menu.mouseDown(mouseAt({box.x + 20.f, box.y + 14.f}));
    fixture.menu.mouseUp(mouseAt(onCopy));

    check(fixture.chosen == "edit.copy");
    check(!fixture.menu.isOpen());
};

// Releasing outside after pressing inside backs out rather than choosing, which
// is how someone cancels a menu they opened by mistake.
auto tReleaseOutsideCancels = test("ContextMenu/releaseOutsideCancels") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());

    const auto box = fixture.menu.boxBounds();

    fixture.menu.mouseDown(mouseAt({box.x + 20.f, box.y + 14.f}));
    fixture.menu.mouseUp(mouseAt({5.f, 5.f}));

    check(fixture.chosen.empty());
    check(fixture.menu.isOpen());
};

// A disabled row is inert: it neither runs nor closes the menu, so the failure
// is visible rather than the menu vanishing having done nothing.
auto tDisabledRowDoesNotRun = test("ContextMenu/disabledRowDoesNotRun") = []
{
    auto fixture = Fixture {};

    fixture.hasSelection = false;
    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());

    const auto box = fixture.menu.boxBounds();

    fixture.menu.mouseDown(mouseAt({box.x + 20.f, box.y + 14.f}));
    fixture.menu.mouseUp(mouseAt({box.x + 20.f, box.y + 14.f}));

    check(fixture.chosen.empty());
    check(fixture.menu.isOpen());
};

// The command is routed out rather than run here, because a focused text box
// may claim it — the same reason the menu bar's items dispatch.
auto tChoiceDispatchesRatherThanRuns =
    test("ContextMenu/choiceDispatchesRatherThanRuns") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());

    const auto box = fixture.menu.boxBounds();

    fixture.menu.mouseDown(mouseAt({box.x + 20.f, box.y + 14.f}));
    fixture.menu.mouseUp(mouseAt({box.x + 20.f, box.y + 14.f}));

    check(fixture.chosen == "edit.cut");

    // Straight past the registry: the command itself did not run here.
    check(fixture.ran.empty());
};

// --- keyboard ---------------------------------------------------------------

auto tArrowsSkipSeparators = test("ContextMenu/arrowsSkipSeparators") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());

    // Down from nothing lands on the first row, not on row 0 by default.
    fixture.menu.keyDown(keyEvent(Graphics::KeyCode::DownArrow));
    check(fixture.menu.highlightedRow() == 0);

    fixture.menu.keyDown(keyEvent(Graphics::KeyCode::DownArrow));
    fixture.menu.keyDown(keyEvent(Graphics::KeyCode::DownArrow));
    check(fixture.menu.highlightedRow() == 2);

    // Row 3 is the separator, so the next Down lands on 4.
    fixture.menu.keyDown(keyEvent(Graphics::KeyCode::DownArrow));
    check(fixture.menu.highlightedRow() == 4);
};

auto tArrowsSkipDisabledRows = test("ContextMenu/arrowsSkipDisabledRows") = []
{
    auto fixture = Fixture {};

    // Cut and Copy both unavailable, so Down should land straight on Paste.
    fixture.hasSelection = false;
    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());

    fixture.menu.keyDown(keyEvent(Graphics::KeyCode::DownArrow));

    check(fixture.menu.highlightedRow() == 2);
};

// Up from nothing lands on the last runnable row rather than the first.
auto tUpFromNothingTakesTheLastRow =
    test("ContextMenu/upFromNothingTakesTheLastRow") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());
    fixture.menu.keyDown(keyEvent(Graphics::KeyCode::UpArrow));

    check(fixture.menu.highlightedRow() == 4);
};

// Stops at the ends rather than cycling. A context menu is short enough to see
// whole, so wrapping reads as the highlight jumping rather than as an end.
auto tHighlightStopsAtTheEnds = test("ContextMenu/highlightStopsAtTheEnds") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());

    fixture.menu.keyDown(keyEvent(Graphics::KeyCode::DownArrow));
    check(fixture.menu.highlightedRow() == 0);

    fixture.menu.keyDown(keyEvent(Graphics::KeyCode::UpArrow));
    check(fixture.menu.highlightedRow() == 0);

    for (auto step = 0; step < 10; ++step)
        fixture.menu.keyDown(keyEvent(Graphics::KeyCode::DownArrow));

    check(fixture.menu.highlightedRow() == 4);
};

auto tReturnRunsHighlighted = test("ContextMenu/returnRunsHighlighted") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());

    fixture.menu.keyDown(keyEvent(Graphics::KeyCode::DownArrow));
    fixture.menu.keyDown(keyEvent(Graphics::KeyCode::Return));

    check(fixture.chosen == "edit.cut");
    check(!fixture.menu.isOpen());
};

// Return with nothing highlighted does nothing at all, and in particular does
// not close — a keystroke that shuts the menu having done nothing reads as a
// dropped input.
auto tReturnWithoutHighlightDoesNothing =
    test("ContextMenu/returnWithoutHighlightDoesNothing") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());
    fixture.menu.keyDown(keyEvent(Graphics::KeyCode::Return));

    check(fixture.chosen.empty());
    check(fixture.menu.isOpen());
};

auto tEscapeDismisses = test("ContextMenu/escapeDismisses") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());
    fixture.menu.keyDown(keyEvent(Graphics::KeyCode::Escape));

    check(!fixture.menu.isOpen());
    check(fixture.closes == 1);
    check(fixture.chosen.empty());
};

// Modal while up: an unhandled key is swallowed rather than bubbling to the
// document behind it, which would edit a file whose caret nobody can see.
auto tOtherKeysAreSwallowed = test("ContextMenu/otherKeysAreSwallowed") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());

    check(fixture.menu.keyDown(keyEvent(Graphics::KeyCode::Tab)));
    check(fixture.menu.isOpen());
};

// hide() on a menu that is already closed must not fire onClosed a second time,
// or focus would be restored twice — the second time to wherever the first
// restore put it.
auto tHideIsIdempotent = test("ContextMenu/hideIsIdempotent") = []
{
    auto fixture = Fixture {};

    fixture.menu.show({100.f, 100.f}, fixture.clipboardMenu());

    fixture.menu.hide();
    fixture.menu.hide();

    check(fixture.closes == 1);
};

// --- focus ------------------------------------------------------------------

// Unlike the palette, whose text field is the focus target, the menu itself
// takes the keyboard — it has no field, and something has to answer the arrows.
auto tMenuIsAFocusStop = test("ContextMenu/menuIsAFocusStop") = []
{
    auto fixture = Fixture {};
    auto host = WidgetHost {};

    host.setRoot(fixture.menu);
    host.setFocus(&fixture.menu);

    check(fixture.menu.acceptsFocus());
    check(host.focused() == &fixture.menu);

    // And it is not a text box, so the editing commands still belong to the
    // application rather than being claimed here.
    check(!fixture.menu.isTextInput());
    check(!host.runCommandOnFocus("edit.paste"));
};
