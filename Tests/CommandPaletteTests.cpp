#include <ECodeUI/CommandPalette.h>
#include <ECodeUI/WidgetHost.h>

#include <NanoTest/NanoTest.h>

// Filtering, ranking, keyboard and dismissal. None of it needs a device: what
// the palette draws is tested by rendering, but *what it is offering* and *what
// a key does to it* are plain logic, which is why they are here.

using namespace nano;
using namespace ecode;
using namespace eacp;

namespace
{
const auto windowBounds = Graphics::Rect {0.f, 0.f, 1200.f, 800.f};

// A palette over a registry of realistic commands, so ranking is exercised
// against titles that actually collide rather than against made-up ones.
//
// Keys go through a WidgetHost rather than into the palette directly, because
// routing *is* part of the behaviour now: the query field has focus and takes
// what it understands, and only what it declines reaches the palette. Calling
// palette.keyDown by hand would test half the path and would report that typing
// does nothing.
struct Fixture
{
    Fixture()
    {
        registry.add({"workbench.showPalette",
                      "Show All Commands",
                      [this] { ran = "palette"; }});
        registry.add({"file.save", "File: Save", [this] { ran = "save"; }});
        registry.add({"edit.undo",
                      "Edit: Undo",
                      [this] { ran = "undo"; },
                      [this] { return undoAvailable; }});
        registry.add(
            {"edit.selectAll", "Edit: Select All", [this] { ran = "selectAll"; }});

        keymap.bind("cmd+shift+p", "workbench.showPalette");
        keymap.bind("cmd+s", "file.save");

        palette.onClosed = [this] { ++closes; };

        root.addChild(palette);
        host.setRoot(root);

        root.setBounds(windowBounds);
        palette.setBounds(windowBounds);
    }

    // Opening and focusing the field, which is what the application does.
    void show()
    {
        palette.show();
        host.setFocus(&palette.keyboardTarget());
    }

    bool press(std::uint16_t code, std::string characters = {})
    {
        auto event = Graphics::KeyEvent {};

        event.keyCode = code;
        event.charactersIgnoringModifiers = characters;
        event.characters = std::move(characters);

        return host.keyDown(event);
    }

    void type(const std::string& text)
    {
        for (auto character: text)
            press(Graphics::KeyCode::Unknown, std::string {character});
    }

    std::string titleOf(int entry) const
    {
        return registry.commands()[palette.entries()[entry].command].title;
    }

    std::string selectedTitle() const { return titleOf(palette.selectedEntry()); }

    ChromeTheme theme;
    CommandRegistry registry;
    Keymap keymap;

    Widget root;
    CommandPalette palette {theme, registry, keymap};
    WidgetHost host;

    std::string ran;
    int closes = 0;
    bool undoAvailable = true;
};

Graphics::MouseEvent mouseAt(float x, float y)
{
    auto event = Graphics::MouseEvent {};
    event.pos = Graphics::Point {x, y};

    return event;
}
} // namespace

// --- opening and filtering --------------------------------------------------

auto tPaletteStartsHidden = test("Palette/startsHidden") = []
{
    auto fixture = Fixture {};

    check(!fixture.palette.isOpen());
};

auto tPaletteShowsEverything = test("Palette/opensOfferingEveryCommand") = []
{
    auto fixture = Fixture {};
    fixture.show();

    check(fixture.palette.isOpen());
    check(fixture.palette.entries().size() == fixture.registry.commands().size());

    // In registration order, and with the first one already selected so Enter
    // is enough on its own.
    check(fixture.titleOf(0) == "Show All Commands");
    check(fixture.palette.selectedEntry() == 0);
};

auto tPaletteFilters = test("Palette/filtersToWhatMatchesTheQuery") = []
{
    auto fixture = Fixture {};
    fixture.show();
    fixture.palette.setQuery("undo");

    check(fixture.palette.entries().size() == 1);
    check(fixture.titleOf(0) == "Edit: Undo");
};

// The point of scoring rather than merely filtering. Both "File: Save" and
// "Edit: Select All" contain s-a in order, so a palette that only filtered
// would offer them in registration order and put Save second.
auto tPaletteRanksByScore = test("Palette/putsTheBestMatchFirst") = []
{
    auto fixture = Fixture {};
    fixture.show();
    fixture.palette.setQuery("sa");

    check(fixture.palette.entries().size() >= 2);
    check(fixture.titleOf(0) == "File: Save");
};

