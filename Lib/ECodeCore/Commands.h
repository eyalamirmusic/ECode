#pragma once

#include <eacp/Core/Core.h>

#include <functional>
#include <string>
#include <string_view>

namespace ecode
{
// Something the editor can be asked to do, named so that a keystroke, a palette
// entry and a menu item can all ask for the same thing without knowing each
// other.
//
// PLAN.md §5 has this arriving *with* the palette rather than after it, and the
// counter-example is CowTerm: three unrelated keybinding mechanisms, none of
// which can name a command, so a palette would have needed a fourth. Here the
// palette enumerates the registry and the keymap points into it by id, and
// neither holds a list that has to be kept in step by hand.
struct Command
{
    // Namespaced, VSCode-style: "editor.undo", "workbench.showPalette". The
    // stable name — a title can be reworded, a binding rebound.
    std::string id;

    // What the palette shows, and the only string a person ever reads.
    std::string title;

    std::function<void()> run = [] {};

    // Commands that make no sense right now are still listed, but greyed and
    // not runnable — undo with an empty history, save with nothing to save.
    // Defaulted so a command that is always available says nothing.
    std::function<bool()> isEnabled = [] { return true; };
};

// Every command the application knows about, in registration order.
//
// Order is the palette's order for an empty query, so it is worth registering
// them in the order they should be offered rather than sorting later.
class CommandRegistry
{
public:
    void add(Command command);

    const eacp::Vector<Command>& commands() const { return list; }

    // Null when nothing has that id — a keymap can name a command that was
    // never registered, and a typo there should not be a crash.
    //
    // The pointer is into the registry's own storage, so it does not survive a
    // later add(). Registration happens once at startup and lookups are
    // transient, which is what makes that safe; anything holding on across a
    // frame should keep the id.
    const Command* find(std::string_view id) const;

    // False when the command is unknown or disabled, so a caller can fall back
    // rather than having to check first.
    bool run(std::string_view id) const;

private:
    eacp::Vector<Command> list;
};
} // namespace ecode
