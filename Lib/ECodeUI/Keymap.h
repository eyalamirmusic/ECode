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
        ChordSequence chords;
        std::string commandId;

        bool operator==(const Binding& other) const
        {
            return chords == other.chords && commandId == other.commandId;
        }
    };

    // An unparseable chord is dropped. Binding the same chord twice keeps both
    // and the later one wins, which is what lets user bindings be appended
    // after the defaults instead of merged into them.
    //
    // An empty command id is how a binding is taken *away*, and it is the same
    // mechanism rather than a second one: the entry shadows the default, so the
    // chord resolves to nothing and shortcutFor stops offering it. Which is
    // exactly what an unbound chord already looks like from both sides.
    void bind(std::string_view chords, std::string commandId);

    const eacp::Vector<Binding>& bindings() const { return list; }

    // Whole-table equality, which the application uses to tell whether a
    // reloaded settings file actually moved a binding. It matters because the
    // menu bar is built from this: on macOS an item's key equivalent is matched
    // before the window sees the key, so a stale bar keeps claiming a chord the
    // file has since given to something else — and reinstalling one is not free
    // of consequence, since it replaces the menus AppKit may be tracking.
    bool operator==(const Keymap& other) const { return list == other.list; }
    bool operator!=(const Keymap& other) const { return !(*this == other); }

    // Empty when nothing is bound to exactly this, which includes a sequence
    // that merely *begins* one: ⌘K resolves to nothing here however many
    // bindings start with it. ChordMatcher is what turns keys into commands;
    // this is the table it asks.
    std::string_view commandFor(const ChordSequence& chords) const;
    std::string_view commandFor(const Chord& chord) const;
    std::string_view commandFor(const eacp::Graphics::KeyEvent& event) const;

    // Whether some binding carries on from here — whether, having pressed this
    // much, there is anything left to wait for.
    //
    // Live bindings only. A sequence the settings file took away with an empty
    // command id leaves its entry in the table, and a prefix test reading the
    // *shape* of the table would go on waiting for a second key that can no
    // longer mean anything — with the first key swallowed every time.
    bool isPrefixOfABinding(const ChordSequence& prefix) const;

    // The chords that currently run this command, for display. Invalid when
    // there are none — including when the only binding for it has since been
    // shadowed by a later one, because a palette that advertises a shortcut
    // that no longer works is worse than one that advertises nothing.
    ChordSequence shortcutFor(std::string_view commandId) const;

private:
    eacp::Vector<Binding> list;
};

// Where the keyboard stands inside a chord sequence: ⌘K has been pressed, and
// what runs depends on the key after it.
//
// Its own class rather than state on Keymap, because a keymap is a table —
// compared whole, and replaced whole every time the settings file is re-read —
// while this is one window's keyboard at one instant. The table is passed in on
// each press rather than held, so a reload cannot leave a reference behind.
//
// It also has to say what it is waiting for out loud, which is why message()
// is here and not composed by the window: a prefix that swallows the next
// keystroke with nothing on screen explaining why is the failure this editor
// can least afford, and a rule belongs where a test can read it.
//
// There is no timeout, and that is a decision rather than an omission. Any key
// that does not continue the sequence ends it, so a pending prefix costs at
// most the one keystroke that ends it and never leaves the editor untypeable —
// and a wall-clock rule would put the behaviour of the keyboard behind a number
// no test could read without waiting for it.
class ChordMatcher
{
public:
    enum class Result
    {
        // Nothing is bound here and nothing was pending, so the key belongs to
        // whatever sits below the keymap — the document, usually.
        noMatch,

        // The start of a longer binding. Consumed, and the next key decides.
        pending,

        // A whole binding, named by Match::command.
        matched,

        // A prefix was pending and this key does not continue it. Consumed —
        // it was somebody reaching for a chord, not text — and named in
        // message() rather than passed on to be typed.
        cancelled,
    };

    struct Match
    {
        Result result = Result::noMatch;

        // Into the keymap's own storage, so it is good only until that table is
        // replaced. Dispatched immediately by every caller.
        std::string_view command;
    };

    Match press(const Keymap& keymap, const Chord& chord);

    // Forgets a pending prefix and says nothing about it. What a re-read of the
    // settings file does: the table it was a prefix *of* has been replaced.
    void cancel();

    bool isPending() const { return pending.isValid(); }

    // What the status bar says, or empty when there is nothing to say.
    std::string message() const;

private:
    ChordSequence pending;

    // The sequence that ended in nothing, kept only so it can be named.
    ChordSequence unmatched;
};

// ECode's own bindings, which is what a settings file is layered onto.
//
// Here rather than in the application for the reason defaultMenus() is: it is a
// table, and a table belongs where a test can read it. It is also half of the
// merge policy — the file's bindings are appended to this rather than replacing
// it, so a file that names three chords keeps the other thirty.
Keymap defaultKeymap();
} // namespace ecode
