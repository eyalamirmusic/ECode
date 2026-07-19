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
};
} // namespace ecode
