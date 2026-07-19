#include "Commands.h"

namespace ecode
{
void CommandRegistry::add(Command command)
{
    // Re-registering an id replaces it rather than shadowing it, so a command
    // cannot end up in the palette twice with only one of them reachable.
    for (auto& existing: list)
    {
        if (existing.id == command.id)
        {
            existing = std::move(command);
            return;
        }
    }

    list.push_back(std::move(command));
}

const Command* CommandRegistry::find(std::string_view id) const
{
    for (const auto& command: list)
        if (command.id == id)
            return &command;

    return nullptr;
}

bool CommandRegistry::run(std::string_view id) const
{
    const auto* command = find(id);

    if (command == nullptr || !command->isEnabled())
        return false;

    command->run();
    return true;
}
} // namespace ecode
