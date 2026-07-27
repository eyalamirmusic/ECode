#include <ECodeWorkbench/Settings.h>
#include <ECodeWorkbench/Themes.h>

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

// The same layering against a base the file did not choose, which is what a
// theme picked from inside the app is.
//
// The failure this exists to catch is the obvious way to write the picker:
// themeByName and an assignment. That draws the right palette and quietly
// discards every colour the file overrode — so someone who had set one entry
// would watch it vanish on picking a theme, with the file still saying what they
// asked for and nothing anywhere reporting it.
auto tThemeFromJsonKeepsTheOverrides =
    test("Settings/pickingAThemeKeepsTheFilesOwnColours") = []
{
    constexpr auto text = R"({
        "theme": "dark",
        "chromeColors": { "statusBar": "#7c3aed" },
        "textColors": { "keyword": "#ff5555" }
    })";

    const auto picked = themeFromJson(text, "light");
    const auto light = themeByName("light");

    // The overrides survive the base changing under them.
    check(same(picked.chrome.statusBar, fromHexColor("#7c3aed", Color {})));
    check(same(picked.text.keyword, fromHexColor("#ff5555", Color {})));

    // And everything else is the palette that was asked for rather than the one
    // the file names — said against both, for the reason the layering test says
    // it twice: `light` catches a base that was mangled, and the file's own
    // "dark" catches the name being ignored.
    check(same(picked.text.background, light.text.background));
    check(!same(picked.text.background, darkText.background));
};

