#pragma once

#include <eacp/Graphics/Graphics.h>

#include <string>
#include <string_view>

namespace ecode
{
// One keystroke, in the form a binding names it and in the form an event
// arrives as.
//
// `key` is canonical and lower-case: a single character for keys that produce
// one ("p", "/"), a word for keys that do not ("escape", "pageup", "f5").
//
// Which of the two an event is identified by is the one real decision here, and
// it turns on what the key *produced* rather than on which key it was:
//
//   - **A letter or a digit comes from the character.** `Cmd+Z` should be undo
//     on the key the person's keyboard says Z, whatever layout they use, and
//     `charactersIgnoringModifiers` is the field that answers that. A key *code*
//     is a physical position on an ANSI board, so matching one would put undo
//     under whatever sits at QWERTY's Z on Dvorak.
//   - **Everything else comes from the key code.** The character is the wrong
//     answer for punctuation: `Cmd+Shift+/` arrives as "?" and would not match a
//     binding written `/`, and on a non-US layout the bracket keys produce
//     something else entirely. eacp's `KeyCode` names punctuation for its
//     *unshifted* key precisely so this works.
//
// The two rules collide on any layout where a key at a punctuation *position*
// types a letter, which is most of the non-QWERTY ones. Asking what came out
// settles it; asking which key it was does not.
//
// Shift is normalized out of the character — macOS folds it in, so
// `Cmd+Shift+P` arrives as "P" — and lives in `modifiers` alone. Otherwise the
// same chord would have two spellings and only one of them would match.
struct Chord
{
    std::string key;
    eacp::Graphics::ModifierKeys modifiers;

    // "cmd+shift+p", "ctrl+alt+delete", "escape". Case-insensitive, and each
    // modifier takes its common aliases (cmd/command/meta, alt/option/opt,
    // ctrl/control). An unparseable string gives an invalid chord rather than
    // throwing, so a bad line in a keymap costs that one binding.
    static Chord parse(std::string_view text);

    static Chord fromEvent(const eacp::Graphics::KeyEvent& event);

    bool isValid() const { return !key.empty(); }

    bool operator==(const Chord& other) const;
    bool operator!=(const Chord& other) const { return !(*this == other); }

    // "⌘⇧P" — what the palette prints beside a command. macOS order and macOS
    // glyphs, matching what the title bar already says elsewhere in the app.
    std::string display() const;
};

// Chords to command ids.
//
// Deliberately holds ids rather than callables: a binding for a command that
// does not exist is a dead entry rather than a dangling reference, and the same
// table can be read from a config file before the registry is populated.
class Keymap
{
public:
    struct Binding
    {
        Chord chord;
        std::string commandId;
    };

    // An unparseable chord is dropped. Binding the same chord twice keeps both
    // and the later one wins, which is what lets user bindings be appended
    // after the defaults instead of merged into them.
    void bind(std::string_view chord, std::string commandId);

    const eacp::Vector<Binding>& bindings() const { return list; }

    // Empty when the chord is unbound.
    std::string_view commandFor(const Chord& chord) const;
    std::string_view commandFor(const eacp::Graphics::KeyEvent& event) const;

    // The chord that currently runs this command, for display. Invalid when
    // there is none — including when the only binding for it has since been
    // shadowed by a later one, because a palette that advertises a shortcut
    // that no longer works is worse than one that advertises nothing.
    Chord chordFor(std::string_view commandId) const;

private:
    eacp::Vector<Binding> list;
};
} // namespace ecode
