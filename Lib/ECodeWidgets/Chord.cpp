#include "Chord.h"

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
} // namespace ecode
