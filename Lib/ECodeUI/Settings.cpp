#include "Settings.h"

#include <eacp/Core/Utils/File.h>
#include <eacp/Core/Utils/Files.h>

#include <Miro/Json.h>

#include <cstdint>
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
} // namespace

FilePath settingsPath()
{
    return FilePath::homeDirectory() / ".config" / "ecode.json";
}

Configuration configurationFromJson(std::string_view text)
{
    const auto json = Miro::Json::getParsedValue(text);

    auto config = Configuration {};

    Miro::fromJSON(config.settings, json);

    // The named palette first, then the file's own colours onto it. Both loads
    // are the same call: Miro leaves a key the JSON does not mention at the
    // value the struct already held, so "layer a partial theme over a full one"
    // needs no code beyond doing them in this order.
    config.theme = themeByName(config.settings.theme);

    Miro::fromJSON(config.theme.chrome, blockOrNull(json, chromeColorsKey));
    Miro::fromJSON(config.theme.text, blockOrNull(json, textColorsKey));

    return config;
}

Configuration loadConfiguration(const FilePath& path)
{
    return configurationFromJson(Files::readFile(path));
}

Configuration loadConfiguration()
{
    return loadConfiguration(settingsPath());
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

    return Miro::Json::print(json, 4);
}

bool writeSettingsTemplateIfAbsent(const FilePath& path)
{
    if (File {path}.exists())
        return true;

    const auto text = settingsTemplate();

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