// And the name it is given is held to the same rule the file's is: an unknown
// one falls back rather than failing, since a picker is not the only caller and
// nothing here is in a position to report anything.
auto tThemeFromJsonFallsBack =
    test("Settings/anUnknownPickedThemeFallsBackWithTheOverridesIntact") = []
{
    const auto picked =
        themeFromJson(R"({"textColors": {"caret": "#00ff00"}})", "solarized");

    check(same(picked.text.caret, Color {0.f, 1.f, 0.f}));
    check(same(picked.text.background, darkText.background));
};

// --- which theme is in force ------------------------------------------------

// The rule the picker rests on, and the one thing about it that is not obvious:
// a poll re-reading the same file must not undo a choice, while an edit to the
// file must.
//
// Both directions matter and they fail differently. Forgetting the first makes
// the picker look broken — pick a theme, wait a second, watch it revert, with
// the settings file the only clue and nobody looking at it. Forgetting the
// second makes the *file* look broken, which is worse: editing "theme" would do
// nothing for the rest of the session, and the obvious next move is to save it
// again.
auto tThemeChoiceKeepsAPickAcrossAReRead =
    test("Settings/aPickedThemeSurvivesTheFileBeingReadAgain") = []
{
    auto choice = ThemeChoice {};

    choice.fileSaid("dark");
    check(choice.name() == "dark");

    choice.pick("light");
    check(choice.name() == "light");

    // The once-a-second poll, finding what it found last time.
    choice.fileSaid("dark");
    check(choice.name() == "light");
};

auto tThemeChoiceYieldsToAnEditedFile =
    test("Settings/anEditedThemeKeyTakesThePickBack") = []
{
    auto choice = ThemeChoice {};

    choice.fileSaid("dark");
    choice.pick("light");

    // Somebody has edited the file. It is the later of the two deliberate acts,
    // so it wins — and it wins even though what it now names is not what the
    // pick was made against.
    choice.fileSaid("solarized");
    check(choice.name() == "solarized");

    // And having yielded once, the choice is gone rather than lying in wait to
    // come back on the next poll.
    choice.fileSaid("solarized");
    check(choice.name() == "solarized");
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

// --- keybindings ----------------------------------------------------------

// The merge policy, and the whole reason the block is layered rather than
// loaded: a file naming one chord has said nothing about the other forty.
//
// Both halves are needed and neither is enough. A block that *replaced* the
// defaults would pass the first check and fail the second; a block that was
// read and thrown away would pass the second and fail the first.
auto tKeybindingsLayerOnTheDefaults =
    test("Settings/aKeybindingBlockLayersOverTheDefaults") = []
{
    const auto config =
        configurationFromJson(R"({"keybindings": {"cmd+k": "file.close"}})");

    check(config.keymap.commandFor(Chord::parse("cmd+k")) == "file.close");
    check(config.keymap.commandFor(Chord::parse("cmd+s")) == "file.save");
};

// Rebinding a chord the defaults already used, which is the case that says
// which way round the two are appended. And the second check is the one that
// costs something to get wrong: a chord taken from a command has to stop being
// advertised for it, because the palette prints that string as an instruction.
auto tKeybindingsRebindADefault =
    test("Settings/aFileCanTakeAChordFromTheCommandThatHadIt") = []
{
    const auto config =
        configurationFromJson(R"({"keybindings": {"cmd+s": "file.saveAs"}})");

    check(config.keymap.commandFor(Chord::parse("cmd+s")) == "file.saveAs");

    check(!config.keymap.shortcutFor("file.save").isValid());
    check(config.keymap.shortcutFor("file.saveAs").display() == "⌘S");
};

// Taking a chord away entirely, which is the one thing the colour blocks have
// no equivalent of — a colour can only be changed to another colour.
auto tKeybindingsCanUnbind = test("Settings/anEmptyCommandIdUnbindsAChord") = []
{
    const auto config = configurationFromJson(R"({"keybindings": {"cmd+d": ""}})");

    check(config.keymap.commandFor(Chord::parse("cmd+d")).empty());
    check(!config.keymap.shortcutFor("edit.addNextOccurrence").isValid());

    // The rest of the table is still there, which is what separates an unbind
    // from a block that failed to load at all.
    check(config.keymap.commandFor(Chord::parse("cmd+z")) == "edit.undo");
};

// A bad line costs its own line, the same way a bad colour costs its own entry.
//
// The second check is the one worth having, and it is why Chord::parse rejects
// a second key rather than taking the last one: read leniently, "cmmd+k" is a
// binding on the *bare* key K, and handleShortcut runs before the document —
// so a typo in the file would cost the ability to type a letter, silently, in
// every file open in the editor.
auto tOneBadChordCostsOneLine =
    test("Settings/anUnreadableChordLeavesTheRestOfTheBlockAlone") = []
{
    const auto config = configurationFromJson(R"({
        "keybindings": {
            "cmmd+k": "file.close",
            "cmd+j": "file.new"
        }
    })");

    check(config.keymap.commandFor(Chord::parse("cmd+j")) == "file.new");
    check(config.keymap.commandFor(Chord::parse("k")).empty());
};

// A sequence, which is the whole reason a binding is written as text rather
// than as a chord: the spelling with a space in it has to survive the file, the
// map it is read into and the merge onto the defaults.
auto tKeybindingsCanBeSequences = test("Settings/aFileCanBindAChordSequence") = []
{
    const auto config =
        configurationFromJson(R"({"keybindings": {"cmd+k cmd+w": "file.close"}})");

    check(config.keymap.commandFor(ChordSequence::parse("cmd+k cmd+w"))
          == "file.close");

    // And the prefix on its own is not the binding, which is what a keymap that
    // took the first chord and stopped would have made of it.
    check(config.keymap.commandFor(Chord::parse("cmd+k")).empty());
    check(config.keymap.shortcutFor("file.close").display() == "⌘K ⌘W");
};

// The defaults put three sequences under ⌘K, so a file binding it alone is the
// collision that is now easy to write — and it is silent from both sides: the
// chord waits for a second key, and the command it names never runs.
auto tKeybindingOnAPrefixIsReported =
    test("Settings/aChordThatBeginsALongerOneIsStillAPrefix") = []
{
    const auto config =
        configurationFromJson(R"({"keybindings": {"cmd+k": "file.close"}})");

    check(config.keymap.isPrefixOfABinding(ChordSequence::parse("cmd+k")));

    // Taking the sequences away with it is what makes the chord its own again,
    // and it is the only way a file has of saying so.
    const auto freed = configurationFromJson(R"({"keybindings": {
        "cmd+k": "file.close",
        "cmd+k cmd+t": "",
        "cmd+k cmd+left": "",
        "cmd+k cmd+right": ""
    }})");

    check(!freed.keymap.isPrefixOfABinding(ChordSequence::parse("cmd+k")));
};

// A command that was never registered still takes the chord, deliberately. The
// alternative is a rebinding that half-applies — ⌘S still saving because the
// command it was pointed at was misspelt — which is the one outcome that
// cannot be reasoned about from the file.
auto tKeybindingsTakeTheChordRegardless =
    test("Settings/anUnknownCommandStillTakesTheChord") = []
{
    const auto config = configurationFromJson(
        R"({"keybindings": {"cmd+s": "file.saveEverything"}})");

    check(config.keymap.commandFor(Chord::parse("cmd+s")) == "file.saveEverything");
    check(!config.keymap.shortcutFor("file.save").isValid());
};

