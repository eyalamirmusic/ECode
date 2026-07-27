#include <ECodeUI/Keymap.h>

#include <NanoTest/NanoTest.h>

// Chord parsing, and the one decision that matters here: which field of a key
// event identifies the key. Letters come from the character so a binding
// follows the layout's legend; punctuation comes from the code because the
// character it produces changes under Shift and under a non-US layout.
//
// Both halves of that split have a test that fails if the other rule were used,
// because either mistake is silent — the binding simply never fires.

using namespace nano;
using namespace ecode;
using namespace eacp;

namespace
{
Graphics::KeyEvent keyEvent(std::uint16_t code, std::string characters)
{
    auto event = Graphics::KeyEvent {};

    event.keyCode = code;
    event.charactersIgnoringModifiers = characters;
    event.characters = std::move(characters);

    return event;
}
} // namespace

// --- parsing ----------------------------------------------------------------

auto tChordParses = test("Chord/parsesModifiersAndKey") = []
{
    const auto chord = Chord::parse("cmd+shift+p");

    check(chord.key == "p");
    check(chord.modifiers.command);
    check(chord.modifiers.shift);
    check(!chord.modifiers.alt);
    check(!chord.modifiers.control);
};

auto tChordParsesAliases = test("Chord/acceptsModifierAliasesAndAnyCase") = []
{
    check(Chord::parse("Command+Option+K") == Chord::parse("cmd+alt+k"));
    check(Chord::parse("CTRL+A") == Chord::parse("control+a"));
    check(Chord::parse("meta+s") == Chord::parse("cmd+s"));
};

auto tChordParsesNamedKeys = test("Chord/parsesKeysThatHaveNoCharacter") = []
{
    check(Chord::parse("escape").key == "escape");
    check(Chord::parse("cmd+pagedown").key == "pagedown");
    check(Chord::parse("f5").key == "f5");
};

// "+" is both the separator and a key.
auto tChordParsesPlus = test("Chord/parsesThePlusKey") = []
{
    const auto chord = Chord::parse("cmd++");

    check(chord.key == "+");
    check(chord.modifiers.command);
};

auto tChordRejectsNonsense = test("Chord/aChordWithNoKeyIsInvalid") = []
{
    check(!Chord::parse("").isValid());
    check(!Chord::parse("cmd").isValid());
    check(!Chord::parse("cmd+shift").isValid());
};

// A misspelt modifier, which is the failure a hand-edited settings file makes
// possible and the one it can least afford. Nothing distinguishes "cmmd" from a
// key name, so the lenient reading is the *bare* key "k" — and a bare binding is
// matched before the document sees the keystroke, so one typo in the file would
// leave the editor unable to type a letter with nothing anywhere saying why.
//
// The second check is what separates rejecting the chord from merely ignoring
// the token: taking "cmmd" as a stray modifier would leave a working ⌘K.
auto tChordRejectsASecondKey = test("Chord/aMisspeltModifierIsNotABareKey") = []
{
    check(!Chord::parse("cmmd+k").isValid());
    check(Chord::parse("cmmd+k") != Chord::parse("cmd+k"));

    check(!Chord::parse("a+b").isValid());
    check(!Chord::parse("cmd+s+escape").isValid());

    // And the one spelling where a key legitimately arrives before a token that
    // is not the last, which parse() takes off the end before splitting at all.
    check(Chord::parse("cmd++").isValid());
};

// --- events -----------------------------------------------------------------

// macOS folds Shift into the character, so Cmd+Shift+P arrives as "P". Left
// alone, the chord would be "P" and no binding written in lower case would ever
// match it — every shifted binding in the app, dead and silent.
auto tEventNormalizesShift = test("Chord/foldsShiftOutOfTheCharacter") = []
{
    auto event = keyEvent(Graphics::KeyCode::P, "P");
    event.modifiers.command = true;
    event.modifiers.shift = true;

    check(Chord::fromEvent(event) == Chord::parse("cmd+shift+p"));
};

