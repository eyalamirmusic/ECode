#include <ECodeUI/CommandPalette.h>
#include <ECodeUI/Settings.h>
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
        return palette.itemOf(palette.entries()[entry]).title;
    }

    std::string hintOf(int entry) const
    {
        return palette.itemOf(palette.entries()[entry]).hint;
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

// --- the shortcuts it prints ------------------------------------------------

// The composition the settings file makes possible, and the reason it is tested
// here rather than only in SettingsTests: the merge produces a keymap, and the
// palette is what a person reads that keymap *through*. Two halves that are each
// right can still print the wrong string between them.
//
// The three checks are three different failures. The rebound command has to show
// its new chord; a command the file never mentioned has to keep the default's;
// and the chord that was taken away has to stop being offered for the command it
// used to run — that last one is the expensive direction, since the palette
// prints it as an instruction to press a key.
auto tPalettePrintsTheConfiguredChords =
    test("Palette/printsTheChordsAFileRebound") = []
{
    auto fixture = Fixture {};

    fixture.keymap =
        configurationFromJson(R"({"keybindings": {"cmd+e": "file.save"}})").keymap;

    fixture.show();
    fixture.type("save");

    check(fixture.selectedTitle() == "File: Save");
    check(fixture.hintOf(0) == "⌘E");

    fixture.palette.setQuery("show all");
    check(fixture.hintOf(0) == "⇧⌘P");
};

auto tPaletteDropsAnUnboundChord =
    test("Palette/offersNoChordForACommandTheFileUnbound") = []
{
    auto fixture = Fixture {};

    fixture.keymap =
        configurationFromJson(R"({"keybindings": {"cmd+s": ""}})").keymap;

    fixture.show();
    fixture.type("save");

    check(fixture.selectedTitle() == "File: Save");
    check(fixture.hintOf(0).empty());
};

// --- a list of the caller's own ---------------------------------------------
//
// The box over something that is not the registry, which is what the theme
// picker is. Everything above is the command case; what changes here is that the
// items carry their own actions, that highlighting one *shows* it, and that a
// dismissal has to put back what the showing changed.

namespace
{
// Three items that record what was previewed and what was run.
//
// The previews are recorded as one joined string rather than as a list, and that
// is not tidiness: a count that fails would be followed by an index off the end
// of it, and a mutation that kills a test by crashing it has not been checked —
// §7. One comparison of one string cannot do that, and it says which previews
// ran and in what order, which is what two of these tests are about.
struct PickFixture : Fixture
{
    eacp::Vector<PaletteItem> items(int count)
    {
        auto list = eacp::Vector<PaletteItem> {};

        for (auto index = 0; index < count; ++index)
        {
            const auto name = "Theme " + std::to_string(index);

            auto item = PaletteItem {};

            item.title = name;
            item.hint = index == 0 ? "current" : "";
            item.run = [this, name] { chosen = name; };

            item.preview = [this, name]
            { previews += (previews.empty() ? "" : ", ") + name; };

            list.push_back(std::move(item));
        }

        return list;
    }

    void pick(int count = 3)
    {
        palette.show(items(count), "Select Color Theme", [this] { ++restores; });
        host.setFocus(&palette.keyboardTarget());
    }

    std::string previews;
    std::string chosen;
    int restores = 0;
};
} // namespace

auto tPickOffersItsOwnList =
    test("Palette/offersTheListItWasGivenRatherThanTheRegistry") = []
{
    auto fixture = PickFixture {};
    fixture.pick();

    check(fixture.palette.entries().size() == 3);
    check(fixture.titleOf(0) == "Theme 0");
    check(fixture.hintOf(0) == "current");

    // The registry is still there and still has commands in it — this is the
    // same widget, opened over something else.
    check(fixture.registry.commands().size() == 4);

    fixture.type("2");

    check(fixture.palette.entries().size() == 1);
    check(fixture.selectedTitle() == "Theme 2");
};

// Opening previews nothing: the caller is already showing what the first row
// stands for. The expensive direction is a picker that changes the window the
// moment it opens, before anyone has chosen anything.
auto tPickOpeningPreviewsNothing = test("Palette/openingAPickerPreviewsNothing") = []
{
    auto fixture = PickFixture {};
    fixture.pick();

    check(fixture.previews.empty());
};

// The peek: arrowing shows each row, Enter keeps what is showing, and the
// restore is *not* run — the thing it would put back is what was just chosen.
auto tPickEnterCommits = test("Palette/enterKeepsWhatTheHighlightWasShowing") = []
{
    auto fixture = PickFixture {};
    fixture.pick();

    fixture.press(Graphics::KeyCode::DownArrow, "");
    fixture.press(Graphics::KeyCode::DownArrow, "");

    check(fixture.previews == "Theme 1, Theme 2");

    fixture.press(Graphics::KeyCode::Return, "\r");

    check(fixture.chosen == "Theme 2");
    check(fixture.restores == 0);
    check(!fixture.palette.isOpen());
};

// And the other half, which is the half a preview cannot do without: Escape
// puts back what was there and runs nothing.
auto tPickEscapeRestores = test("Palette/escapeUndoesWhatThePreviewsChanged") = []
{
    auto fixture = PickFixture {};
    fixture.pick();

    fixture.press(Graphics::KeyCode::DownArrow, "");
    fixture.press(Graphics::KeyCode::Escape, "\x1b");

    check(fixture.chosen.empty());
    check(fixture.restores == 1);
    check(!fixture.palette.isOpen());
};

// A click outside is a dismissal like any other, and has to undo as much.
auto tPickClickAwayRestores = test("Palette/aClickOutsideAPickerUndoesItToo") = []
{
    auto fixture = PickFixture {};
    fixture.pick();

    fixture.press(Graphics::KeyCode::DownArrow, "");
    fixture.palette.mouseDown(mouseAt(20.f, 700.f));

    check(fixture.restores == 1);
};

// The case a preview hung off the *row* cannot see. Typing narrows the list
// while the highlight stays on row 0, so the row does not move and the item
// under it does — and a picker that only heard about moved rows would go on
// showing whatever was highlighted before the query.
auto tPickPreviewsWhatTheQueryLandsOn =
    test("Palette/aQueryThatChangesTheHighlightedItemPreviewsIt") = []
{
    auto fixture = PickFixture {};
    fixture.pick();

    fixture.type("2");

    check(fixture.palette.selectedEntry() == 0);
    check(fixture.previews == "Theme 2");
};

// What the theme picker opens with: the value already in force, wherever it
// sits in the list. Its own preview runs — the caller is what decides that
// re-showing the current value is a no-op — and nothing before it does.
auto tPickSelectItem = test("Palette/selectItemOpensOnAValueOtherThanTheFirst") = []
{
    auto fixture = PickFixture {};
    fixture.pick();

    fixture.palette.selectItem(2);

    check(fixture.palette.selectedEntry() == 2);
    check(fixture.previews == "Theme 2");
};

// The undo belongs to the opening, not to the widget. A command palette opened
// after a picker was dismissed must not inherit the picker's restore — Escape
// out of ⌘⇧P would otherwise put the theme back to whatever the picker had been
// opened over, minutes later and with nothing to connect the two.
auto tPickRestoreDoesNotOutliveTheOpening =
    test("Palette/aCommandPaletteDoesNotInheritAPickersUndo") = []
{
    auto fixture = PickFixture {};

    fixture.pick();
    fixture.press(Graphics::KeyCode::Escape, "\x1b");

    check(fixture.restores == 1);

    fixture.show();
    fixture.press(Graphics::KeyCode::Escape, "\x1b");

    check(fixture.restores == 1);
};
