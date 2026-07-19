#include "MenuBuilder.h"

namespace ecode
{
using namespace eacp;

std::optional<Graphics::KeyEquivalent> toKeyEquivalent(const Chord& chord)
{
    if (!chord.isValid() || chord.key.size() != 1)
        return std::nullopt;

    auto equivalent = Graphics::KeyEquivalent {};
    equivalent.key = chord.key;
    equivalent.modifiers = chord.modifiers;

    return equivalent;
}

namespace
{
// A palette title carries its category ("File: Save") because the palette is
// one flat list and the category is how someone finds it. A menu already says
// which one it is, so repeating it reads as a stutter — "File ▸ File: Save".
//
// Only the menu's own name is stripped, and only as a whole prefix. A title
// that happens to contain a colon for some other reason keeps it, which is the
// difference between this and splitting on the first ": " and hoping.
std::string menuTitleFor(const std::string& title, const std::string& menuTitle)
{
    const auto prefix = menuTitle + ": ";

    if (title.starts_with(prefix))
        return title.substr(prefix.size());

    return title;
}

Graphics::MenuItem itemFor(const std::string& id,
                           const std::string& menuTitle,
                           const CommandRegistry& commands,
                           const Keymap& keymap,
                           const CommandDispatch& dispatch)
{
    const auto* command = commands.find(id);

    // Only reached for an id the caller already checked, since buildMenuBar
    // skips the unknown ones. Titled by the id rather than left blank so an
    // escaped case is visible in the menu instead of silently absent.
    if (command == nullptr)
        return Graphics::MenuItem::withAction(id);

    return Graphics::MenuItem::withAction(
        menuTitleFor(command->title, menuTitle),

        // By value: the platform keeps this lambda for as long as the bar is
        // installed, which outlives any dispatch the caller passed as a
        // temporary.
        [dispatch, id] { dispatch(id); },
        toKeyEquivalent(keymap.chordFor(id)),

        // Asked each time the menu opens, so this reads the command's own
        // predicate live rather than sampling it when the bar was built.
        [&commands, id]
        {
            const auto* current = commands.find(id);
            return current != nullptr && current->isEnabled();
        });
}
} // namespace

Graphics::MenuBar buildMenuBar(const Vector<MenuSpec>& specs,
                               const CommandRegistry& commands,
                               const Keymap& keymap,
                               const CommandDispatch& dispatch)
{
    auto bar = Graphics::MenuBar {};

    for (const auto& spec: specs)
    {
        auto menu = Graphics::Menu {spec.title};

        for (const auto& id: spec.commandIds)
        {
            if (id.empty())
            {
                menu.addSeparator();
                continue;
            }

            // A command the registry never got is left out rather than shown
            // dead. It is a mistake either way; unknownCommandIds is how it
            // gets found.
            if (commands.find(id) == nullptr)
                continue;

            menu.add(itemFor(id, spec.title, commands, keymap, dispatch));
        }

        bar.add(std::move(menu));
    }

    return bar;
}

Vector<std::string> unknownCommandIds(const Vector<MenuSpec>& specs,
                                      const CommandRegistry& commands)
{
    auto missing = Vector<std::string> {};

    for (const auto& spec: specs)
        for (const auto& id: spec.commandIds)
            if (!id.empty() && commands.find(id) == nullptr)
                missing.add(id);

    return missing;
}

Vector<MenuSpec> defaultMenus()
{
    const auto separator = MenuSpec::separator();

    auto menus = Vector<MenuSpec> {};

    menus.add(
        {"File",
         {"file.open", "file.openFolder", separator, "file.save", "file.revert"}});

    menus.add({"Edit",
               {"edit.undo",
                "edit.redo",
                separator,
                commands::editCut,
                commands::editCopy,
                commands::editPaste,
                separator,
                commands::editSelectAll}});

    menus.add({"Find",
               {"find.show",
                "find.showReplace",
                separator,
                "find.next",
                "find.previous",
                separator,
                "find.replaceAll"}});

    menus.add({"View",
               {"view.focusEditor",
                "view.focusExplorer",
                separator,
                "view.refreshExplorer",
                separator,
                "workbench.showPalette"}});

    return menus;
}
} // namespace ecode
