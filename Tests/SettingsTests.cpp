#include <ECodeUI/Settings.h>
#include <ECodeUI/Themes.h>

#include <NanoTest/NanoTest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

// The settings file: the colour codec under it, what a partial file is allowed
// to leave out, and what a broken one costs.
//
// The load path is deliberately incapable of failing — a file someone has just
// broken with an edit has to leave the editor working, or there is nothing left
// to edit it back with. Which makes "did that actually take effect?" the thing
// worth testing, since nothing will ever say it did not.

using namespace nano;
using namespace ecode;
using eacp::Graphics::Color;

namespace
{
std::filesystem::path scratch(const std::string& name)
{
    auto dir = std::filesystem::temp_directory_path() / ("ecode-settings-" + name);

    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    return dir;
}

void writeTo(const std::filesystem::path& path, std::string_view contents)
{
    auto out = std::ofstream {path, std::ios::binary | std::ios::trunc};
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

// A write from outside the app, with the timestamp pushed forward explicitly:
// two writes inside one filesystem tick share a modification time, and the
// watcher would then only see the change if the length moved too.
void writeExternally(const std::filesystem::path& path, std::string_view contents)
{
    writeTo(path, contents);

    std::filesystem::last_write_time(
        path, std::filesystem::last_write_time(path) + std::chrono::seconds {2});
}

bool same(const Color& a, const Color& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// Whole-palette comparison. Byte-wise rather than field-wise because there is
// no operator== on a Color and, more to the point, because a comparison that
// went through reflection could not see a field reflection had forgotten. Sound
// only while a theme is nothing but Colors, which is what the size check in
// checkEveryColorSurvivesJson is there to keep true.
template <typename Theme>
bool sameBytes(const Theme& a, const Theme& b)
{
    return std::memcmp(&a, &b, sizeof(Theme)) == 0;
}

// The palettes the dark theme is made of, named so they can be addressed.
const auto darkChrome = ChromeTheme {};
const auto darkText = TextTheme {};
} // namespace

// --- the colour codec ----------------------------------------------------

auto tHexRoundTrip = test("Settings/hexIsTheByteMappingBothWays") = []
{
    check(toHexColor(Color {0.f, 0.f, 0.f}) == "#000000");
    check(toHexColor(Color {1.f, 1.f, 1.f}) == "#ffffff");

    // 0x1e / 255, which is what a theme file's "#1e1e2e" has to come back as.
    const auto parsed = fromHexColor("#1e1e2e", Color {});

    check(same(parsed, Color {30.f / 255.f, 30.f / 255.f, 46.f / 255.f}));
    check(toHexColor(parsed) == "#1e1e2e");
};

// The alpha digits are written only when there is something to say, so an
// opaque palette does not read as sixty-eight colours each with a detail worth
// noticing.
auto tHexAlpha = test("Settings/hexCarriesAlphaOnlyWhenItIsNotOpaque") = []
{
    check(toHexColor(Color {1.f, 1.f, 1.f, 1.f}) == "#ffffff");
    check(toHexColor(Color {1.f, 1.f, 1.f, 0.f}) == "#ffffff00");

    const auto translucent = fromHexColor("#ffffff33", Color {});

    check(same(translucent, Color {1.f, 1.f, 1.f, 51.f / 255.f}));
    check(toHexColor(translucent) == "#ffffff33");

    // Six digits mean opaque, not "keep whatever alpha was there". A theme
    // entry written "#ff0000" over a translucent default means the red it
    // looks like, and an inherited 5% alpha would draw almost nothing.
    check(fromHexColor("#ff0000", Color {0.f, 0.f, 0.f, 0.05f}).a == 1.f);
};

auto tHexIsForgiving = test("Settings/hexTakesEitherCaseAndAnOptionalHash") = []
{
    const auto expected = Color {0.f, 1.f, 0.f};

    check(same(fromHexColor("#00ff00", Color {}), expected));
    check(same(fromHexColor("00ff00", Color {}), expected));
    check(same(fromHexColor("#00FF00", Color {}), expected));
};

// The reason a fallback is a parameter rather than black. A typo in one entry
// costs that entry, and the caller passes the colour the palette already had —
// so the built-in one stays on screen instead of a widget going black.
//
// "#gggggg" is the case that separates a real parse from a length check: six
// characters, and not a colour.
auto tHexFallsBack = test("Settings/anUnparseableColourKeepsTheOneItHad") = []
{
    const auto had = Color {0.25f, 0.5f, 0.75f, 0.5f};

    check(same(fromHexColor("", had), had));
    check(same(fromHexColor("#", had), had));
    check(same(fromHexColor("#gggggg", had), had));
    check(same(fromHexColor("#12345", had), had));
    check(same(fromHexColor("#1234567", had), had));
    check(same(fromHexColor("rebeccapurple", had), had));
};

// --- every field is actually reflected -----------------------------------

// A field left out of MIRO_REFLECT is a field the settings file cannot set, and
// nothing about that is visible: the key is simply ignored, like every unknown
// key. So this walks the struct as raw channels, gives every one of them its own
// value, and demands all of them survive a round trip.
//
// The oracle is memcmp rather than a JSON comparison, and that is the whole
// point. Comparing the two structs' JSON would compare what reflection emits
// against what reflection emits — a missing field is missing from both sides,
// and the test would pass precisely when it should fail.
//
// Reading the struct as an array of floats is sound only while it is nothing
// but Colors, which the size check on the next line is there to keep true.
template <typename Theme>
void checkEveryColorSurvivesJson(int colorCount)
{
    check(sizeof(Theme) == static_cast<std::size_t>(colorCount) * sizeof(Color));

    const auto channels = static_cast<std::size_t>(colorCount) * 4;

    auto perturbed = Theme {};
    auto* values = reinterpret_cast<float*>(&perturbed);

    // A distinct value per channel, so a pair of fields swapped in the macro's
    // list fails as loudly as a pair left out. Quantised to 255ths because that
    // is what hex can carry — an arbitrary float would fail on the rounding and
    // say nothing about whether the field was reflected.
    for (auto index = std::size_t {}; index < channels; ++index)
        values[index] = static_cast<float>((index * 7 + 3) % 256) / 255.f;

    auto loaded = Theme {};
    Miro::fromJSONString(loaded, Miro::toJSONString(perturbed));

    check(std::memcmp(&loaded, &perturbed, sizeof(Theme)) == 0);
}

auto tChromeFieldsReflect = test("Settings/everyChromeColourSurvivesTheFile") = []
{ checkEveryColorSurvivesJson<ChromeTheme>(48); };

auto tTextFieldsReflect = test("Settings/everyTextColourSurvivesTheFile") = []
{ checkEveryColorSurvivesJson<TextTheme>(20); };

// A colour has to be a *string* in the file. If it ever became an object of four
// numbers every theme anyone had written would silently stop applying, and the
// only symptom would be the defaults coming back.
auto tColorsAreStrings = test("Settings/aColourIsWrittenAsAHexString") = []
{
    const auto json = Miro::toJSONString(darkText);

    check(json.find(toHexColor(darkText.background)) != std::string::npos);

    // And an object of four numbers is what it must not be. A Color has no
    // other member that could produce an "r" key, so its absence is the whole
    // assertion.
    check(json.find("\"r\"") == std::string::npos);
};

// --- the theme table ------------------------------------------------------

// The dark theme is the structs' own defaults, which is what lets every caller
// that never heard of a theme keep the picture it had.
auto tDarkIsTheDefault = test("Settings/darkIsWhatADefaultStructAlreadyWas") = []
{
    const auto dark = themeByName("dark");

    check(sameBytes(dark.chrome, darkChrome));
    check(sameBytes(dark.text, darkText));
};

auto tThemeNamesResolve = test("Settings/everyNameInTheTableResolves") = []
{
    check(themeNames().size() >= 2);

    for (const auto& name: themeNames())
        check(isThemeName(name));

    check(!isThemeName("solarized"));
    check(isThemeName(defaultThemeName));
};

// Both halves, and the reason they are one table: picking "light" and getting a
// light sidebar around a dark file is not a theme, it is a bug.
auto tLightIsADifferentTheme = test("Settings/lightChangesBothHalves") = []
{
    const auto light = themeByName("light");

    check(!sameBytes(light.chrome, darkChrome));
    check(!sameBytes(light.text, darkText));

    // The one property a light theme has to have, checked rather than assumed:
    // the page is brighter than the ink on it. Dark is the other way round, so
    // this is a claim the two palettes genuinely disagree on.
    check(light.text.background.r > light.text.text.r);
    check(darkText.background.r < darkText.text.r);
};

// The name comes from a hand-edited file, so a typo has to cost the theme and
// not the launch.
auto tUnknownThemeFallsBack = test("Settings/anUnknownThemeNameFallsBackToDark") = []
{
    const auto config = configurationFromJson(R"({"theme": "sunburst"})");

    check(sameBytes(config.theme.chrome, darkChrome));
};

// --- what a file may leave out -------------------------------------------

auto tEmptyFileIsDefaults =
    test("Settings/anEmptyFileLeavesEveryDefaultStanding") = []
{
    const auto config = configurationFromJson("");

    check(config.settings.theme == defaultThemeName);
    check(config.settings.font.pointSize == FontSettings {}.pointSize);
    check(sameBytes(config.theme.text, darkText));
};

// A file broken by an edit has to leave the editor working, because the editor
// is what the file will be edited back with.
auto tBrokenJsonIsDefaults = test("Settings/malformedJsonLoadsAsNothingAtAll") = []
{
    const auto config = configurationFromJson("{ this is not json");

    check(config.settings.theme == defaultThemeName);
    check(sameBytes(config.theme.chrome, darkChrome));
};

auto tFontComesFromTheFile = test("Settings/theFileSetsTheFamilyAndTheSize") = []
{
    const auto config = configurationFromJson(
        R"({"font": {"family": "Courier New", "pointSize": 17}})");

    check(config.settings.font.family == "Courier New");
    check(config.settings.font.pointSize == 17.f);

    // And so Reset means the size the file asked for, which is the reason the
    // zoom is a separate number at all.
    check(config.settings.font.size() == 17.f);
};

// Both of the things a hand-edited font block can get wrong, corrected where
// they are read rather than at whichever call site remembered to.
auto tFontIsChecked = test("Settings/anImpossibleFontIsBroughtBackIntoRange") = []
{
    const auto huge = configurationFromJson(R"({"font": {"pointSize": 500}})");
    check(huge.settings.font.pointSize == FontSettings::maximumSize);

    const auto tiny = configurationFromJson(R"({"font": {"pointSize": -3}})");
    check(tiny.settings.font.pointSize == FontSettings::minimumSize);

    const auto nameless = configurationFromJson(R"({"font": {"family": ""}})");
    check(!nameless.settings.font.family.empty());
};

// The zoom is deliberately not part of the file: written back to disk it would
// turn ⌘0 into "back to whatever size I last happened to be at".
auto tZoomIsNotConfigurable = test("Settings/theFileCannotSetTheZoom") = []
{
    const auto config =
        configurationFromJson(R"({"font": {"pointSize": 13, "zoom": 9}})");

    check(config.settings.font.zoom == 0.f);
    check(config.settings.font.size() == 13.f);

    check(settingsTemplate().find("zoom") == std::string::npos);
};

// --- layering -------------------------------------------------------------

// The whole point of the colour blocks being partial. Name a theme, then say the
// one thing about it you disagree with.
auto tOverridesLayerOnTheNamedTheme =
    test("Settings/aColourBlockLayersOverTheNamedTheme") = []
{
    const auto config = configurationFromJson(R"({
        "theme": "light",
        "chromeColors": { "statusBar": "#7c3aed" },
        "textColors": { "keyword": "#ff5555" }
    })");

    const auto light = themeByName("light");

    // The entries named.
    check(same(config.theme.chrome.statusBar, fromHexColor("#7c3aed", Color {})));
    check(same(config.theme.text.keyword, fromHexColor("#ff5555", Color {})));

    // And everything else still the theme that was named, rather than the
    // defaults — which is the failure a block that *replaced* the palette would
    // produce, and it would only show up on the entries nobody had overridden.
    //
    // Said twice, because neither half can catch what the other does. Against
    // the *defaults* is the comparison that notices the name being ignored: a
    // themeByName that returned dark whatever it was asked for would hand the
    // same answer to this test's own `light`, and every check against it would
    // agree. Against `light` is the one that notices the layering mangling an
    // entry into some third colour, which matching-but-wrong would pass.
    check(!same(config.theme.chrome.sidebar, darkChrome.sidebar));
    check(!same(config.theme.text.background, darkText.background));

    check(same(config.theme.chrome.sidebar, light.chrome.sidebar));
    check(same(config.theme.text.background, light.text.background));
    check(same(config.theme.text.string, light.text.string));
};

// The same layering with no theme named, which is the case that separates
// "override the named theme" from "override whatever was loaded last".
auto tOverridesWorkWithoutAThemeName =
    test("Settings/aColourBlockAloneLayersOverTheDefault") = []
{
    const auto config =
        configurationFromJson(R"({"textColors": {"caret": "#00ff00"}})");

    check(same(config.theme.text.caret, Color {0.f, 1.f, 0.f}));
    check(same(config.theme.text.background, darkText.background));
};

// A key nobody has heard of is ignored, so a file can be grown and shared
// between versions.
auto tUnknownKeysAreIgnored = test("Settings/unknownKeysAreIgnored") = []
{
    const auto config = configurationFromJson(R"({
        "theme": "light",
        "minimapEnabled": true,
        "textColors": { "keyword": "#ff5555", "thereIsNoSuchColour": "#000000" }
    })");

    check(same(config.theme.text.keyword, fromHexColor("#ff5555", Color {})));
    check(same(config.theme.text.background, themeByName("light").text.background));
};

// A bad colour costs its own entry. The check that matters is the *rest* of the
// block: an implementation that gave up on the first bad value would leave every
// colour after it unset, and the palette would be right where it was tested.
auto tOneBadColourCostsOneEntry =
    test("Settings/abrokenColourLeavesTheRestOfTheBlockAlone") = []
{
    const auto config = configurationFromJson(R"({
        "textColors": {
            "caret": "not a colour",
            "keyword": "#ff5555",
            "selection": "#010203"
        }
    })");

