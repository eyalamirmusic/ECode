#include "Settings.h"

#include <eacp/Core/Utils/File.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>

#include <Miro/Json.h>

#include <cstdint>
#include <filesystem>
#include <span>

namespace ecode
{
using namespace eacp;

namespace
{
// The sub-object at `key`, or a null value when there is no such key — which
// loads as "no fields" and so leaves the palette exactly as it was.
//
// Not `json[key]`: Miro's const subscript is `map::at` on a `std::get<Object>`,
// so it throws twice over for the two cases this has to treat as ordinary — a
// file with no colour block in it, and a file that is not an object at all.
Miro::JSON blockOrNull(const Miro::JSON& json, const char* key)
{
    if (json.isObject())
        if (const auto* found = Miro::Json::find(json.asObject(), key))
            return *found;

    return {};
}

constexpr auto chromeColorsKey = "chromeColors";
constexpr auto textColorsKey = "textColors";

// The named palette first, then the file's own colours onto it. Both loads are
// the same call: Miro leaves a key the JSON does not mention at the value the
// struct already held, so "layer a partial theme over a full one" needs no code
// beyond doing them in this order.
Theme layeredTheme(const Miro::JSON& json, std::string_view name)
{
    auto theme = themeByName(name);

    Miro::fromJSON(theme.chrome, blockOrNull(json, chromeColorsKey));
    Miro::fromJSON(theme.text, blockOrNull(json, textColorsKey));

    return theme;
}

// A file that does not exist and a file with nothing in it are the same thing
// to every reader here, so they have to be the same thing to the writer too.
bool isBlank(std::string_view text)
{
    return text.find_first_not_of(" \t\r\n") == std::string_view::npos;
}

// Writing is allowed to fail — a read-only home, a full disk — and everything
// here answers with a bool rather than throwing, because a settings file that
// could not be written is not a reason to lose the window.
bool writeText(const FilePath& path, std::string_view text)
{
    try
    {
        Files::writeFileAtomically(
            path, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    }
    catch (const std::exception&)
    {
        return false;
    }

    return true;
}
} // namespace

FilePath settingsPath()
{
    // Empty when the platform cannot answer, and a bare "ECode/settings.json"
    // would then be resolved against whatever the working directory happens to
    // be. The home directory is the fallback every platform can still give.
    const auto root = FilePath::appDataDirectory();

    return (root.empty() ? FilePath::homeDirectory() : root) / "ECode"
           / "settings.json";
}

FilePath legacySettingsPath()
{
    return FilePath::homeDirectory() / ".config" / "ecode.json";
}

bool migrateSettings(const FilePath& from, const FilePath& to)
{
    if (File {to}.exists() || !File {from}.exists())
        return false;

    if (!writeText(to, Files::readFile(from)))
        return false;

    // Removed rather than left behind, because two settings files — one being
    // read and the other being edited — is the state this exists to avoid. It
    // is also why the destination existing is enough to stop: whatever is
    // there now is the answer, and there is nothing to migrate onto it.
    auto failure = std::error_code {};
    std::filesystem::remove(toStdPath(from), failure);

    return true;
}

Configuration configurationFromJson(std::string_view text)
{
    const auto json = Miro::Json::getParsedValue(text);

    auto config = Configuration {};

    Miro::fromJSON(config.settings, json);

    config.theme = layeredTheme(json, config.settings.theme);

    // And the same order for the bindings, for the same reason: appending is
    // what makes the file's block partial, since Keymap resolves a chord to its
    // last binding. An unparseable chord is dropped by bind() and costs that
    // one line — the defaults it would have shadowed stay in force, which is
    // the side of that trade a person can still work in.
    config.keymap = defaultKeymap();

    for (const auto& [chord, commandId]: config.settings.keybindings)
        config.keymap.bind(chord, commandId);

    return config;
}

Theme themeFromJson(std::string_view text, std::string_view name)
{
    return layeredTheme(Miro::Json::getParsedValue(text), name);
}

std::string readSettings(const FilePath& path)
{
    return Files::readFile(path);
}

std::string settingsTemplate()
{
    auto json = Miro::toJSON(Settings {});

    // Present and empty, which is the whole point of writing them: the blocks
    // say where an override goes without pre-writing sixty-eight of them, and a
    // file that spelled every colour out would leave its own "theme" key with
    // nothing left to decide.
    //
    // Through toObject rather than JSON's own subscript, which is map::at on
    // both overloads and so throws for a key that is not already there — it
    // reads a value, it never makes one.
    auto& fields = json.toObject();

    fields[chromeColorsKey] = Miro::Json::Object {};
    fields[textColorsKey] = Miro::Json::Object {};

    // Nothing puts "keybindings" here, and that is the difference between the
    // two: it is a field of Settings, so reflection writes it — empty, because
    // the struct's own default is empty. The colour blocks are not fields of
    // anything and have to be added by hand.
    //
    // Empty is the right template entry for it either way, and for a sharper
    // reason than the colours have. A template that spelled out the forty-odd
    // default bindings would be a copy of a table that moves, so every chord it
    // named would be pinned to whatever the version that created the file had.

    return Miro::Json::print(json, 4);
}

std::optional<std::string>
    settingsWith(std::string_view text, std::string_view key, std::string value)
{
    auto json = Miro::Json::getParsedValue(isBlank(text) ? settingsTemplate()
                                                         : std::string {text});

    if (!json.isObject())
        return {};

    // Through toObject rather than JSON's own subscript, which is map::at on
    // both overloads and so throws for a key that is not already there — it
    // reads a value, it never makes one. Which is exactly the case here: the
    // first theme anyone picks may be the first time the key is written.
    json.toObject()[std::string {key}] = std::move(value);

    return Miro::Json::print(json, 4);
}

bool writeSetting(const FilePath& path, std::string_view key, std::string value)
{
    const auto updated = settingsWith(readSettings(path), key, std::move(value));

    return updated && writeText(path, *updated);
}

bool writeSettingsTemplateIfAbsent(const FilePath& path)
{
    if (File {path}.exists())
        return true;

    return writeText(path, settingsTemplate());
}

SettingsWatcher::SettingsWatcher(FilePath pathToWatch)
    : filePath(std::move(pathToWatch))
{
    poll();
}

bool SettingsWatcher::poll()
{
    const auto file = File {filePath};

    const auto nowExists = file.exists();
    const auto nowModified = nowExists ? file.modificationTime() : 0;
    const auto nowSize = nowExists ? file.size() : 0;

    if (nowExists == exists && nowModified == modified && nowSize == size)
        return false;

    exists = nowExists;
    modified = nowModified;
    size = nowSize;

    return true;
}
} // namespace ecode