// Letters are identified by the character, not the code. A Dvorak keyboard
// reports the *position* of QWERTY's Z as the code for undo's neighbours, so
// matching on the code would put undo under whichever key happens to sit there.
auto tEventLettersComeFromTheCharacter =
    test("Chord/identifiesLettersByCharacter") = []
{
    // The physical key at QWERTY's ";" position, which on Dvorak types "z".
    auto event = keyEvent(Graphics::KeyCode::Semicolon, "z");
    event.modifiers.command = true;

    const auto chord = Chord::fromEvent(event);

    check(chord == Chord::parse("cmd+z"));
    check(chord != Chord::parse("cmd+;"));
};

// And punctuation the other way round. Cmd+Shift+/ produces "?", so a chord
// taken from the character would not match a binding written "/" — which is
// exactly why eacp names its punctuation key codes for the *unshifted* key.
auto tEventPunctuationComesFromTheCode =
    test("Chord/identifiesPunctuationByKeyCode") = []
{
    auto event = keyEvent(Graphics::KeyCode::Slash, "?");
    event.modifiers.command = true;
    event.modifiers.shift = true;

    check(Chord::fromEvent(event) == Chord::parse("cmd+shift+/"));
};

// Escape's `characters` is a control code, so a chord built from it would be
// unprintable rather than "escape".
auto tEventNamedKeys = test("Chord/identifiesNamedKeysByKeyCode") = []
{
    check(Chord::fromEvent(keyEvent(Graphics::KeyCode::Escape, "\x1b"))
          == Chord::parse("escape"));

    check(Chord::fromEvent(keyEvent(Graphics::KeyCode::Return, "\r"))
          == Chord::parse("enter"));

    check(Chord::fromEvent(keyEvent(Graphics::KeyCode::UpArrow, ""))
          == Chord::parse("up"));
};

// --- display ----------------------------------------------------------------

auto tChordDisplays = test("Chord/printsInMacOSOrder") = []
{
    check(Chord::parse("cmd+shift+p").display() == "⇧⌘P");
    check(Chord::parse("cmd+s").display() == "⌘S");
    check(Chord::parse("ctrl+alt+shift+cmd+a").display() == "⌃⌥⇧⌘A");
    check(Chord::parse("escape").display() == "⎋");
    check(Chord::parse("cmd+up").display() == "⌘↑");
};

// --- the keymap -------------------------------------------------------------

auto tKeymapResolves = test("Keymap/resolvesABoundChord") = []
{
    auto keymap = Keymap {};
    keymap.bind("cmd+s", "file.save");

    auto event = keyEvent(Graphics::KeyCode::S, "s");
    event.modifiers.command = true;

    check(keymap.commandFor(event) == "file.save");
};

auto tKeymapUnbound = test("Keymap/anUnboundChordResolvesToNothing") = []
{
    auto keymap = Keymap {};
    keymap.bind("cmd+s", "file.save");

    // Same key, different modifiers — a chord is the whole combination.
    auto event = keyEvent(Graphics::KeyCode::S, "s");
    event.modifiers.command = true;
    event.modifiers.shift = true;

    check(keymap.commandFor(event).empty());
};

// An unparseable binding is dropped rather than stored as a chord that could
// never match — otherwise it would sit in the table shadowing nothing and
// showing up in chordFor as a shortcut that does not exist.
auto tKeymapDropsInvalid = test("Keymap/dropsAnUnparseableBinding") = []
{
    auto keymap = Keymap {};
    keymap.bind("cmd+shift", "file.save");

    check(keymap.bindings().empty());
};

// Later wins, which is what lets a user keymap be appended after the defaults
// rather than merged into them.
auto tKeymapLaterBindingWins = test("Keymap/aLaterBindingShadowsAnEarlierOne") = []
{
    auto keymap = Keymap {};
    keymap.bind("cmd+s", "file.save");
    keymap.bind("cmd+s", "file.saveAll");

    check(keymap.commandFor(Chord::parse("cmd+s")) == "file.saveAll");
};

auto tKeymapChordForDisplay = test("Keymap/reportsTheChordThatRunsACommand") = []
{
    auto keymap = Keymap {};
    keymap.bind("cmd+shift+p", "workbench.showPalette");

    check(keymap.chordFor("workbench.showPalette").display() == "⇧⌘P");
    check(!keymap.chordFor("file.save").isValid());
};

