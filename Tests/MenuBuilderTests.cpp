#include <ECodeUI/MenuBuilder.h>
#include <ECodeUI/TextField.h>
#include <ECodeUI/WidgetHost.h>

#include <NanoTest/NanoTest.h>

#include <string>

// The menu bar, which holds no strings of its own: every item takes its title
// and availability from the command registry and its shortcut from the keymap.
//
// The half worth the most attention is the last one. A menu shortcut is matched
// by macOS against the menu bar *before* the window is sent a key down, so
// putting Paste in the Edit menu changes how ⌘V arrives — as a command rather
// than a keystroke. Everything that used to decide what ⌘V meant looked at a
// key event, and none of it runs any more.

using namespace nano;
using namespace ecode;
using namespace eacp;

namespace
{
struct Fixture
{
    Fixture()
    {
        commands.add({"file.save", "File: Save", [this] { ran = "file.save"; }});

        commands.add({"file.revert",
                      "File: Revert File",
                      [this] { ran = "file.revert"; },
                      [this] { return canRevert; }});

        commands.add({"edit.copy", "Edit: Copy", [this] { ran = "edit.copy"; }});

        // No category prefix, like the find bar's own entries.
        commands.add({"find.show", "Find", [this] { ran = "find.show"; }});

        keymap.bind("cmd+s", "file.save");
        keymap.bind("cmd+shift+z", "edit.redo");
        keymap.bind("escape", "find.close");
    }

    Vector<MenuSpec> fileMenu() const
    {
        return {
            MenuSpec {"File", {"file.save", MenuSpec::separator(), "file.revert"}}};
    }

    CommandRegistry commands;
    Keymap keymap;

    std::string ran;
    bool canRevert = false;

    CommandDispatch dispatch = [this](std::string_view id)
    { dispatched = std::string {id}; };

    std::string dispatched;
};

const Graphics::MenuItem* itemNamed(const Graphics::Menu& menu,
                                    std::string_view title)
{
    for (const auto& item: menu.items)
        if (item.title == title)
            return &item;

    return nullptr;
}

bool hasCommand(const MenuSpec& spec, std::string_view id)
{
    for (const auto& candidate: spec.commandIds)
        if (candidate == id)
            return true;

    return false;
}
} // namespace

// --- key equivalents --------------------------------------------------------

auto tSingleCharacterConverts = test("MenuBuilder/singleCharacterConverts") = []
{
    const auto equivalent = toKeyEquivalent(Chord::parse("cmd+s"));

    check(equivalent.has_value());
    check(equivalent->key == "s");
    check(equivalent->modifiers.command);
    check(!equivalent->modifiers.shift);
};

// Shift stays a modifier rather than capitalising the key. AppKit takes the
// equivalent and the mask separately, and folding shift into the character
// would give the same chord two spellings — which is the same reason Chord
// normalises it out in the first place.
auto tShiftStaysAModifier = test("MenuBuilder/shiftStaysAModifier") = []
{
    const auto equivalent = toKeyEquivalent(Chord::parse("cmd+shift+p"));

    check(equivalent.has_value());
    check(equivalent->key == "p");
    check(equivalent->modifiers.command);
    check(equivalent->modifiers.shift);
};

auto tAltIsCarried = test("MenuBuilder/altIsCarried") = []
{
    const auto equivalent = toKeyEquivalent(Chord::parse("cmd+alt+f"));

    check(equivalent.has_value());
    check(equivalent->key == "f");
    check(equivalent->modifiers.alt);
};

// A named key has no single character to be an equivalent, so it gets none.
// This is the case that must not be approximated: an item claiming "e" for
// Escape would bind ⌘E to it and take that chord away from whatever holds it.
auto tNamedKeyHasNoEquivalent = test("MenuBuilder/namedKeyHasNoEquivalent") = []
{
    check(!toKeyEquivalent(Chord::parse("escape")).has_value());
    check(!toKeyEquivalent(Chord::parse("cmd+pageup")).has_value());
    check(!toKeyEquivalent(Chord::parse("f5")).has_value());
};

// A chord with no key at all — modifiers alone are not a shortcut.
auto tInvalidChordHasNoEquivalent =
    test("MenuBuilder/invalidChordHasNoEquivalent") = []
{
    check(!toKeyEquivalent(Chord {}).has_value());
    check(!toKeyEquivalent(Chord::parse("cmd")).has_value());
    check(!toKeyEquivalent(Chord::parse("")).has_value());
};