// What the application gates the menu bar rebuild on. Installing a bar replaces
// the menus AppKit may be tracking, so "the settings file changed" is not the
// question — "did a chord move" is, and a colour block is the case that has to
// answer no.
auto tKeymapIsUnmovedByAColourChange =
    test("Settings/aColourOnlyFileLeavesTheKeymapIdentical") = []
{
    const auto plain = configurationFromJson("{}");
    const auto coloured = configurationFromJson(
        R"({"theme": "light", "textColors": {"caret": "#00ff00"}})");

    check(plain.keymap == coloured.keymap);
    check(plain.keymap == defaultKeymap());

    const auto rebound =
        configurationFromJson(R"({"keybindings": {"cmd+k": "file.close"}})");

    check(!(rebound.keymap == plain.keymap));
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

    check(config.keymap == defaultKeymap());
};

// Present and empty. A template that spelled all sixty-eight colours out would
// pin the file to one palette and leave its own "theme" key doing nothing — and
// the person who changed it would have no way to see why.
//
// The keybindings block is the same decision with a sharper edge: written out
// in full it would be a copy of a table that moves, so every chord the file
// named would be frozen at whatever the version that created it had bound.
auto tTemplateHasEmptyColourBlocks =
    test("Settings/theTemplateOffersTheBlocksWithoutFillingThem") = []
{
    const auto text = settingsTemplate();

    check(text.find("chromeColors") != std::string::npos);
    check(text.find("textColors") != std::string::npos);
    check(text.find("keybindings") != std::string::npos);

    // No colour in it anywhere. Cheap and exact: every colour this could write
    // starts with a '#', and nothing else in the template does.
    check(text.find('#') == std::string::npos);

    // And no binding either, by the same trick: every chord in the table has a
    // modifier, and "+" appears nowhere else in a settings file.
    check(text.find('+') == std::string::npos);
};

auto tTemplateIsWrittenOnce = test("Settings/theTemplateNeverOverwritesAFile") = []
{
    const auto dir = scratch("template");
    const auto path = eacp::FilePath {dir / "ecode.json"};

    check(writeSettingsTemplateIfAbsent(path));
    check(configurationFromJson(readSettings(path)).settings.theme
          == defaultThemeName);

    writeTo(dir / "ecode.json", R"({"theme": "light"})");

    // The command behind this is one somebody can press twice.
    check(writeSettingsTemplateIfAbsent(path));
    check(configurationFromJson(readSettings(path)).settings.theme == "light");
};

auto tMissingFileIsDefaults = test("Settings/aPathWithNothingAtItIsTheDefaults") = []
{
    const auto dir = scratch("missing");
    const auto config =
        configurationFromJson(readSettings(eacp::FilePath {dir / "nothing.json"}));

    check(config.settings.theme == defaultThemeName);
};

// --- writing one setting back -----------------------------------------------
//
// What the theme picker does when something is chosen rather than previewed. The
// whole risk here is the *rest* of the file, so that is what these are about.

// The obvious implementation — fill in a Settings, print it — passes any test
// that only reads the key back. What it destroys is everything reflection does
// not know about, which is both colour blocks, and they are not fields of
// anything so nothing would say so.
//
// Read back through configurationFromJson rather than by looking for substrings,
// because that is the only oracle that answers the question actually being asked:
// does the file still *mean* what it meant, with one thing changed.
auto tWriteKeepsTheRestOfTheFile =
    test("Settings/settingTheThemeKeepsEverythingElseInTheFile") = []
{
    constexpr auto before = R"({
        "theme": "dark",
        "font": { "family": "Menlo", "pointSize": 15 },
        "chromeColors": { "statusBar": "#7c3aed" },
        "textColors": { "keyword": "#ff5555" },
        "keybindings": { "cmd+e": "file.save" },
        "somethingALaterVersionWrote": 7
    })";

    const auto after = settingsWith(before, themeKey, "light");

    check(after.has_value());

    const auto config = configurationFromJson(*after);

    // The one thing that was asked for — and asked for through themeKey, so a
    // key that did not match the reflected field would come back "dark" here.
    check(config.settings.theme == "light");

    // And everything that was not. The colour blocks are the point: they are
    // layered onto the *new* theme, so this also says the write left them where
    // the reader can still find them.
    check(same(config.theme.chrome.statusBar, fromHexColor("#7c3aed", Color {})));
    check(same(config.theme.text.keyword, fromHexColor("#ff5555", Color {})));
    check(same(config.theme.text.background, themeByName("light").text.background));

    check(config.settings.font.family == "Menlo");
    check(config.settings.font.size() == 15.f);
    check(config.keymap.shortcutFor("file.save").display() == "⌘E");

    // Including the key this version has never heard of, which is the one no
    // struct could have carried across.
    check(after->find("somethingALaterVersionWrote") != std::string::npos);
};