    check(same(config.theme.text.caret, darkText.caret));
    check(same(config.theme.text.keyword, fromHexColor("#ff5555", Color {})));
    check(same(config.theme.text.selection, fromHexColor("#010203", Color {})));
};

// --- the template ---------------------------------------------------------

// Written once, when there is nothing there. Reading it back has to give
// exactly what the app was already running with, or the first thing the command
// does is change the editor it was meant to expose.
auto tTemplateIsTheDefaults = test("Settings/theTemplateReadsBackAsTheDefaults") = []
{
    const auto config = configurationFromJson(settingsTemplate());

    check(config.settings.theme == defaultThemeName);
    check(config.settings.font.pointSize == FontSettings {}.pointSize);
    check(config.settings.font.family == FontSettings {}.family);

    check(sameBytes(config.theme.chrome, darkChrome));
    check(sameBytes(config.theme.text, darkText));
};

// Present and empty. A template that spelled all sixty-eight colours out would
// pin the file to one palette and leave its own "theme" key doing nothing — and
// the person who changed it would have no way to see why.
auto tTemplateHasEmptyColourBlocks =
    test("Settings/theTemplateOffersTheBlocksWithoutFillingThem") = []
{
    const auto text = settingsTemplate();

    check(text.find("chromeColors") != std::string::npos);
    check(text.find("textColors") != std::string::npos);

    // No colour in it anywhere. Cheap and exact: every colour this could write
    // starts with a '#', and nothing else in the template does.
    check(text.find('#') == std::string::npos);
};

