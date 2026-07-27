#include "Keymap.h"

#include <algorithm>

namespace ecode
{
using namespace eacp;

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
