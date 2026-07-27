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
    //
    // Exactly one token may be the key, and that is stricter than it needs to
    // be for anything the app itself writes. It is for the settings file: a
    // misspelt modifier is a token like any other, so "cmmd+k" would otherwise
    // parse as the *bare* key "k" — and a bare binding is matched before the
    // document sees the key, so a typo would leave the editor unable to type a
    // letter with nothing on screen to say why.
    static Chord parse(std::string_view text);

    static Chord fromEvent(const eacp::Graphics::KeyEvent& event);

    bool isValid() const { return !key.empty(); }

    bool operator==(const Chord& other) const;
    bool operator!=(const Chord& other) const { return !(*this == other); }

    // "⌘⇧P" — what the palette prints beside a command. macOS order and macOS
    // glyphs, matching what the title bar already says elsewhere in the app.
    std::string display() const;
};

// One or more chords pressed in order: VSCode's ⌘K ⌘T, which a table keyed on a
// single chord cannot express at all.
//
// Written with a space between the chords, because that is the one separator
// "+" has not already claimed and it is the spelling a settings file has to be
// able to carry. Whitespace between them is otherwise ignored, so a file that
// lines its bindings up reads the same as one that does not.
struct ChordSequence
{
    eacp::Vector<Chord> chords;

    // Every chord in the text or none of them: one unreadable part fails the
    // whole sequence rather than binding the half that parsed. That half is a
    // *bare* ⌘K, and a keymap holding it would run the command the sequence
    // named on the first key — a typo in the second chord silently promoted to
    // a shortcut for something else.
    static ChordSequence parse(std::string_view text);

    bool isValid() const { return !chords.empty(); }

    // "⌘K ⌘T".
    std::string display() const;

    // The one chord when there is exactly one, and an invalid chord otherwise.
    //
    // Which is what keeps a sequence out of the menu bar: macOS matches a key
    // equivalent before the window is sent a key at all, so an item claiming
    // ⌘K would swallow the prefix and the second chord would never arrive. See
    // toKeyEquivalent.
    Chord single() const;

    // Whether this starts with `prefix` and is longer than it.
    bool continues(const ChordSequence& prefix) const;

    bool operator==(const ChordSequence& other) const
    {
        return chords == other.chords;
    }

    bool operator!=(const ChordSequence& other) const { return !(*this == other); }
};
} // namespace ecode
