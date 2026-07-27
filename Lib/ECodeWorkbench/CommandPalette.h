#pragma once

#include "Keymap.h"
#include <ECodeWidgets/ListView.h>
#include <ECodeWidgets/ScrollView.h>
#include <ECodeWidgets/TextField.h>
#include <ECodeWidgets/Theme.h>

#include <ECodeCore/Commands.h>
#include <ECodeCore/FuzzyMatch.h>

#include <functional>
#include <string>

namespace ecode
{
// One thing the palette can offer.
//
// A command was the only kind for a while, and is still the kind the palette is
// named after — but the box is a query over a list of things with an action
// each, and a theme picker is that same box over a different list. So an item
// carries its action rather than an index into somewhere, and the palette needs
// to know nothing about where the list came from.
struct PaletteItem
{
    std::string title;

    // Right-aligned against the box's edge, the way a menu prints a shortcut. A
    // command puts its chord here; a list with nothing to say leaves it empty.
    std::string hint;

    std::function<void()> run = [] {};

    // Asked at paint time rather than when the list was built, so a command
    // that becomes unavailable while the palette is open greys out where it
    // stands.
    std::function<bool()> isEnabled = [] { return true; };

    // Shows what choosing this would do, without choosing it.
    //
    // CowTerm's *peek*: arrow through and the thing behind the palette changes
    // live, Enter keeps it, Escape puts back what was there. It is what makes a
    // theme worth picking here rather than from a menu, since the only way to
    // judge one is to look at a file drawn in it.
    //
    // Nothing for a command, and deliberately: running one *is* its effect, and
    // a previewed Save has already happened.
    std::function<void()> preview = [] {};
};

// The command palette: a query, the things that fuzzy-match it, and Enter.
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

    void themeChanged() override;

    // Opens over the registry, with an empty query — so the palette always
    // starts by offering everything rather than resuming a filter the person
    // has forgotten typing.
    void show();

    // Opens over a list of the caller's own.
    //
    // `restore` is the half of the peek pattern that cannot live on an item: an
    // item knows how to show itself, and only the caller knows what was on
    // screen before the first arrow key moved. It runs when the palette is
    // dismissed without choosing anything, and never when something was chosen.
    //
    // Both of these are per-opening rather than wiring, which is why they are
    // arguments rather than members: show() takes the default and so a command
    // palette opened after a picker cannot inherit the picker's undo.
    void show(
        eacp::Vector<PaletteItem> itemsToOffer,
        std::string prompt,
        std::function<void()> restore = [] {});

    void hide();
    bool isOpen() const { return isVisible(); }

    // Fired after the palette closes, however it closed. The application uses
    // it to put focus back where it was — the palette deliberately does not
    // know about the host, so it cannot restore focus itself.
    std::function<void()> onClosed = [] {};

    // What the application should focus after show(). The palette is a container
    // now; the thing that takes the keyboard is the query field inside it.
    //
    // Focusing the palette itself was the old arrangement, and it is what forced
    // the palette to reimplement a text box: a widget that owns the keyboard has
    // to handle every key, so it grew its own caret, its own UTF-8 backspace and
    // its own idea of what counts as typing — the last of which was wrong, and
    // put a private-use codepoint into the query on every press of Left.
    Widget& keyboardTarget() { return input; }

    void setQuery(std::string text);
    const std::string& query() const { return input.text(); }

    // One item that survived the filter, with where in its title the query
    // matched so the palette can pick those characters out.
    struct Entry
    {
        // Index into items(), which is int-indexed like every eacp Vector.
        // ListView counts its rows in size_t, so the cast happens at that
        // boundary and nowhere else.
        int item = 0;

        FuzzyMatch match;
    };

    // Everything this opening is offering, unfiltered and in the order it was
    // given. A snapshot: the registry is read when the palette opens, not per
    // keystroke, which is also what makes each item's hint a string resolved
    // once rather than a chord looked up per visible row per frame.
    const eacp::Vector<PaletteItem>& items() const { return offered; }

    // What survived the query, best match first. Public because it is the
    // honest way to test the filter without a device.
    const eacp::Vector<Entry>& entries() const { return matches; }

    const PaletteItem& itemOf(const Entry& entry) const
    {
        return offered[entry.item];
    }

    int selectedEntry() const { return list.selectedRow(); }

    // Highlights whichever entry stands for `item`, previewing it, and does
    // nothing when the query has filtered it out.
    //
    // For a picker that should open on the value already in force rather than
    // on its first row — which is where the highlight otherwise starts, and
    // would mean opening the theme picker immediately previewed some other
    // theme.
    void selectItem(int item);

    // Runs the highlighted item and closes. Nothing happens when there is no
    // selection or the item is disabled, and in particular the palette stays
    // open — closing on a keystroke that did nothing reads as a dropped input.
    void acceptSelection();

    void layout() override;

    // The backdrop is this widget's, so it takes the clicks that dismiss.
    bool wantsMouse() const override { return true; }

    // Not a focus stop of its own — see keyboardTarget(). Keys still reach it,
    // as the field's parent, for the ones the field passes up.
    bool acceptsFocus() const override { return false; }

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
    // Every registered command as an item, with the chord that runs it.
    eacp::Vector<PaletteItem> commandItems() const;

    // Shared by both show()s and by acceptSelection, which is the one closing
    // that must not run `restore`.
    void open(eacp::Vector<PaletteItem> itemsToOffer,
              std::string prompt,
              std::string emptyText,
              std::function<void()> restore);

    void close(bool accepted);

    void refilter();

    // Runs the highlighted item's preview, unless it is already what was last
    // previewed.
    //
    // Not left to ListView::onSelectionChanged, which reports a moved *row*: a
    // keystroke that refilters can leave the highlight on row 0 while row 0 has
    // become a different item, and a preview hung off the row alone would go on
    // showing whatever was highlighted before the query narrowed.
    void previewSelection();

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

    eacp::Vector<PaletteItem> offered;
    eacp::Vector<Entry> matches;

    // What the query line reads when nothing has been typed, and what the box
    // says when nothing matched. Both belong to the opening rather than to the
    // widget: "No matching commands" is wrong over a list of themes.
    std::string emptyMessage;

    // What the last dismissal has to undo, and the item its preview last ran.
    std::function<void()> restoreOnCancel = [] {};
    int previewed = -1;

    TextField input;
    ScrollView results;
    ListView list;
};
} // namespace ecode
