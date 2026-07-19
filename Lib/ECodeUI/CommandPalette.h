#pragma once

#include "Keymap.h"
#include "ListView.h"
#include "ScrollView.h"
#include "Theme.h"

#include <ECodeCore/Commands.h>
#include <ECodeCore/FuzzyMatch.h>

#include <functional>
#include <string>

namespace ecode
{
// The command palette: a query, the commands that fuzzy-match it, and Enter.
//
// It is a child of the root widget covering the whole window, rather than a
// popup. PaintContext has no notion of a layer escaping its parent's clip —
// there is one scissor rect and ClipScope only ever narrows it — so an overlay
// is something laid out over everything rather than something drawn outside
// something else. Covering the window is also what makes a click anywhere
// outside the box dismiss it, without a separate backdrop widget to catch them.
//
// It holds no commands of its own: the registry is the list and the keymap is
// the shortcut column, so a command registered anywhere in the app appears here
// with its binding and nothing has to be added in two places.
class CommandPalette final : public Widget
{
public:
    CommandPalette(const ChromeTheme& themeToUse,
                   const CommandRegistry& registryToUse,
                   const Keymap& keymapToUse);

    // Opens with an empty query, so the palette always starts by offering
    // everything rather than resuming a filter the person has forgotten typing.
    void show();
    void hide();
    bool isOpen() const { return isVisible(); }

    // Fired after the palette closes, however it closed. The application uses
    // it to put focus back where it was — the palette deliberately does not
    // know about the host, so it cannot restore focus itself.
    std::function<void()> onClosed = [] {};

    void setQuery(std::string text);
    const std::string& query() const { return queryText; }

    // One command that survived the filter, with where in its title the query
    // matched so the palette can pick those characters out.
    struct Entry
    {
        // Index into the registry, which is int-indexed like every eacp
        // Vector. ListView counts its rows in size_t, so the cast happens at
        // that boundary and nowhere else.
        int command = 0;

        FuzzyMatch match;

        // The chord that runs it, already rendered. Cached here because it
        // depends on the keymap rather than on the query, and recomputing a
        // string per visible row per frame is work with no reason.
        std::string shortcut;
    };

    // What the palette is currently offering, best match first. Public because
    // it is the honest way to test the filter without a device.
    const eacp::Vector<Entry>& entries() const { return matches; }

    int selectedEntry() const { return list.selectedRow(); }

    // Runs the highlighted command and closes. Nothing happens when there is no
    // selection or the command is disabled, and in particular the palette stays
    // open — closing on a keystroke that did nothing reads as a dropped input.
    void acceptSelection();

    void layout() override;

    bool wantsMouse() const override { return true; }
    bool acceptsFocus() const override { return true; }

    void prepare(eacp::Text::GlyphAtlas& atlas,
                 const eacp::Graphics::Rect& visible) override;
    void paint(PaintContext& context) override;

    void mouseDown(const eacp::Graphics::MouseEvent& event) override;
    bool keyDown(const eacp::Graphics::KeyEvent& event) override;

    // The box itself, as against the backdrop filling the rest of the window.
    eacp::Graphics::Rect boxBounds() const;
    eacp::Graphics::Rect inputBounds() const;
    eacp::Graphics::Rect resultsBounds() const;

private:
    void refilter();
    void paintRow(PaintContext& context,
                  std::size_t index,
                  const eacp::Graphics::Rect& area,
                  bool selected);

    // Draws the title with the matched characters in their own colour, by
    // splitting it into runs of matched and unmatched text.
    void drawTitle(PaintContext& context,
                   const Entry& entry,
                   float x,
                   float baseline,
                   const eacp::Graphics::Color& base) const;

    float resultsHeight() const;

    const ChromeTheme& theme;
    const CommandRegistry& registry;
    const Keymap& keymap;

    std::string queryText;
    eacp::Vector<Entry> matches;

    ScrollView results;
    ListView list;
};
} // namespace ecode