// Punctuation converts like any other single character, which matters because
// "+" is also the separator a binding is written with: "cmd+" is ⌘ and the plus
// key, not a malformed ⌘. Worth pinning here rather than assuming, since a
// conversion that dropped it would silently cost the item its shortcut.
auto tPunctuationConverts = test("MenuBuilder/punctuationConverts") = []
{
    const auto plus = toKeyEquivalent(Chord::parse("cmd+"));

    check(plus.has_value());
    check(plus->key == "+");
    check(plus->modifiers.command);

    const auto slash = toKeyEquivalent(Chord::parse("cmd+/"));

    check(slash.has_value());
    check(slash->key == "/");
};

// --- building ---------------------------------------------------------------

auto tItemsTakeTitleFromRegistry =
    test("MenuBuilder/itemsTakeTitleFromRegistry") = []
{
    auto fixture = Fixture {};

    const auto bar = buildMenuBar(
        fixture.fileMenu(), fixture.commands, fixture.keymap, fixture.dispatch);

    check(bar.menus.size() == 1);
    check(bar.menus[0].title == "File");

    // Three entries: two commands and the separator between them.
    check(bar.menus[0].items.size() == 3);
    check(bar.menus[0].items[1].isSeparator);
};

// "File: Save" in the palette is "Save" in the File menu. The palette is one
// flat list where the category is how a command is found; a menu already says
// which one it is.
auto tCategoryPrefixIsStripped = test("MenuBuilder/categoryPrefixIsStripped") = []
{
    auto fixture = Fixture {};

    const auto bar = buildMenuBar(
        fixture.fileMenu(), fixture.commands, fixture.keymap, fixture.dispatch);

    check(itemNamed(bar.menus[0], "Save") != nullptr);
    check(itemNamed(bar.menus[0], "Revert File") != nullptr);
    check(itemNamed(bar.menus[0], "File: Save") == nullptr);
};

// Only the menu's own name is stripped. The same command under a different
// menu keeps its title whole, which is what stops this being a blind split on
// the first colon.
auto tForeignPrefixIsKept = test("MenuBuilder/foreignPrefixIsKept") = []
{
    auto fixture = Fixture {};

    const auto specs = Vector<MenuSpec> {MenuSpec {"Edit", {"file.save"}}};

    const auto bar =
        buildMenuBar(specs, fixture.commands, fixture.keymap, fixture.dispatch);

    check(itemNamed(bar.menus[0], "File: Save") != nullptr);
    check(itemNamed(bar.menus[0], "Save") == nullptr);
};

auto tTitleWithoutCategoryIsUntouched =
    test("MenuBuilder/titleWithoutCategoryIsUntouched") = []
{
    auto fixture = Fixture {};

    const auto specs = Vector<MenuSpec> {MenuSpec {"Find", {"find.show"}}};

    const auto bar =
        buildMenuBar(specs, fixture.commands, fixture.keymap, fixture.dispatch);

    check(itemNamed(bar.menus[0], "Find") != nullptr);
};

auto tShortcutComesFromKeymap = test("MenuBuilder/shortcutComesFromKeymap") = []
{
    auto fixture = Fixture {};

    const auto bar = buildMenuBar(
        fixture.fileMenu(), fixture.commands, fixture.keymap, fixture.dispatch);

    const auto* save = itemNamed(bar.menus[0], "Save");

    check(save != nullptr);
    check(save->shortcut.has_value());
    check(save->shortcut->key == "s");
    check(save->shortcut->modifiers.command);

    // Nothing is bound to revert, so it prints no shortcut rather than a wrong
    // one.
    const auto* revert = itemNamed(bar.menus[0], "Revert File");

    check(revert != nullptr);
    check(!revert->shortcut.has_value());
};

// A command the registry never got is left out entirely. Showing it would
// advertise something that cannot run; the id is reported by unknownCommandIds
// instead.
auto tUnknownCommandIsSkipped = test("MenuBuilder/unknownCommandIsSkipped") = []
{
    auto fixture = Fixture {};

    const auto specs =
        Vector<MenuSpec> {MenuSpec {"File", {"file.save", "file.doesNotExist"}}};

    const auto bar =
        buildMenuBar(specs, fixture.commands, fixture.keymap, fixture.dispatch);

    check(bar.menus[0].items.size() == 1);
    check(itemNamed(bar.menus[0], "Save") != nullptr);
};