auto tTemplateIsWrittenOnce = test("Settings/theTemplateNeverOverwritesAFile") = []
{
    const auto dir = scratch("template");
    const auto path = eacp::FilePath {dir / "ecode.json"};

    check(writeSettingsTemplateIfAbsent(path));
    check(loadConfiguration(path).settings.theme == defaultThemeName);

    writeTo(dir / "ecode.json", R"({"theme": "light"})");

    // The command behind this is one somebody can press twice.
    check(writeSettingsTemplateIfAbsent(path));
    check(loadConfiguration(path).settings.theme == "light");
};

auto tMissingFileIsDefaults = test("Settings/aPathWithNothingAtItIsTheDefaults") = []
{
    const auto dir = scratch("missing");
    const auto config = loadConfiguration(eacp::FilePath {dir / "nothing.json"});

    check(config.settings.theme == defaultThemeName);
};

// --- the watcher ----------------------------------------------------------

// Standing in for file watching, which eacp does not have. It is what makes
// editing the settings inside ECode work without a reload command.
auto tWatcherSeesAWrite = test("Settings/theWatcherReportsAChangeOnce") = []
{
    const auto dir = scratch("watch");
    const auto file = dir / "ecode.json";

    writeTo(file, R"({"theme": "dark"})");

    auto watcher = SettingsWatcher {eacp::FilePath {file}};

    // Construction takes the stamp, so a file nobody has touched since launch
    // does not read as a change and re-apply the theme every second.
    check(!watcher.poll());

    writeExternally(file, R"({"theme": "light"})");

    check(watcher.poll());
    check(!watcher.poll());
};

