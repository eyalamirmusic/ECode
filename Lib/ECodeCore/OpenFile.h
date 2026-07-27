#pragma once

#include "ScrollOffset.h"
#include "Style.h"
#include "TextFile.h"

#include <eacp/Core/Core.h>

namespace ecode
{
// One file open at once: the file itself, how it is coloured, and where the
// view of it had got to.
struct OpenFile
{
    TextFile file;

    // One parse tree per document rather than one for the workspace. A shared
    // highlighter would have to reparse on every switch — ~40 ms on an 8,000
    // line file, paid on a ⌘W rather than on an open — and would hand the
    // renderer a tree describing text that is no longer on screen in between.
    //
    // Null is a valid state and means "draw this as plain text", which is what
    // a grammar that failed to load leaves behind.
    eacp::OwningPointer<Highlighter> highlighter;

    // The scroll offset belongs to the file rather than to the widget showing
    // it, so switching away and back leaves the text where it was left.
    //
    // Kept here rather than saved and restored around each switch for the same
    // reason Editor owns the line map: a step that has to be remembered at
    // every call site is one that will eventually be forgotten, and forgetting
    // it means the view jumping on a tab switch with nothing to say why.
    ScrollOffset scroll;
};
} // namespace ecode