auto tUnknownIdsAreReported = test("MenuBuilder/unknownIdsAreReported") = []
{
    auto fixture = Fixture {};

    const auto specs = Vector<MenuSpec> {
        MenuSpec {"File", {"file.save", MenuSpec::separator(), "file.typo"}}};

    const auto missing = unknownCommandIds(specs, fixture.commands);

    check(missing.size() == 1);
    check(missing[0] == "file.typo");

    // A separator is not an id and must not be reported as a missing one.
    check(unknownCommandIds(fixture.fileMenu(), fixture.commands).size() == 0);
};

// The item runs through the dispatcher rather than the registry. This is the
// property the whole design turns on — see the focus tests below for why.
auto tItemDispatchesById = test("MenuBuilder/itemDispatchesById") = []
{
    auto fixture = Fixture {};

    const auto bar = buildMenuBar(
        fixture.fileMenu(), fixture.commands, fixture.keymap, fixture.dispatch);

    itemNamed(bar.menus[0], "Save")->action();

    check(fixture.dispatched == "file.save");

    // Straight past the registry: the command itself did not run here.
    check(fixture.ran.empty());
};

// The dispatcher is copied into the item, not referenced, so a caller passing a
// temporary does not leave the menu holding a dangling reference — and the
// platform keeps these lambdas for as long as the bar is installed.
auto tDispatcherIsCopied = test("MenuBuilder/dispatcherIsCopied") = []
{
    auto fixture = Fixture {};
    auto seen = std::string {};

    const auto bar =
        buildMenuBar(fixture.fileMenu(),
                     fixture.commands,
                     fixture.keymap,
                     [&seen](std::string_view id) { seen = std::string {id}; });

    itemNamed(bar.menus[0], "Save")->action();

    check(seen == "file.save");
};

// Availability is read when the menu opens, not when it was built. Without
// this an app would have to rebuild its menu bar on every state change to keep
// the greying honest.
auto tEnablementIsReadLive = test("MenuBuilder/enablementIsReadLive") = []
{
    auto fixture = Fixture {};

    const auto bar = buildMenuBar(
        fixture.fileMenu(), fixture.commands, fixture.keymap, fixture.dispatch);

    const auto* revert = itemNamed(bar.menus[0], "Revert File");

    check(revert != nullptr);
    check(!revert->isEnabled());

    fixture.canRevert = true;

    // Same item, no rebuild.
    check(revert->isEnabled());
};

auto tCommandWithoutPredicateIsEnabled =
    test("MenuBuilder/commandWithoutPredicateIsEnabled") = []
{
    auto fixture = Fixture {};

    const auto bar = buildMenuBar(
        fixture.fileMenu(), fixture.commands, fixture.keymap, fixture.dispatch);

    check(itemNamed(bar.menus[0], "Save")->isEnabled());
};

// --- the default menus ------------------------------------------------------

auto tDefaultMenusAreOrdered = test("MenuBuilder/defaultMenusAreOrdered") = []
{
    const auto menus = defaultMenus();

    check(menus.size() == 4);
    check(menus[0].title == "File");
    check(menus[1].title == "Edit");
    check(menus[2].title == "Find");
    check(menus[3].title == "View");
};

// Windows has no application menu to carry Quit — eacp's standardApplicationMenu
// is empty there and the empty menu is dropped from the bar — so the File menu
// is the only way out of the app and Exit has to be in it.
auto tExitIsLastInFileMenu = test("MenuBuilder/exitIsLastInFileMenu") = []
{
    const auto file = defaultMenus(true)[0];

    check(file.commandIds.back() == "file.exit");

    // Set off from the file operations above it, the way Windows separates it.
    check(file.commandIds[file.commandIds.size() - 2] == MenuSpec::separator());
};

// And is absent otherwise, rather than shown everywhere. macOS already has Quit
// in the application menu, so a File ▸ Exit beside it would be a second and
// non-standard way to do the identical thing.
auto tExitIsOmittedWithoutIt = test("MenuBuilder/exitIsOmittedWithoutIt") = []
{
    const auto menus = defaultMenus(false);

    check(!hasCommand(menus[0], "file.exit"));

    // Nowhere else either — this is the only menu it belongs in.
    for (const auto& menu: menus)
        check(!hasCommand(menu, "file.exit"));
};

// The default is the platform's, which is the whole point of the parameter
// having one: App calls defaultMenus() with no argument.
auto tExitDefaultsToWindows = test("MenuBuilder/exitDefaultsToWindows") = []
{
    check(hasCommand(defaultMenus()[0], "file.exit") == Platform::isWindows());
};

