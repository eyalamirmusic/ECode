#include "Keymap.h"

#include <algorithm>
#include <cctype>

namespace ecode
{
using namespace eacp;

namespace
{
struct NamedKey
{
    std::uint16_t code;
    const char* name;
    const char* glyph;
};

// Every key that a binding cannot name by the character it produces, plus the
// punctuation that it should not — see the header for why those are here rather
// than matched as characters.
//
// The glyph column is what the palette prints. Keys with no conventional symbol
// fall back to their name, which is why several are empty.
constexpr NamedKey namedKeys[] = {
    {Graphics::KeyCode::Escape, "escape", "⎋"},
    {Graphics::KeyCode::Return, "enter", "↩"},
    {Graphics::KeyCode::Tab, "tab", "⇥"},
    {Graphics::KeyCode::Space, "space", "␣"},
    {Graphics::KeyCode::Delete, "backspace", "⌫"},
    {Graphics::KeyCode::ForwardDelete, "delete", "⌦"},

    {Graphics::KeyCode::UpArrow, "up", "↑"},
    {Graphics::KeyCode::DownArrow, "down", "↓"},
    {Graphics::KeyCode::LeftArrow, "left", "←"},
    {Graphics::KeyCode::RightArrow, "right", "→"},

    {Graphics::KeyCode::Home, "home", "↖"},
    {Graphics::KeyCode::End, "end", "↘"},
    {Graphics::KeyCode::PageUp, "pageup", "⇞"},
    {Graphics::KeyCode::PageDown, "pagedown", "⇟"},

    {Graphics::KeyCode::Minus, "-", ""},
    {Graphics::KeyCode::Equals, "=", ""},
    {Graphics::KeyCode::LeftBracket, "[", ""},
    {Graphics::KeyCode::RightBracket, "]", ""},
    {Graphics::KeyCode::Backslash, "\\", ""},
    {Graphics::KeyCode::Semicolon, ";", ""},
    {Graphics::KeyCode::Quote, "'", ""},
    {Graphics::KeyCode::Comma, ",", ""},
    {Graphics::KeyCode::Period, ".", ""},
    {Graphics::KeyCode::Slash, "/", ""},
    {Graphics::KeyCode::Grave, "`", ""},

    {Graphics::KeyCode::F1, "f1", ""},
    {Graphics::KeyCode::F2, "f2", ""},
    {Graphics::KeyCode::F3, "f3", ""},
    {Graphics::KeyCode::F4, "f4", ""},
    {Graphics::KeyCode::F5, "f5", ""},
    {Graphics::KeyCode::F6, "f6", ""},
    {Graphics::KeyCode::F7, "f7", ""},
    {Graphics::KeyCode::F8, "f8", ""},
    {Graphics::KeyCode::F9, "f9", ""},
    {Graphics::KeyCode::F10, "f10", ""},
    {Graphics::KeyCode::F11, "f11", ""},
    {Graphics::KeyCode::F12, "f12", ""},
};

bool isSpace(char c)
{
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

std::string toLower(std::string_view text)
{
    auto result = std::string {text};

    for (auto& c: result)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    return result;
}

const NamedKey* namedKeyForCode(std::uint16_t code)
{
    for (const auto& key: namedKeys)
        if (key.code == code)
            return &key;

    return nullptr;
}

const NamedKey* namedKeyForName(std::string_view name)
{
    for (const auto& key: namedKeys)
        if (name == key.name)
            return &key;

    return nullptr;
}

// True when the token named a modifier, which is how parse() tells a modifier
// apart from the key without caring which position it is in.
bool applyModifier(std::string_view token, Graphics::ModifierKeys& modifiers)
{
    if (token == "cmd" || token == "command" || token == "meta" || token == "super")
    {
        modifiers.command = true;
        return true;
    }

    if (token == "shift")
    {
        modifiers.shift = true;
        return true;
    }

    if (token == "alt" || token == "option" || token == "opt")
    {
        modifiers.alt = true;
        return true;
    }

    if (token == "ctrl" || token == "control")
    {
        modifiers.control = true;
        return true;
    }

    return false;
}
} // namespace

Chord Chord::parse(std::string_view text)
{
    auto chord = Chord {};
    auto lowered = toLower(text);

    // "+" is both the separator and a key, so a trailing one is the key and is
    // taken off before anything is split. Handling it here rather than in the
    // loop keeps the loop free of empty tokens entirely.
    if (!lowered.empty() && lowered.back() == '+')
    {
        chord.key = "+";
        lowered.pop_back();

        if (!lowered.empty() && lowered.back() == '+')
            lowered.pop_back();
    }

    std::size_t start = 0;

    while (start < lowered.size())
    {
        auto end = lowered.find('+', start);

        if (end == std::string::npos)
            end = lowered.size();

        const auto token = std::string_view {lowered}.substr(start, end - start);

        if (!token.empty() && !applyModifier(token, chord.modifiers))
        {
            // A second key in one chord, which is not a chord this can express
            // — and the way a misspelt modifier arrives. See the header: taken
            // as a key it would bind a bare letter, which is worse than nothing
            // because it is matched before the document.
            if (!chord.key.empty())
                return {};

            chord.key = token;
        }

        start = end + 1;
    }

    return chord;
}

Chord Chord::fromEvent(const Graphics::KeyEvent& event)
{
    auto chord = Chord {};
    chord.modifiers = event.modifiers;

    const auto text = toLower(event.charactersIgnoringModifiers);

    // The character comes first, but only when it is a letter or a digit. That
    // ordering is the whole rule, and it is load-bearing in both directions:
    //
    //   - Codes first would break the layouts the character rule exists for. On
    //     Dvorak the key at QWERTY's ";" position types "z", so undo would end
    //     up under whichever key sits at that position instead of under the one
    //     labelled Z.
    //   - Characters first *unconditionally* would break punctuation. Shift+/
    //     produces "?", which matches no binding written "/", and on a non-US
    //     layout the bracket keys produce something else again.
    //
    // Asking what the key produced rather than which rule to apply settles both:
    // a letter identifies itself, and anything else defers to the code.
    if (text.size() == 1 && std::isalnum(static_cast<unsigned char>(text[0])) != 0)
    {
        chord.key = text;
        return chord;
    }

    if (const auto* named = namedKeyForCode(event.keyCode))
    {
        chord.key = named->name;
        return chord;
    }

    // A key with no name and no alphanumeric character — a dead key, or
    // punctuation on a layout eacp's table does not reach. Whatever it produced
    // is still better than nothing, and it is at least stable.
    chord.key = text;

    return chord;
}

bool Chord::operator==(const Chord& other) const
{
    return key == other.key && modifiers.shift == other.modifiers.shift
           && modifiers.control == other.modifiers.control
           && modifiers.alt == other.modifiers.alt
           && modifiers.command == other.modifiers.command;
}

std::string Chord::display() const
{
    if (!isValid())
        return {};

    // Apple's order, which is the one every macOS menu prints and so the one a
    // person reads without having to decode it.
    auto text = std::string {};

    if (modifiers.control)
        text += "⌃";

    if (modifiers.alt)
        text += "⌥";

    if (modifiers.shift)
        text += "⇧";

    if (modifiers.command)
        text += "⌘";

    if (const auto* named = namedKeyForName(key);
        named != nullptr && named->glyph[0] != '\0')
        return text + named->glyph;

    // A single letter reads better capitalised — ⌘S, not ⌘s — and a multi-byte
    // or multi-character key (f5, a bracket) is left as it is.
    if (key.size() == 1)
        return text
               + static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));

