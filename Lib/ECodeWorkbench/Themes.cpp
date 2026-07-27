#include "Themes.h"

namespace ecode
{
using namespace eacp;

namespace
{
// The palettes below are written the way a theme file writes them, so a colour
// can be read off one and pasted into the other. constexpr, so a table still
// costs nothing at static-init — which is why this is not fromHexColor.
constexpr Graphics::Color rgb(std::uint32_t value)
{
    return {static_cast<float>((value >> 16) & 0xffu) / 255.f,
            static_cast<float>((value >> 8) & 0xffu) / 255.f,
            static_cast<float>(value & 0xffu) / 255.f};
}

constexpr Graphics::Color rgba(std::uint32_t value, float alpha)
{
    return rgb(value).withAlpha(alpha);
}

// The dark theme is the structs' own defaults rather than a table, and that is
// deliberate: it means a default-constructed ChromeTheme is what ECode has
// always drawn, so every test and every caller that never heard of a theme name
// keeps the picture it had. Naming it here is what makes it selectable.
Theme darkTheme()
{
    return {};
}

// Light, built by one rule: keep each entry's *hue* and flip its lightness.
//
// The rule matters more than any individual colour. A theme switch that also
// recoloured what things mean — comments going from grey to green, strings from
// green to red — is two changes at once, and the second one is the one nobody
// asked for. So a keyword stays purple and a string stays green; only the
// direction they are read against changes.
//
// The consequence to watch is the translucent entries. Every overlay in the dark
// palette is white at low alpha, because lightening is how you lift something off
// a dark surface. Here they are all black, and an overlay left white would be a
// hover state that cannot be seen at all.
Theme lightTheme()
{
    auto theme = Theme {};

    auto& chrome = theme.chrome;

    // Same ordering as the dark palette reading from most to least recessed:
    // the tab strip is the darkest surface, the active tab matches the editor,
    // and the status bar is the one that stands away from the window — which in
    // a light theme means darker rather than lighter.
    chrome.activityBar = rgb(0xececec);
    chrome.sidebar = rgb(0xf3f3f3);
    chrome.tabBar = rgb(0xe8e8e8);
    chrome.activeTab = rgb(0xffffff);
    chrome.statusBar = rgb(0xd6d6d6);

    chrome.activeTabText = rgb(0x1f1f1f);
    chrome.inactiveTabText = rgb(0x6a6a6a);
    chrome.statusText = rgb(0x2b2b2b);

    chrome.activeTabAccent = rgb(0x0066b8);
    chrome.inactiveGroupAccent = rgb(0xb0b0b0);

    chrome.hoverTab = rgba(0x000000, 0.05f);
    chrome.tabSeparator = rgba(0x000000, 0.09f);

    chrome.tabCloseIcon = rgb(0x6a6a6a);
    chrome.tabCloseIconHover = rgb(0x1f1f1f);
    chrome.tabCloseHover = rgba(0x000000, 0.10f);

    chrome.scrollThumb = rgba(0x000000, 0.20f);
    chrome.scrollThumbActive = rgba(0x000000, 0.40f);

    chrome.rowText = rgb(0x3b3b3b);
    chrome.rowDirectoryText = rgb(0x1f1f1f);
    chrome.rowSelected = rgba(0x000000, 0.08f);

    chrome.unsaved = rgb(0x4a4a4a);
    chrome.conflict = rgb(0xb34700);

    // Lighter than the dark theme's scrim. A dark backdrop over light content
    // is already a strong signal, and 45% of black over white reads as the
    // lights going out rather than as a panel opening.
    chrome.paletteBackdrop = rgba(0x000000, 0.25f);

    // Off-white rather than white, because the editor underneath is white: a
    // floating panel that shares its fill with the page has only its border
    // left to say where it starts.
    chrome.paletteBackground = rgb(0xf8f8f8);
    chrome.paletteBorder = rgba(0x000000, 0.16f);
    chrome.paletteSelected = rgba(0x000000, 0.08f);

    chrome.paletteText = rgb(0x1f1f1f);
    chrome.paletteMatchText = rgb(0x0a5fc2);
    chrome.paletteHintText = rgb(0x767676);
    chrome.paletteDisabledText = rgb(0xa0a0a0);

    chrome.splitter = rgba(0x000000, 0.10f);
    chrome.splitterActive = rgb(0x0066b8);

    chrome.menuBackground = rgb(0xf8f8f8);
    chrome.menuBorder = rgba(0x000000, 0.16f);
    chrome.menuHighlight = rgb(0x0060c0);
    chrome.menuHighlightText = rgb(0xffffff);
    chrome.menuText = rgb(0x1f1f1f);
    chrome.menuShortcutText = rgb(0x767676);
    chrome.menuDisabledText = rgb(0xa0a0a0);
    chrome.menuSeparator = rgba(0x000000, 0.12f);

    chrome.findBackground = rgb(0xf8f8f8);
    chrome.findBorder = rgba(0x000000, 0.16f);

    // The one entry that inverts rather than flipping. In the dark palette the
    // field is *darker* than the bar around it, because a recess is a shadow;
    // on a light bar the same recess is white, and a field darker than its bar
    // would read as filled-in rather than as empty and waiting.
    chrome.findFieldBackground = rgb(0xffffff);

    chrome.findText = rgb(0x1f1f1f);
    chrome.findHintText = rgb(0x767676);
    chrome.findToggleOn = rgb(0x0066b8);
    chrome.findToggleOnText = rgb(0xffffff);
    chrome.findNoResults = rgb(0xb34700);

    auto& text = theme.text;

    text.background = rgb(0xffffff);
    text.text = rgb(0x1f1f1f);
    text.lineNumber = rgb(0x9a9a9a);
    text.currentLineNumber = rgb(0x3b3b3b);
    text.gutterEdge = rgba(0x000000, 0.07f);
    text.caret = rgb(0x0a5fc2);
    text.selection = rgb(0xadd6ff);
    text.currentLine = rgba(0x000000, 0.045f);

    // Both still drawn under the glyphs, so the constraint from the dark
    // palette holds with its sign flipped: these have to stay *light* enough
    // for black text to read through, which is why the current match is a
    // deeper amber rather than a darker one.
    text.searchMatch = rgb(0xfbe9a0);
    text.currentSearchMatch = rgb(0xf6c945);

    text.keyword = rgb(0x8250df);
    text.string = rgb(0x1a7f37);
    text.comment = rgb(0x6a737d);
    text.number = rgb(0xb35900);
    text.function = rgb(0x005cc5);
    text.type = rgb(0x0e7490);
    text.constant = rgb(0xb31d28);
    text.operatorColor = rgb(0x24292f);
    text.punctuation = rgb(0x57606a);
    text.preprocessor = rgb(0x9a5518);

    return theme;
}
} // namespace

eacp::Vector<std::string> themeNames()
{
    return {std::string {defaultThemeName}, "light"};
}

bool isThemeName(std::string_view name)
{
    for (const auto& known: themeNames())
        if (known == name)
            return true;

    return false;
}

Theme themeByName(std::string_view name)
{
    if (name == "light")
        return lightTheme();

    return darkTheme();
}
} // namespace ecode