// Commands the query cannot separate must not be reshuffled under the person
// reading them, which is what an unstable sort would do.
//
// The arrangement is what makes this observable at all, and it took two tries —
// §9's "a test can be unable to fail on your machine". A list where *every*
// score ties passes against std::sort no matter how long it is, because libc++
// leaves an all-equal range where it found it. What a real sort has to permute
// is a list it genuinely has to move: two score classes, interleaved, so the
// ties are carried past each other rather than sitting still.
auto tPaletteSortIsStable =
    test("Palette/keepsRegistrationOrderAmongEqualScores") = []
{
    auto fixture = Fixture {};

    constexpr auto perClass = 30;

    for (auto index = 0; index < perClass; ++index)
    {
        const auto suffix = " " + std::to_string(index);

        // "sa" is a contiguous run at a word start in the first and scattered
        // in the second, so the two score differently while every member of
        // each class scores the same — the query stops at "a" and never reaches
        // the number.
        fixture.registry.add(
            {"strong." + std::to_string(index), "File: Save" + suffix});
        fixture.registry.add(
            {"weak." + std::to_string(index), "Select All" + suffix});
    }

    fixture.show();
    fixture.palette.setQuery("sa");

    // Pulled out by class in the order the palette offers them, rather than at
    // fixed indices: the fixture's own commands land among these, and pinning
    // absolute positions would make this a test of the fixture.
    auto saves = eacp::Vector<std::string> {};
    auto selects = eacp::Vector<std::string> {};

    for (auto index = 0; index < fixture.palette.entries().size(); ++index)
    {
        const auto title = fixture.titleOf(index);

        if (title.rfind("File: Save ", 0) == 0)
            saves.push_back(title);
        else if (title.rfind("Select All ", 0) == 0)
            selects.push_back(title);
    }

    check(saves.size() == perClass);
    check(selects.size() == perClass);

    for (auto index = 0; index < perClass; ++index)
    {
        check(saves[index] == "File: Save " + std::to_string(index));
        check(selects[index] == "Select All " + std::to_string(index));
    }
};

auto tPaletteNoMatches = test("Palette/reportsNoMatchesRatherThanACrash") = []
{
    auto fixture = Fixture {};
    fixture.show();
    fixture.palette.setQuery("zzzz");

    check(fixture.palette.entries().empty());
    check(fixture.palette.selectedEntry() == -1);

    // Enter with nothing selected does nothing at all, and in particular does
    // not close: a keystroke that did nothing should not also dismiss.
    fixture.press(Graphics::KeyCode::Return, "\r");

    check(fixture.palette.isOpen());
    check(fixture.ran.empty());
};

// Reopening starts clean rather than resuming a filter that has been forgotten.
auto tPaletteReopensEmpty = test("Palette/reopensWithAnEmptyQuery") = []
{
    auto fixture = Fixture {};

    fixture.show();
    fixture.palette.setQuery("undo");
    fixture.palette.hide();
    fixture.show();

    check(fixture.palette.query().empty());
    check(fixture.palette.entries().size() == fixture.registry.commands().size());
};

// --- the keyboard -----------------------------------------------------------

auto tPaletteTypingFilters = test("Palette/typingBuildsTheQuery") = []
{
    auto fixture = Fixture {};
    fixture.show();

    check(fixture.press(Graphics::KeyCode::Unknown, "u"));
    check(fixture.press(Graphics::KeyCode::Unknown, "n"));

    check(fixture.palette.query() == "un");
    check(fixture.selectedTitle() == "Edit: Undo");
};

// Return, Tab and Escape all arrive with `characters` set to a control code, so
// a palette that appended whatever came in would type them into the query and
// then match nothing at all.
auto tPaletteDoesNotTypeControlCodes =
    test("Palette/doesNotTypeControlCharacters") = []
{
    auto fixture = Fixture {};
    fixture.show();

    fixture.press(Graphics::KeyCode::Tab, "\t");

    check(fixture.palette.query().empty());
};

