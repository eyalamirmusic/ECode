#pragma once

#include "Keymap.h"
#include "Themes.h"

#include <ECodeRender/FontSettings.h>

#include <eacp/Core/Utils/FilePath.h>

#include <Miro/Reflect.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace ecode
{
// What the settings file says, and what it resolves to.
//
// The file is JSON read through Miro reflection: the struct *is* the schema,
// unknown keys are ignored and missing keys keep their defaults. That last
// property is not a convenience, it is the whole override mechanism — see
// Configuration.
//
//   {
//       "font": { "family": "Menlo", "pointSize": 13 },
//       "theme": "dark",
//       "chromeColors": { "statusBar": "#7c3aed" },
//       "textColors": { "keyword": "#ff5555" },
//       "keybindings": { "cmd+p": "workbench.showPalette", "cmd+d": "" }
//   }
//
// ECode writes this file only where someone asked it to — the starter template
// when there is nothing there, and a setting a picker was used to change. Never
// by re-serialising this struct: reflection writes the fields of Settings, and
// the two colour blocks are fields of nothing, so a round trip would delete them
// along with every key a later version of ECode learns. What a write does
// instead is edit the *parsed document* and print it back, which leaves every
// key it does not name where it was. See settingsWith.
//
// The zoom still does not persist, and that is now a decision about the zoom
// rather than about the file: it is a thing done to one window, and two windows
// disagreeing about it is the point. See FontSettings.
struct Settings
{
    FontSettings font;

    // A name from the built-in table. An unknown one falls back rather than
    // failing; see themeByName.
    std::string theme {defaultThemeName};

    // Chords to command ids, and partial in the same way the colour blocks are:
    // what is here is layered onto the default keymap rather than replacing it,
    // so a file naming three chords keeps the other thirty. See Configuration.
    //
    // A map rather than a list of pairs because a chord can only mean one thing
    // at a time, and JSON's own duplicate-key rule then says so. Its ordering is
    // alphabetical and does not matter: two entries can only interact by naming
    // the same chord, which an object cannot do.
    std::map<std::string, std::string> keybindings;

    MIRO_REFLECT(font, theme, keybindings)
};

// The settings plus the palettes they resolve to, which is what the app applies.
//
// The two colour blocks in the file are *partial* — only the entries named — and
// they are layered onto the theme the file selected rather than replacing it.
// That falls straight out of how Miro loads: put the named palette in the struct
// first, load the file's block over it, and every key the file leaves out keeps
// the palette's answer without either side maintaining a list of which were set.
//
// Which is also the reason the blocks are separate from the palette itself
// rather than the palette being written out in full. A file that spelled all
// sixty-eight colours would make its own "theme" key do nothing, and the person
// who set it would have no way to see why.
//
// The keymap is the same shape of answer and needs no more code than the
// colours did, because Keymap was already built to be appended to: the defaults
// go in, the file's bindings go in after them, and "later wins" *is* the merge.
// What the colours have no equivalent of is taking something away, and that is
// an empty command id — the entry shadows the default, and the chord goes back
// to meaning nothing. See Keymap::bind.
struct Configuration
{
    Settings settings;
    Theme theme;

    // The default bindings with the file's layered over them. A chord the file
    // gives to a command that was never registered still takes the chord away
    // from whatever had it, deliberately: a rebinding that half-applied — the
    // old command still running because the new one was misspelt — is the one
    // outcome nobody could reason about.
    Keymap keymap;
};

// Which theme is in force: what the file said, and what has been picked from
// inside the app since.
//
// Two fields rather than one, because telling them apart *is* the rule. A theme
// picked from the palette outlives a re-read of the settings file, for the same
// reason the font zoom does — it is something someone did to this session, and
// the poll that stats the file once a second has no business undoing it. But the
// file wins whenever its own answer has moved, because editing "theme" is a
// deliberate act too, and the later of the two.
//
// Here rather than in the application for the reason the keymap table is: it is
// a rule, and a rule belongs where a test can read it. Nothing in it is written
// back to the file — see Settings.
class ThemeChoice
{
public:
    const std::string& name() const { return picked.empty() ? fromFile : picked; }

    // The file has been read again. Takes a picked theme away only when the
    // file's answer has changed since the last read, which is what separates
    // someone editing the key from the poll finding the same value there.
    void fileSaid(std::string name)
    {
        if (name == fromFile)
            return;

        fromFile = std::move(name);
        picked.clear();
    }

    void pick(std::string name) { picked = std::move(name); }

private:
    std::string fromFile;
    std::string picked;
};

// The key the theme is stored under, named once because two places spell it —
// the reflected field of Settings that reads it, and the writer that sets it.
// A drift between them is silent in the expensive direction: the write lands,
// the file looks right, and the key it wrote is one nothing reads.
inline constexpr auto themeKey = std::string_view {"theme"};

// Where the file lives: `ECode/settings.json` under the platform's per-user
// application data — Application Support on macOS, Roaming AppData on Windows,
// the XDG data home on Linux.
//
// Where the platform puts it rather than `~/.config`, which is where these
// started and is a convention from one of the three. It also matters more now
// than it did when nothing was written: an app that writes belongs in the
// directory the platform backs up and migrates, and on macOS `~/.config` is
// neither.
eacp::FilePath settingsPath();

// Where the file used to live. Only migrateSettings has any business with it.
eacp::FilePath legacySettingsPath();

// Moves a settings file to where settings live now, if and only if there is
// nothing at the destination and something at the source. Answers whether it
// moved anything, which is true at most once per machine.
//
// Both paths are arguments rather than being looked up, because the looked-up
// ones cannot be pointed anywhere else for a test: FilePath's directories come
// from the platform's own API and ignore $HOME. This way the whole of the rule
// is exercised against a scratch directory and only the two call arguments are
// not.
//
// The copy is written before the original is removed, so a remove that fails
// costs a stale file rather than the settings.
bool migrateSettings(const eacp::FilePath& from, const eacp::FilePath& to);

// Parses settings text. The whole of the reading logic, with no filesystem in
// it, which is what makes the layering testable without a home directory.
//
// Never throws and never reports: malformed JSON loads as "no keys", which is
// the same thing an empty file means and leaves every default standing. A
// settings file that has been broken by an edit should leave the editor working
// so that the file can be edited back.
Configuration configurationFromJson(std::string_view text);

// The palette `name` resolves to, with this file's own colour blocks layered on
// top of it.
//
// The two steps configurationFromJson takes for the name the *file* gives,
// named so they can be taken against a different base — which is what a theme
// chosen from inside the app is. Re-theming any other way means calling
// themeByName and assigning, and that silently drops every override the file
// makes: someone who had set one colour would watch it disappear the moment
// they picked a theme, with the file still saying what they had asked for.
Theme themeFromJson(std::string_view text, std::string_view name);

// The settings file's text, or empty when there is nothing readable there.
//
// The application keeps the text rather than only the Configuration it parses
// to, because the colour blocks have to be layered again whenever the theme
// changes underneath them — see themeFromJson — and reading the disk a second
// time would answer with whatever the file says *now* rather than with what is
// currently in force.
std::string readSettings(const eacp::FilePath& path);

// Writes a starter file at `path` if — and only if — nothing is there, and
// answers whether the path now holds a file.
//
// The template is the defaults with both colour blocks left empty, rather than
// every colour spelled out. Empty is the honest starting point: it says where an
// override goes without pre-writing sixty-eight of them, which would silently
// pin the file to one palette. The names that go in the blocks are the field
// names of ChromeTheme and TextTheme.
//
// Refusing to overwrite is the point rather than an optimisation. This runs
// behind a command someone can press twice.
bool writeSettingsTemplateIfAbsent(const eacp::FilePath& path);

// The template's text, so a test can read it without a filesystem.
std::string settingsTemplate();

// The settings text with one top-level string key set, and every other key left
// exactly as it was.
//
// Through the parsed document rather than through the reflected struct, which is
// the whole reason it exists — see Settings. Nothing is lost that this function
// does not name: not the colour blocks, not the keybindings, not a key some
// later version writes and this one has never heard of.
//
// Nothing when there is text and it is not a JSON object. That is somebody's
// file caught halfway through an edit, and overwriting it is the one mistake
// they cannot undo — whereas a file that is *absent* is not that case at all, so
// blank text starts from the template and the answer is a file that looks like
// one somebody could have written.
std::optional<std::string>
    settingsWith(std::string_view text, std::string_view key, std::string value);

// settingsWith, applied to the file at `path`. False when the file is there and
// unreadable as an object, or when the write failed.
bool writeSetting(const eacp::FilePath& path,
                  std::string_view key,
                  std::string value);

// One stat of the settings file, so a poll can tell whether it moved.
//
// Standing in for file watching, which eacp does not have — PLAN.md §3 gap 10 —
// and riding the timer that already stats every open file once a second for the
// same reason. One more stat a second is nothing next to a frame, and it is what
// makes editing the settings *inside ECode* work: save the tab and the theme
// changes, with no reload command to remember.
//
// Modification time paired with size, for the reason TextFile pairs them: a
// filesystem's timestamp granularity can be a whole second, so two writes inside
// one tick can share a stamp, and when they do the size is usually what differs.
class SettingsWatcher
{
public:
    explicit SettingsWatcher(eacp::FilePath pathToWatch);

    const eacp::FilePath& path() const { return filePath; }

    // Whether the file differs from the last time this was asked. False on the
    // first call: construction takes the stamp, so a file that has not been
    // touched since launch does not read as a change.
    //
    // A file appearing and a file being deleted both count. The first is what
    // happens the moment the template is written, and the second means the
    // defaults come back rather than the last-read settings sticking around
    // with nothing on disk behind them.
    bool poll();

private:
    eacp::FilePath filePath;

    std::int64_t modified = 0;
    std::uint64_t size = 0;
    bool exists = false;
};
} // namespace ecode