    return text + key;
}

// --- ChordSequence -----------------------------------------------------------

ChordSequence ChordSequence::parse(std::string_view text)
{
    auto sequence = ChordSequence {};

    for (std::size_t start = 0; start < text.size();)
    {
        if (isSpace(text[start]))
        {
            ++start;
            continue;
        }

        auto end = start;

        while (end < text.size() && !isSpace(text[end]))
            ++end;

        const auto chord = Chord::parse(text.substr(start, end - start));

        // The whole sequence, or none of it. See the header: half of
        // "cmd+k cmd+t" is a bare ⌘K bound to the theme picker.
        if (!chord.isValid())
            return {};

        sequence.chords.add(chord);
        start = end;
    }

    return sequence;
}

std::string ChordSequence::display() const
{
    auto text = std::string {};

    for (const auto& chord: chords)
    {
        if (!text.empty())
            text += " ";

        text += chord.display();
    }

    return text;
}

Chord ChordSequence::single() const
{
    return chords.size() == 1 ? chords[0] : Chord {};
}

bool ChordSequence::continues(const ChordSequence& prefix) const
{
    if (chords.size() <= prefix.chords.size())
        return false;

    for (auto i = 0; i < prefix.chords.size(); ++i)
        if (chords[i] != prefix.chords[i])
            return false;

    return true;
}