// Backspace deletes a character, not a byte. A query with a multi-byte
// character in it, cut mid-sequence, stops matching anything and cannot be
// repaired by typing.
auto tPaletteBackspaceDeletesACharacter =
    test("Palette/backspaceDeletesAWholeUtf8Character") = []
{
    auto fixture = Fixture {};
    fixture.show();
    fixture.palette.setQuery("aé");

    fixture.press(Graphics::KeyCode::Delete, "\b");

    check(fixture.palette.query() == "a");

    fixture.press(Graphics::KeyCode::Delete, "\b");

    check(fixture.palette.query().empty());

    // And backspace on an empty query is a no-op rather than an underflow.
    fixture.press(Graphics::KeyCode::Delete, "\b");

    check(fixture.palette.query().empty());
};

// AppKit reports every function key in `characters` as a private-use codepoint —
// Left is U+F702 — which encodes to three ordinary UTF-8 bytes and passes a
// control-character test unharmed. The palette handles Up, Down, Home and End by
// name and nothing else, so Left and Right fall through to the typed-text branch
// and land in the query, where they match nothing and cannot be seen.
auto tPaletteIgnoresFunctionKeyCharacters =
    test("Palette/doesNotTypeTheCodepointsAppKitSendsForFunctionKeys") = []
{
    auto fixture = Fixture {};
    fixture.show();

    fixture.press(Graphics::KeyCode::LeftArrow, "\xef\x9c\x82");
    fixture.press(Graphics::KeyCode::RightArrow, "\xef\x9c\x83");

    check(fixture.palette.query().empty());
};

// Up and Down are the two the field has no use for, so they pass through it to
// the palette and on to the list.
auto tPaletteArrowsMoveTheSelection = test("Palette/arrowKeysMoveTheSelection") = []
{
    auto fixture = Fixture {};
    fixture.show();

    fixture.press(Graphics::KeyCode::DownArrow, "");
    check(fixture.palette.selectedEntry() == 1);

    fixture.press(Graphics::KeyCode::UpArrow, "");
    check(fixture.palette.selectedEntry() == 0);
};

// Home and End belong to the text, not to the list — the behaviour that changed
// when the query became a real field.
//
// They used to jump the list to its first and last row. VSCode moves the caret
// with them, which is what anyone typing into a box expects, and it is barely a
// capability lost: the way to reach a distant command in a fuzzy palette is to
// type, not to travel to it.
auto tPaletteHomeAndEndMoveTheCaret =
    test("Palette/homeAndEndMoveTheCaretRatherThanTheSelection") = []
{
    auto fixture = Fixture {};
    fixture.show();

    fixture.type("sa");

    const auto selected = fixture.palette.selectedEntry();

    fixture.press(Graphics::KeyCode::Home, "");
    check(fixture.palette.selectedEntry() == selected);

    // Typing now lands at the *front* of the query, which is only true if the
    // caret actually moved. Asserting on the text rather than on a caret offset
    // keeps this a test of what a person would see.
    fixture.type("f");
    check(fixture.palette.query() == "fsa");

    fixture.press(Graphics::KeyCode::End, "");
    fixture.type("x");

    check(fixture.palette.query() == "fsax");

    // Deliberately no selection check here: typing refilters, so the selection
    // moving afterwards is the list doing its job rather than End doing the
    // wrong one. The Home assertion above is the one that isolates the key.
};

auto tPaletteEnterRuns = test("Palette/enterRunsTheSelectedCommandAndCloses") = []
{
    auto fixture = Fixture {};
    fixture.show();
    fixture.palette.setQuery("undo");

    check(fixture.press(Graphics::KeyCode::Return, "\r"));

    check(fixture.ran == "undo");
    check(!fixture.palette.isOpen());
    check(fixture.closes == 1);
};

// The expensive direction is closing anyway: the palette vanishes, nothing
// happened, and there is nothing on screen to say why.
auto tPaletteEnterOnDisabled =
    test("Palette/enterOnADisabledCommandDoesNothingAndStaysOpen") = []
{
    auto fixture = Fixture {};
    fixture.undoAvailable = false;

    fixture.show();
    fixture.palette.setQuery("undo");

    fixture.press(Graphics::KeyCode::Return, "\r");

    check(fixture.ran.empty());
    check(fixture.palette.isOpen());
    check(fixture.closes == 0);
};

