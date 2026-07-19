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
            chord.key = token;

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

// --- Keymap -----------------------------------------------------------------

void Keymap::bind(std::string_view chordText, std::string commandId)
{
    auto chord = Chord::parse(chordText);

    if (!chord.isValid())
        return;

    list.push_back({std::move(chord), std::move(commandId)});
}

std::string_view Keymap::commandFor(const Chord& chord) const
{
    // Backwards, so a binding appended later shadows an earlier one for the
    // same chord rather than being unreachable behind it.
    for (auto i = list.size(); i > 0; --i)
        if (list[i - 1].chord == chord)
            return list[i - 1].commandId;

    return {};
}

std::string_view Keymap::commandFor(const Graphics::KeyEvent& event) const
{
    return commandFor(Chord::fromEvent(event));
}

Chord Keymap::chordFor(std::string_view commandId) const
{
    for (auto i = list.size(); i > 0; --i)
    {
        const auto& binding = list[i - 1];

        if (binding.commandId != commandId)
            continue;

        // Shadowed by a later binding of the same chord, so it is not what runs
        // this command any more and printing it would be a lie.
        if (commandFor(binding.chord) != commandId)
            continue;

        return binding.chord;
    }

    return {};
}
} // namespace ecode