// The fold: a shadowed binding still names its command, so a chordFor that only
// searched for the id would report a shortcut that no longer runs it. The
// palette prints that string next to the command, so the failure is an
// instruction to press a key that does something else.
auto tKeymapDoesNotReportAShadowedChord =
    test("Keymap/doesNotReportAChordThatWasRebound") = []
{
    auto keymap = Keymap {};
    keymap.bind("cmd+s", "file.save");
    keymap.bind("cmd+s", "file.saveAll");

    check(!keymap.chordFor("file.save").isValid());
    check(keymap.chordFor("file.saveAll").display() == "⌘S");
};

// A command bound twice reports the binding that is actually in force.
auto tKeymapReportsTheLiveBinding =
    test("Keymap/reportsTheLastLiveBindingForACommand") = []
{
    auto keymap = Keymap {};
    keymap.bind("cmd+s", "file.save");
    keymap.bind("cmd+w", "file.save");

    check(keymap.chordFor("file.save").display() == "⌘W");
};

// Taking a binding away, which the settings file spells as an empty command id.
// It is the shadowing rule rather than a second mechanism, and the two halves
// are what make that indistinguishable from never having been bound: the chord
// resolves to nothing, and nothing offers the chord.
auto tKeymapEmptyIdUnbinds = test("Keymap/anEmptyCommandIdTakesTheChordAway") = []
{
    auto keymap = Keymap {};
    keymap.bind("cmd+s", "file.save");
    keymap.bind("cmd+s", "");

    check(keymap.commandFor(Chord::parse("cmd+s")).empty());
    check(!keymap.chordFor("file.save").isValid());
};

// --- equality ---------------------------------------------------------------

// What tells the application whether a reloaded settings file moved a binding,
// and so whether the menu bar has to be built again. The case that matters is
// the second one: a keymap that compared by size, or not at all, would rebuild
// the bar on every save of the file and would do it while a menu could be open.
auto tKeymapEquality = test("Keymap/comparesBindingForBinding") = []
{
    auto one = Keymap {};
    one.bind("cmd+s", "file.save");

    auto same = Keymap {};
    same.bind("cmd+s", "file.save");

    check(one == same);

    auto rebound = Keymap {};
    rebound.bind("cmd+s", "file.saveAs");

    check(one != rebound);

    auto moved = Keymap {};
    moved.bind("cmd+w", "file.save");

    check(one != moved);

    // Order is part of it, because order is what decides which binding wins.
    auto ordered = Keymap {};
    ordered.bind("cmd+s", "file.save");
    ordered.bind("cmd+s", "file.close");

    auto reversed = Keymap {};
    reversed.bind("cmd+s", "file.close");
    reversed.bind("cmd+s", "file.save");

    check(ordered != reversed);
};

// --- the default table ------------------------------------------------------

// The table lives here rather than in the application so that this can read it,
// and the property worth pinning is not which chord is which — it is that every
// binding in it survived parsing. A chord that does not parse is dropped
// silently, so a typo in the table is a shortcut that simply never fires.
auto tDefaultKeymapIsWhole = test("Keymap/everyDefaultBindingParsed") = []
{
    const auto keymap = defaultKeymap();

    check(!keymap.bindings().empty());

    for (const auto& binding: keymap.bindings())
    {
        check(binding.chord.isValid());
        check(!binding.commandId.empty());
    }

    // Counted, because "every binding in the table parsed" is also true of a
    // table that lost half of itself: bind() drops what it cannot read, so the
    // loop above only ever sees the survivors.
    check(keymap.bindings().size() >= 30);
};

// A shortcut nothing can reach. One command taking two chords is not an error —
// ⌘= is deliberately bound twice, with and without the shift — but a default
// whose chord a *later* default takes away is one, because then the palette and
// the menu print nothing beside a command that has a binding written for it.
auto tDefaultKeymapHasNoDeadBindings = test("Keymap/noDefaultShadowsAnother") = []
{
    const auto keymap = defaultKeymap();

    for (const auto& binding: keymap.bindings())
        check(keymap.chordFor(binding.commandId).isValid());
};
