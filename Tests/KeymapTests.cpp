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
auto tEventLettersComeFromTheCharacter = test("Chord/identifiesLettersByCharacter") = []
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
auto tEventPunctuationComesFromTheCode = test("Chord/identifiesPunctuationByKeyCode") = []
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
auto tKeymapReportsTheLiveBinding = test("Keymap/reportsTheLastLiveBindingForACommand") = []
{
    auto keymap = Keymap {};
    keymap.bind("cmd+s", "file.save");
    keymap.bind("cmd+w", "file.save");

    check(keymap.chordFor("file.save").display() == "⌘W");
};