// --- Keymap -----------------------------------------------------------------

void Keymap::bind(std::string_view chordText, std::string commandId)
{
    auto chords = ChordSequence::parse(chordText);

    if (!chords.isValid())
        return;

    list.push_back({std::move(chords), std::move(commandId)});
}

std::string_view Keymap::commandFor(const ChordSequence& chords) const
{
    // Backwards, so a binding appended later shadows an earlier one for the
    // same chords rather than being unreachable behind it.
    for (auto i = list.size(); i > 0; --i)
        if (list[i - 1].chords == chords)
            return list[i - 1].commandId;

    return {};
}

std::string_view Keymap::commandFor(const Chord& chord) const
{
    return commandFor(ChordSequence {{chord}});
}

std::string_view Keymap::commandFor(const Graphics::KeyEvent& event) const
{
    return commandFor(Chord::fromEvent(event));
}

bool Keymap::isPrefixOfABinding(const ChordSequence& prefix) const
{
    for (const auto& binding: list)
        if (binding.chords.continues(prefix))
            if (!commandFor(binding.chords).empty())
                return true;

    return false;
}

ChordSequence Keymap::shortcutFor(std::string_view commandId) const
{
    for (auto i = list.size(); i > 0; --i)
    {
        const auto& binding = list[i - 1];

        if (binding.commandId != commandId)
            continue;

        // Shadowed by a later binding of the same chords, so it is not what
        // runs this command any more and printing it would be a lie.
        if (commandFor(binding.chords) != commandId)
            continue;

        return binding.chords;
    }

    return {};
}

// --- ChordMatcher ------------------------------------------------------------

ChordMatcher::Match ChordMatcher::press(const Keymap& keymap, const Chord& chord)
{
    // Whatever it was waiting for or complaining about, this key answers it.
    unmatched = {};

    const auto wasPending = isPending();

    pending.chords.add(chord);

    // A prefix wins over a binding on the very same chords, because waiting is
    // the only state from which either of them can still happen — running the
    // short one at once would leave the long one unreachable, and nothing on
    // screen would say why. reportKeybindingProblems is what says it instead.
    if (keymap.isPrefixOfABinding(pending))
        return {Result::pending, {}};

    const auto attempted = pending;
    pending = {};

    if (const auto command = keymap.commandFor(attempted); !command.empty())
        return {Result::matched, command};

    if (!wasPending)
        return {Result::noMatch, {}};

    unmatched = attempted;

    return {Result::cancelled, {}};
}

void ChordMatcher::cancel()
{
    pending = {};
    unmatched = {};
}

std::string ChordMatcher::message() const
{
    if (pending.isValid())
        return pending.display() + " was pressed — waiting for the next key";

    if (unmatched.isValid())
        return unmatched.display() + " is not a command";

    return {};
}

// --- the default table -------------------------------------------------------

