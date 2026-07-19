#pragma once

#include "Keymap.h"

#include <ECodeCore/Commands.h>

#include <eacp/Core/Core.h>
#include <eacp/Graphics/Graphics.h>

#include <functional>
#include <optional>
#include <string>

namespace ecode
{
// One menu, named by command ids rather than by callables.
//
// The registry supplies each item's title and whether it is available, and the
// keymap supplies its shortcut, so a menu holds no strings anyone has to keep
// in step with the palette. Adding a command to the registry and its id to a
// spec is the whole job.
struct MenuSpec
{
    std::string title;

    // An empty id is a separator. Ids the registry does not know are skipped —
    // see unknownCommandIds for finding them before a person does.
    eacp::Vector<std::string> commandIds;

    static std::string separator() { return {}; }
};

// Where a menu item's command goes. Deliberately not the registry: a menu
// shortcut is matched by the OS against the menu bar before the window sees a
// key down at all, so an item that ran commands.run() directly would bypass the
// precedence keyDown applies — and ⌘V with a find field focused would paste
// into the document rather than the field. See Widget::runCommand.
using CommandDispatch = std::function<void(std::string_view)>;

// Nothing when the chord cannot be expressed as a native key equivalent.
//
// Only keys that are a single character convert: AppKit takes the equivalent as
// a string, and the named keys (escape, pageup, f5) would need their platform
// glyph constants. Returning nothing for those is deliberate rather than
// approximate — an item simply prints no shortcut, and because the menu bar
// then does not claim the chord, the keymap still handles it as it did before.
std::optional<eacp::Graphics::KeyEquivalent> toKeyEquivalent(const Chord& chord);

// The menu bar the specs describe, ready for setApplicationMenuBar.
//
// The lifetime worth knowing: each item's enabled predicate holds `commands` by
// reference, and the platform keeps it for as long as the bar is installed — so
// the registry must outlive the menus, which it does when both belong to the
// window that installs them at startup. `dispatch` is copied and `keymap` is
// only read here, so neither has to.
eacp::Graphics::MenuBar buildMenuBar(const eacp::Vector<MenuSpec>& specs,
                                     const CommandRegistry& commands,
                                     const Keymap& keymap,
                                     const CommandDispatch& dispatch);

// Ids named in the specs that the registry does not have — a typo, or a command
// that was renamed on one side only. Worth asking at startup, because the
// symptom is an item that quietly never appears rather than anything failing.
eacp::Vector<std::string> unknownCommandIds(const eacp::Vector<MenuSpec>& specs,
                                            const CommandRegistry& commands);

// ECode's menus: File, Edit, Find and View, in the order they are shown.
//
// Here rather than in the application because it is the list a test can check
// against the registry, and because the ids are the only thing binding the two
// together.
eacp::Vector<MenuSpec> defaultMenus();
} // namespace ecode