auto tPaletteEscapeCloses = test("Palette/escapeClosesWithoutRunningAnything") = []
{
    auto fixture = Fixture {};
    fixture.show();

    check(fixture.press(Graphics::KeyCode::Escape, "\x1b"));

    check(!fixture.palette.isOpen());
    check(fixture.ran.empty());
    check(fixture.closes == 1);
};

// Everything is consumed while the palette is up, including keys it does
// nothing with. The editor is still in the tree underneath, and an unconsumed
// key bubbles to it — so a palette that returned false would be typed through.
auto tPaletteSwallowsEverything = test("Palette/consumesEveryKeyWhileOpen") = []
{
    auto fixture = Fixture {};
    fixture.show();

    check(fixture.press(Graphics::KeyCode::F5, ""));
    check(fixture.press(Graphics::KeyCode::PageUp, ""));
};

// --- the mouse --------------------------------------------------------------

auto tPaletteClickOutsideDismisses =
    test("Palette/aClickOutsideTheBoxDismisses") = []
{
    auto fixture = Fixture {};
    fixture.show();

    // Bottom-left, which the box never reaches.
    fixture.palette.mouseDown(mouseAt(20.f, 700.f));

    check(!fixture.palette.isOpen());
    check(fixture.closes == 1);
};

// Clicking the query field is not a dismissal, which is the fold: a palette
// that dismissed on any click reaching it would close the moment the person
// clicked into the thing they were typing in.
auto tPaletteClickInsideStaysOpen =
    test("Palette/aClickInsideTheBoxDoesNotDismiss") = []
{
    auto fixture = Fixture {};
    fixture.show();

    const auto input = fixture.palette.inputBounds();

    fixture.palette.mouseDown(mouseAt(input.x + 10.f, input.y + input.h * 0.5f));

    check(fixture.palette.isOpen());
    check(fixture.closes == 0);
};

// --- focus ------------------------------------------------------------------

// The palette owns the keyboard while it is open. Its result list must not be
// a focus stop of its own, or clicking a row would move focus off the palette
// and the next keystroke would go nowhere.
auto tPaletteKeepsFocusOnItself =
    test("Palette/aClickOnAResultLeavesFocusOnThePalette") = []
{
    auto fixture = Fixture {};
    fixture.show();

    auto host = WidgetHost {};
    host.setRoot(fixture.palette);
    host.setBounds(windowBounds);
    host.setFocus(&fixture.palette);

    const auto results = fixture.palette.resultsBounds();

    host.mouseDown(mouseAt(results.x + 10.f, results.y + 5.f));

    check(host.focused() == &fixture.palette);
};

// And a click on a row runs it, rather than only selecting it.
auto tPaletteClickRunsTheRow = test("Palette/aClickOnAResultRunsIt") = []
{
    auto fixture = Fixture {};
    fixture.show();
    fixture.palette.setQuery("undo");

    auto host = WidgetHost {};
    host.setRoot(fixture.palette);
    host.setBounds(windowBounds);

    const auto results = fixture.palette.resultsBounds();

    host.mouseDown(mouseAt(results.x + 10.f, results.y + 5.f));

    check(fixture.ran == "undo");
    check(!fixture.palette.isOpen());
};

// --- layout -----------------------------------------------------------------

// The box grows with the results up to a cap and then scrolls, so a long list
// cannot cover the file the palette is being used on.
auto tPaletteBoxIsCapped = test("Palette/theBoxStopsGrowingAndScrollsInstead") = []
{
    auto fixture = Fixture {};

    for (auto index = 0; index < 40; ++index)
        fixture.registry.add({"filler." + std::to_string(index),
                              "Filler Command " + std::to_string(index)});

    fixture.show();

    check(fixture.palette.entries().size() > 12);
    check(fixture.palette.boxBounds().h < windowBounds.h * 0.6f);
};

auto tPaletteBoxIsCentred = test("Palette/theBoxIsCentredAndFitsANarrowWindow") = []
{
    auto fixture = Fixture {};
    fixture.show();

    const auto box = fixture.palette.boxBounds();

    check(box.x + box.w * 0.5f == windowBounds.w * 0.5f);

    // Narrower than the box's natural width: it has to shrink rather than run
    // off both edges.
    fixture.palette.setBounds({0.f, 0.f, 320.f, 480.f});

    check(fixture.palette.boxBounds().x >= 0.f);
    check(fixture.palette.boxBounds().right() <= 320.f);
};