// The four the Edit menu offers are exactly the four a focused text box claims,
// so every one of them can arrive at a field instead of the document. Pinned
// because adding a fifth to either list without the other is silent.
auto tEditMenuMatchesFieldCommands =
    test("MenuBuilder/editMenuMatchesFieldCommands") = []
{
    const auto menus = defaultMenus();

    auto found = 0;

    for (const auto& id: menus[1].commandIds)
        if (id == commands::editCut || id == commands::editCopy
            || id == commands::editPaste || id == commands::editSelectAll)
            ++found;

    check(found == 4);
};

// --- focus precedence -------------------------------------------------------

// The reason menu items dispatch rather than run. A focused field claims the
// editing commands, so ⌘V from the Edit menu reaches the query box rather than
// the file behind it.
auto tFocusedFieldClaimsEditing = test("MenuBuilder/focusedFieldClaimsEditing") = []
{
    auto theme = ChromeTheme {};
    auto field = TextField {theme};
    auto host = WidgetHost {};

    field.setBounds({0.f, 0.f, 200.f, 30.f});
    host.setRoot(field);
    host.setFocus(&field);

    check(host.runCommandOnFocus(commands::editSelectAll));
    check(host.runCommandOnFocus(commands::editCopy));
    check(host.runCommandOnFocus(commands::editCut));
    check(host.runCommandOnFocus(commands::editPaste));
};

// And claims them even with nothing selected. Returning false here would hand
// ⌘C to the application, which would copy from the document while the caret is
// visibly in the box — the failure is silent and in the direction that costs
// something, since the clipboard quietly holds the wrong thing.
auto tEmptyFieldStillClaimsEditing =
    test("MenuBuilder/emptyFieldStillClaimsEditing") = []
{
    auto theme = ChromeTheme {};
    auto field = TextField {theme};
    auto host = WidgetHost {};

    field.setBounds({0.f, 0.f, 200.f, 30.f});
    host.setRoot(field);
    host.setFocus(&field);

    check(!field.hasSelection());
    check(host.runCommandOnFocus(commands::editCopy));
    check(host.runCommandOnFocus(commands::editCut));
};

// Everything else falls through, or a find field would answer for Undo and the
// document would never see it.
auto tFieldPassesOtherCommandsOn =
    test("MenuBuilder/fieldPassesOtherCommandsOn") = []
{
    auto theme = ChromeTheme {};
    auto field = TextField {theme};
    auto host = WidgetHost {};

    field.setBounds({0.f, 0.f, 200.f, 30.f});
    host.setRoot(field);
    host.setFocus(&field);

    check(!host.runCommandOnFocus("edit.undo"));
    check(!host.runCommandOnFocus("file.save"));
    check(!host.runCommandOnFocus("find.next"));
};

// With focus anywhere that is not a text box, the application takes everything.
auto tNonTextFocusClaimsNothing = test("MenuBuilder/nonTextFocusClaimsNothing") = []
{
    auto host = WidgetHost {};
    auto root = Widget {};

    root.setBounds({0.f, 0.f, 200.f, 30.f});
    host.setRoot(root);
    host.setFocus(&root);

    check(!host.runCommandOnFocus(commands::editPaste));
    check(!host.runCommandOnFocus(commands::editCopy));
};

auto tNoFocusClaimsNothing = test("MenuBuilder/noFocusClaimsNothing") = []
{
    auto host = WidgetHost {};
    auto root = Widget {};

    host.setRoot(root);
    host.setFocus(nullptr);

    check(!host.runCommandOnFocus(commands::editPaste));
};

// The field's own parent must not answer for it: runCommandOnFocus asks the
// focused widget alone rather than bubbling the way keyDown does. A find bar
// holding a query box is not itself a text input, and letting the walk continue
// past the box would give the bar a say in commands meant for the document.
auto tCommandDoesNotBubble = test("MenuBuilder/commandDoesNotBubble") = []
{
    auto theme = ChromeTheme {};
    auto host = WidgetHost {};
    auto parent = Widget {};
    auto field = TextField {theme};

    parent.setBounds({0.f, 0.f, 200.f, 60.f});
    parent.addChild(field);
    field.setBounds({0.f, 0.f, 200.f, 30.f});

    host.setRoot(parent);
    host.setFocus(&parent);

    // Focus is on the parent, which is not a text box — so the field below it
    // is never asked.
    check(!host.runCommandOnFocus(commands::editPaste));
};
