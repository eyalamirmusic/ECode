#pragma once

#include "Keymap.h"
#include "Themes.h"

#include <ECodeRender/FontSettings.h>

#include <eacp/Core/Utils/FilePath.h>

#include <Miro/Reflect.h>

#include <map>
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
// Nothing writes this file behind the person editing it. ECode reads it, and
// the only thing it ever writes is a starter template, and only when there is
// no file there at all — so a comment-free JSON round trip can never eat a
// block someone hand-wrote. It is also why ⌘+ does not persist: see
// FontSettings.
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

// Where the file lives. `~/.config/ecode.json`, matching CowTerm's
// `~/.config/cowterm.json` — the two are sibling projects on one machine, and a
// developer who has found one should not have to hunt for the other.
eacp::FilePath settingsPath();

// Parses settings text. The whole of the reading logic, with no filesystem in
// it, which is what makes the layering testable without a home directory.
//
// Never throws and never reports: malformed JSON loads as "no keys", which is
// the same thing an empty file means and leaves every default standing. A
// settings file that has been broken by an edit should leave the editor working
// so that the file can be edited back.
Configuration configurationFromJson(std::string_view text);

// The file at `path`, or the defaults if there is nothing readable there.
Configuration loadConfiguration(const eacp::FilePath& path);
Configuration loadConfiguration();

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
