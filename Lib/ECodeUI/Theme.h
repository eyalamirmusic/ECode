#pragma once

#include <eacp/Graphics/Graphics.h>

namespace ecode
{
// Colours for the chrome around the editor.
//
// Separate from TextTheme, which colours a *document* — token kinds, selection,
// caret, gutter. Both are hardcoded structs for now; PLAN.md §5 has themes
// becoming data, at which point these are two tables in one file rather than
// two structs in two headers. constexpr throughout, so a palette costs nothing
// at static-init and lives in rodata.
struct ChromeTheme
{
    eacp::Graphics::Color activityBar {0.094f, 0.098f, 0.118f};
    eacp::Graphics::Color sidebar {0.102f, 0.110f, 0.129f};
    eacp::Graphics::Color tabBar {0.086f, 0.090f, 0.110f};
    eacp::Graphics::Color activeTab {0.118f, 0.125f, 0.149f};
    eacp::Graphics::Color statusBar {0.180f, 0.192f, 0.235f};

    eacp::Graphics::Color activeTabText {0.85f, 0.87f, 0.91f};
    eacp::Graphics::Color inactiveTabText {0.55f, 0.58f, 0.65f};
    eacp::Graphics::Color statusText {0.88f, 0.90f, 0.94f};

    // A tab's accent, drawn along its top edge, so the active tab reads as
    // active without relying on the fill alone.
    eacp::Graphics::Color activeTabAccent {0.35f, 0.55f, 0.85f};

    // Scrollbar thumbs. Dim until grabbed — a scrollbar is a status readout
    // most of the time and a control only briefly.
    eacp::Graphics::Color scrollThumb {1.f, 1.f, 1.f, 0.16f};
    eacp::Graphics::Color scrollThumbActive {1.f, 1.f, 1.f, 0.34f};

    // Rows in a list or tree. No hover colour: nothing tracks the pointer yet,
    // and a palette entry for a state that does not exist reads as though it
    // does.
    eacp::Graphics::Color rowText {0.78f, 0.81f, 0.87f};
    eacp::Graphics::Color rowDirectoryText {0.88f, 0.90f, 0.94f};
    eacp::Graphics::Color rowSelected {1.f, 1.f, 1.f, 0.09f};

    // Unsaved work, and a save that could not happen.
    eacp::Graphics::Color unsaved {0.694f, 0.741f, 0.831f};
    eacp::Graphics::Color conflict {0.898f, 0.541f, 0.310f};

    // The command palette. The backdrop is translucent rather than opaque so
    // the file stays legible underneath — the palette is a thing on top of the
    // work, not a screen that replaces it — and the border stands in for the
    // drop shadow the sprite renderer has no way to draw.
    eacp::Graphics::Color paletteBackdrop {0.f, 0.f, 0.f, 0.45f};
    eacp::Graphics::Color paletteBackground {0.145f, 0.157f, 0.184f};
    eacp::Graphics::Color paletteBorder {1.f, 1.f, 1.f, 0.13f};
    eacp::Graphics::Color paletteSelected {1.f, 1.f, 1.f, 0.10f};

    eacp::Graphics::Color paletteText {0.88f, 0.90f, 0.94f};

    // The characters the query actually matched, picked out of the title.
    eacp::Graphics::Color paletteMatchText {0.42f, 0.68f, 0.98f};

    // Placeholder, shortcut column, and the empty-result line — everything the
    // palette says rather than offers.
    eacp::Graphics::Color paletteHintText {0.50f, 0.53f, 0.60f};

    // A command that is listed but cannot run right now: undo with nothing to
    // undo. Greyed rather than hidden, since a command vanishing from the list
    // is harder to understand than one that is visibly unavailable.
    eacp::Graphics::Color paletteDisabledText {0.42f, 0.44f, 0.50f};

    // The splitter between the sidebar and the editor. Nearly invisible until
    // the pointer is on it: a divider is a seam most of the time and a control
    // only while someone is reaching for it, so a permanently lit line would be
    // drawing attention to the one part of the chrome that has nothing to say.
    eacp::Graphics::Color splitter {1.f, 1.f, 1.f, 0.06f};
    eacp::Graphics::Color splitterActive {0.35f, 0.55f, 0.85f};

    // The in-window context menu. `Graphics::Menu` is the native menu bar and
    // has no popup, so this one is drawn by us like everything else inside the
    // GPU view.
    //
    // Deliberately its own set rather than borrowed from the palette's, even
    // though the two are the same material today: the palette is a modal panel
    // and a context menu is a transient popup, and the first time either grows
    // a shadow or a translucency the other will not want it.
    eacp::Graphics::Color menuBackground {0.145f, 0.157f, 0.184f};
    eacp::Graphics::Color menuBorder {1.f, 1.f, 1.f, 0.13f};

    // The row under the pointer or the arrow keys. A context menu is the first
    // thing here that tracks the pointer at all — every other widget ignores it
    // — so this is the first hover colour in the theme.
    eacp::Graphics::Color menuHighlight {0.24f, 0.42f, 0.68f};
    eacp::Graphics::Color menuHighlightText {0.96f, 0.97f, 0.99f};

    eacp::Graphics::Color menuText {0.88f, 0.90f, 0.94f};
    eacp::Graphics::Color menuShortcutText {0.50f, 0.53f, 0.60f};
    eacp::Graphics::Color menuDisabledText {0.42f, 0.44f, 0.50f};
    eacp::Graphics::Color menuSeparator {1.f, 1.f, 1.f, 0.10f};

    // The find bar. Opaque, unlike the palette's backdrop: it sits over the text
    // it is searching and stays there while the person works, so anything
    // showing through would be the very lines they are trying to read.
    eacp::Graphics::Color findBackground {0.145f, 0.157f, 0.184f};
    eacp::Graphics::Color findBorder {1.f, 1.f, 1.f, 0.13f};
    eacp::Graphics::Color findFieldBackground {0.098f, 0.106f, 0.125f};

    eacp::Graphics::Color findText {0.88f, 0.90f, 0.94f};
    eacp::Graphics::Color findHintText {0.50f, 0.53f, 0.60f};

    // An option that is on. A filled chip rather than a brighter label, because
    // the difference has to be readable at a glance and two weights of grey are
    // not — this is state the search result depends on, and someone puzzled by a
    // missing match needs to see it without hunting.
    eacp::Graphics::Color findToggleOn {0.24f, 0.42f, 0.68f};
    eacp::Graphics::Color findToggleOnText {0.96f, 0.97f, 0.99f};

    // A query that matched nothing. Said in words as well, so it does not rely
    // on colour alone.
    eacp::Graphics::Color findNoResults {0.898f, 0.541f, 0.310f};
};
} // namespace ecode
