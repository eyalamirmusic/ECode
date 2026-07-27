#pragma once

// Not only for the macro: it carries the ADL hook that makes every field below
// a "#rrggbb" string rather than an object of four numbers.
#include "ColorJson.h"

#include <ECodeCore/Style.h>

#include <eacp/Graphics/Graphics.h>

#include <Miro/Reflect.h>

namespace ecode
{
// Colours for a *document*: the text, the gutter, the caret, the selection, and
// one entry per TokenKind.
//
// Its own header rather than TextRenderer's, because a theme is now data that a
// config file reads and writes, and the file that does that has no business
// pulling in the renderer, the row cache and the glyph atlas to get at a struct
// of colours. ChromeTheme — the palette for everything *around* a document — is
// the matching half, and lives with the widgets that use it.
//
// The defaults are the dark theme, so a default-constructed one is what ECode
// has always looked like and the built-in table names it "dark". Every field is
// a JSON key of the same name; see Settings for what a file may leave out.
struct TextTheme
{
    eacp::Graphics::Color background {0.118f, 0.125f, 0.149f};
    eacp::Graphics::Color text {0.85f, 0.87f, 0.91f};
    eacp::Graphics::Color lineNumber {0.38f, 0.41f, 0.48f};
    eacp::Graphics::Color currentLineNumber {0.75f, 0.78f, 0.85f};
    eacp::Graphics::Color gutterEdge {1.f, 1.f, 1.f, 0.05f};
    eacp::Graphics::Color caret {0.55f, 0.78f, 0.98f};
    eacp::Graphics::Color selection {0.22f, 0.32f, 0.46f};
    eacp::Graphics::Color currentLine {1.f, 1.f, 1.f, 0.035f};

    // Search hits. Every match gets the dim one and the match being looked at
    // gets the bright one, because the two answer different questions — "where
    // else is this?" and "which one am I on?" — and a single colour for both
    // makes the second unanswerable without counting.
    //
    // Both are drawn under the glyphs, so they have to stay dark enough to read
    // through. That is why the current match is a stronger orange rather than a
    // lighter fill: raising the brightness far enough to distinguish it would
    // start to swallow the text on top of it.
    eacp::Graphics::Color searchMatch {0.35f, 0.31f, 0.16f};
    eacp::Graphics::Color currentSearchMatch {0.62f, 0.44f, 0.13f};

    // One colour per TokenKind. A syntax engine maps its captures onto kinds and
    // never names a colour; this is the only place colours live.
    eacp::Graphics::Color keyword {0.78f, 0.57f, 0.92f};
    eacp::Graphics::Color string {0.65f, 0.85f, 0.55f};
    eacp::Graphics::Color comment {0.42f, 0.47f, 0.55f};
    eacp::Graphics::Color number {0.95f, 0.72f, 0.45f};
    eacp::Graphics::Color function {0.45f, 0.72f, 0.95f};
    eacp::Graphics::Color type {0.40f, 0.85f, 0.82f};
    eacp::Graphics::Color constant {0.95f, 0.62f, 0.60f};
    eacp::Graphics::Color operatorColor {0.80f, 0.82f, 0.88f};
    eacp::Graphics::Color punctuation {0.62f, 0.66f, 0.74f};
    eacp::Graphics::Color preprocessor {0.90f, 0.68f, 0.50f};

    const eacp::Graphics::Color& colorFor(TokenKind kind) const;

    MIRO_REFLECT(background,
                 text,
                 lineNumber,
                 currentLineNumber,
                 gutterEdge,
                 caret,
                 selection,
                 currentLine,
                 searchMatch,
                 currentSearchMatch,
                 keyword,
                 string,
                 comment,
                 number,
                 function,
                 type,
                 constant,
                 operatorColor,
                 punctuation,
                 preprocessor)
};
} // namespace ecode
