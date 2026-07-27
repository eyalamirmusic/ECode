#pragma once

#include <ECodeWidgets/Theme.h>

#include <ECodeRender/TextTheme.h>

#include <eacp/Core/Utils/Containers.h>

#include <string>
#include <string_view>

namespace ecode
{
// The built-in palettes, by name.
//
// Two halves — the chrome's and the document's — because they are read by
// layers that do not link each other, but one table, because a theme is a
// single thing to the person choosing it: picking "light" and getting a light
// sidebar around a dark file is not a theme, it is a bug.
//
// A name is the whole of what the settings file stores. Everything else it may
// say about colour is an override on top of whichever of these it named; see
// Settings.

// Both halves of one theme, which is the unit the rest of the app applies.
struct Theme
{
    ChromeTheme chrome;
    TextTheme text;
};

// Every name that resolves, in the order a picker should offer them. The
// default is first.
eacp::Vector<std::string> themeNames();

// The named palette, or the default for a name that is not in the table.
//
// Falling back rather than failing, because the name comes from a hand-edited
// file: a typo should cost the theme, not the launch, and the status quo is the
// least surprising thing to be left looking at. Callers that want to say so out
// loud ask isThemeName first.
Theme themeByName(std::string_view name);

bool isThemeName(std::string_view name);

// What themeByName falls back to, and what a file that says nothing gets.
inline constexpr auto defaultThemeName = std::string_view {"dark"};
} // namespace ecode