// The expensive direction, and the reason a write can refuse. A file someone is
// halfway through editing does not parse, and replacing it with a tidy document
// holding one key is the single mistake they cannot undo.
auto tWriteRefusesABrokenFile =
    test("Settings/aFileThatDoesNotParseIsNotWrittenOver") = []
{
    check(!settingsWith(R"({"theme": "dark",)", themeKey, "light").has_value());
    check(!settingsWith("not json at all", themeKey, "light").has_value());

    // An array parses and is not a file this can edit either.
    check(!settingsWith("[1, 2, 3]", themeKey, "light").has_value());
};

// A file that is not there is not a broken file, and picking a theme before
// ever opening the settings has to work. What it leaves behind is a file
// somebody could have written, blocks and all, rather than a lone key.
auto tWriteStartsFromTheTemplate =
    test("Settings/settingAThemeWithNoFileYetStartsFromTheTemplate") = []
{
    const auto written = settingsWith("", themeKey, "light");

    check(written.has_value());
    check(configurationFromJson(*written).settings.theme == "light");

    check(written->find("chromeColors") != std::string::npos);
    check(written->find("textColors") != std::string::npos);
};

auto tWriteSettingRoundTrips =
    test("Settings/aWrittenThemeIsThereOnTheNextRead") = []
{
    const auto dir = scratch("write");
    const auto path = eacp::FilePath {dir / "settings.json"};

    // No file at all, which is what a fresh machine has.
    check(writeSetting(path, themeKey, "light"));
    check(configurationFromJson(readSettings(path)).settings.theme == "light");

    // And again, over the file the first one made.
    check(writeSetting(path, themeKey, "dark"));
    check(configurationFromJson(readSettings(path)).settings.theme
          == defaultThemeName);
};

// --- moving to where settings live now --------------------------------------

auto tMigrationMovesTheFile =
    test("Settings/settingsAreMovedFromTheOldLocation") = []
{
    const auto dir = scratch("migrate");
    const auto from = eacp::FilePath {dir / "ecode.json"};
    const auto to = eacp::FilePath {dir / "ECode" / "settings.json"};

    writeTo(dir / "ecode.json", R"({"theme": "light"})");

    check(migrateSettings(from, to));
    check(configurationFromJson(readSettings(to)).settings.theme == "light");

    // The old one is gone rather than left as a second file that is read by
    // nothing and edited by someone.
    check(!std::filesystem::exists(dir / "ecode.json"));

    // And it happens once: with nothing left to move, there is nothing to do.
    check(!migrateSettings(from, to));
};

// The direction that would cost something: a machine that has already been set
// up here must not have its settings replaced by whatever an old install left
// behind. Both files exist, and the newer location wins without being touched.
auto tMigrationLeavesAnExistingFile =
    test("Settings/anExistingFileIsNotMigratedOver") = []
{
    const auto dir = scratch("migrate-existing");
    const auto from = eacp::FilePath {dir / "ecode.json"};
    const auto to = eacp::FilePath {dir / "ECode" / "settings.json"};

    writeTo(dir / "ecode.json", R"({"theme": "light"})");

    std::filesystem::create_directories(dir / "ECode");
    writeTo(dir / "ECode" / "settings.json", R"({"theme": "dark"})");

    check(!migrateSettings(from, to));
    check(configurationFromJson(readSettings(to)).settings.theme
          == defaultThemeName);

    // And the old file is still there, untouched: nothing was moved, so nothing
    // was removed.
    check(std::filesystem::exists(dir / "ecode.json"));
};

auto tMigrationWithNothingToMove =
    test("Settings/migratingWithNoOldFileDoesNothing") = []
{
    const auto dir = scratch("migrate-none");
    const auto to = eacp::FilePath {dir / "ECode" / "settings.json"};

    check(!migrateSettings(eacp::FilePath {dir / "ecode.json"}, to));
    check(!std::filesystem::exists(dir / "ECode" / "settings.json"));
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
