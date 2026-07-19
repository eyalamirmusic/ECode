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

    // Unsaved work, and a save that could not happen.
    eacp::Graphics::Color unsaved {0.694f, 0.741f, 0.831f};
    eacp::Graphics::Color conflict {0.898f, 0.541f, 0.310f};
};
} // namespace ecode
