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

    // Set only by a command that turns something on and off, and the reason a
    // menu item for it can show a checkmark. Without it, "Toggle Word Wrap"
    // runs and the only way to find out whether it took is to look at the text.
    //
    // The one std::function here that is deliberately null by default, against
    // the house rule that they carry a no-op. Null means "this is not a toggle",
    // which is a different thing from a toggle that is currently off, and there
    // is no value of the predicate that says it: false would put an empty
    // checkbox beside every ordinary command. eacp's MenuChecked draws the same
    // distinction the same way, so a check at the call site is unavoidable
    // whatever this does.
    std::function<bool()> isChecked = nullptr;
};

// The four editing commands a focused text box claims for itself.
//
// Named here rather than spelled out at each site because they are the one set
// matched across module boundaries — TextField claims them, the application
// registers them, the menu bar lists them — and a mismatch is silent in the
// expensive direction: the field simply stops claiming the command, and ⌘V in
// a find field pastes into the file being searched.
namespace commands
{
inline constexpr auto editCut = "edit.cut";
inline constexpr auto editCopy = "edit.copy";
inline constexpr auto editPaste = "edit.paste";
inline constexpr auto editSelectAll = "edit.selectAll";
} // namespace commands

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