// The other half of the stamp, and the only case that can tell it is there. A
// filesystem's timestamp granularity can be a whole second, so an edit-save-
// edit-save cycle produces two writes that share a modification time — and then
// the length is the only thing left that moved.
//
// Forced rather than raced: the stamp is put back to what it was, which is
// exactly what the filesystem would have done on its own.
auto tWatcherSeesASameTickWrite =
    test("Settings/theWatcherReportsAWriteThatSharesATimestamp") = []
{
    const auto dir = scratch("watch-tick");
    const auto file = dir / "ecode.json";

    writeTo(file, R"({"theme": "dark"})");

    const auto stamp = std::filesystem::last_write_time(file);

    auto watcher = SettingsWatcher {eacp::FilePath {file}};

    check(!watcher.poll());

    writeTo(file, R"({"theme": "light", "font": {"pointSize": 20}})");
    std::filesystem::last_write_time(file, stamp);

    check(watcher.poll());
};

// Both directions. A file appearing is what happens the moment the template is
// written, and one going away means the defaults come back rather than the last
// settings read sticking around with nothing on disk behind them.
auto tWatcherSeesAppearAndVanish =
    test("Settings/theWatcherReportsAFileArrivingAndLeaving") = []
{
    const auto dir = scratch("watch-life");
    const auto file = dir / "ecode.json";

    auto watcher = SettingsWatcher {eacp::FilePath {file}};

    check(!watcher.poll());

    writeTo(file, R"({"theme": "light"})");
    check(watcher.poll());

    std::filesystem::remove(file);
    check(watcher.poll());
    check(!watcher.poll());
};