Keymap defaultKeymap()
{
    auto keymap = Keymap {};

    keymap.bind("cmd+shift+p", "workbench.showPalette");
    keymap.bind("cmd+n", "file.new");
    keymap.bind("cmd+o", "file.open");
    keymap.bind("cmd+shift+o", "file.openFolder");
    keymap.bind("cmd+s", "file.save");
    keymap.bind("cmd+shift+s", "file.saveAs");
    keymap.bind("cmd+w", "file.close");
    keymap.bind("cmd+z", "edit.undo");
    keymap.bind("cmd+shift+z", "edit.redo");
    keymap.bind("cmd+x", "edit.cut");
    keymap.bind("cmd+c", "edit.copy");
    keymap.bind("cmd+v", "edit.paste");
    keymap.bind("cmd+a", "edit.selectAll");
    keymap.bind("cmd+d", "edit.addNextOccurrence");
    keymap.bind("cmd+shift+l", "edit.selectAllOccurrences");

    // VSCode's chords, and like ⌃Tab they cannot become menu key equivalents:
    // toKeyEquivalent only converts single characters, so an arrow stays with
    // the keymap. That is the right side of the trade here — a key equivalent
    // is matched by macOS before the window sees the key, and ⌥⌘↑ has to reach
    // the editor.
    keymap.bind("cmd+alt+up", "edit.addCursorAbove");
    keymap.bind("cmd+alt+down", "edit.addCursorBelow");
    keymap.bind("cmd+f", "find.show");
    keymap.bind("cmd+alt+f", "find.showReplace");
    keymap.bind("cmd+g", "find.next");
    keymap.bind("cmd+shift+g", "find.previous");
    keymap.bind("cmd+1", "view.focusEditor");
    keymap.bind("cmd+shift+e", "view.focusExplorer");

    // VSCode's chords, and deliberately not expressible as menu key
    // equivalents: toKeyEquivalent only converts single characters, so "tab"
    // stays with the keymap and the menu item prints no shortcut rather than
    // claiming one macOS would match before the window.
    keymap.bind("ctrl+tab", "view.nextTab");
    keymap.bind("ctrl+shift+tab", "view.previousTab");

    // VSCode's chord for splitting, and unlike the arrows it *is* a single
    // character, so the menu takes it as a native key equivalent and macOS
    // matches it before the window. That is the right side of the trade for a
    // command that has no business reaching the document.
    keymap.bind("cmd+\\", "view.splitEditor");

    // VSCode's own spelling, and both of them: the sequence is the chord anyone
    // arriving from VSCode will press, and ⌥⌘← / → is two fewer keys for the
    // same thing. The sequence goes first because shortcutFor hands back the
    // later binding, and the pair is what the palette should advertise.
    //
    // The shifted arrows move the file rather than the focus, which is the same
    // shift-means-take-it-with-you the arrow keys already mean in the document.
    // VSCode has no sequence for those, so neither has this.
    keymap.bind("cmd+k cmd+right", "view.focusNextGroup");
    keymap.bind("cmd+k cmd+left", "view.focusPreviousGroup");
    keymap.bind("cmd+alt+right", "view.focusNextGroup");
    keymap.bind("cmd+alt+left", "view.focusPreviousGroup");
    keymap.bind("cmd+alt+shift+right", "view.moveEditorToNextGroup");
    keymap.bind("cmd+alt+shift+left", "view.moveEditorToPreviousGroup");

    // VSCode's chord for the theme picker, and the reason sequences were built:
    // there was no near-miss worth taking for it, so it went unbound and was
    // reached by name from the palette. A sequence can never be a menu key
    // equivalent, so the File menu still prints nothing beside it — the palette
    // is where it is advertised.
    keymap.bind("cmd+k cmd+t", "preferences.selectTheme");

    // ⌘+ is ⇧⌘= on a US layout, and people press it both ways — with the shift
    // because that is what the key is labelled, and without it because that is
    // what the key *is*. Both, in that order: the later binding is the one
    // shortcutFor hands back, so the menu and the palette print ⌘= rather than the
    // shifted spelling of the same thing.
    //
    // Named by their unshifted keys, which is what Chord::fromEvent matches
    // punctuation on — ⇧⌘= arrives as "+" and would match no binding at all if
    // these were written by the character.
    keymap.bind("cmd+shift+=", "view.increaseFontSize");
    keymap.bind("cmd+=", "view.increaseFontSize");
    keymap.bind("cmd+-", "view.decreaseFontSize");
    keymap.bind("cmd+0", "view.resetFontSize");

    // VSCode's chord, and the one place a binding without Command matters:
    // handleShortcut runs before the editor sees the key, so ⌥Z toggles
    // wrapping rather than typing the Ω that macOS resolves it to.
    keymap.bind("alt+z", "view.toggleWordWrap");

    return keymap;
}
} // namespace ecode
