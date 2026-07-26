#pragma once

#include "Document.h"

#include <cstddef>

namespace ecode
{
class LineMap;

// A caret and, when the two differ, a selection.
//
// One type for both, because in an editor they are the same thing: a caret is
// a selection of zero length. Keeping them separate means every operation has
// to handle two cases, and the ones that get forgotten are the bugs — typing
// over a selection, or arrow-keying out of one.
struct Cursor
{
    // Byte offsets into the document. `head` is where the caret is and where
    // typing happens; `anchor` is the fixed end a shift-selection grew from.
    std::size_t head = 0;
    std::size_t anchor = 0;

    // The column vertical movement is trying to hold.
    //
    // Moving down from column 40 through a short line and out the other side
    // should return to column 40, not to the short line's end. That only works
    // if the intended column outlives the lines it passes through, so it is
    // remembered here and cleared by any horizontal movement.
    //
    // A *display* column measured from the start of the caret's own visual row,
    // not a byte offset into its logical line — the number is being used to
    // land the caret under where it looked like it was, and a tab is one byte
    // and four columns wide. See LineMap.
    std::size_t desiredColumn = 0;
    bool holdsColumn = false;

    bool hasSelection() const { return head != anchor; }

    std::size_t start() const { return head < anchor ? head : anchor; }
    std::size_t end() const { return head < anchor ? anchor : head; }
    std::size_t length() const { return end() - start(); }

    void collapse() { anchor = head; }

    // Places the caret, dropping any selection and any held column.
    void moveTo(std::size_t offset)
    {
        head = offset;
        anchor = offset;
        holdsColumn = false;
    }

    // Moves the head, keeping the anchor — a shift-selection.
    void extendTo(std::size_t offset)
    {
        head = offset;
        holdsColumn = false;
    }
};

// Cursor movement over a document. Free functions rather than members because
// none of them need the cursor's state: they answer "where is the next X from
// here", and the caller decides whether that becomes a move or an extension.
namespace Motion
{
// One codepoint left or right.
//
// Codepoints, not grapheme clusters. A combining accent or a flag emoji is
// several codepoints and should move as one unit; doing that properly needs
// grapheme segmentation, which eacp has no support for. Stepping whole
// codepoints at least never lands mid-character and corrupts the text — the
// failure it does have is a caret that pauses inside a composed emoji.
std::size_t left(const Document& document, std::size_t offset);
std::size_t right(const Document& document, std::size_t offset);

// The start of the previous word, and the end of the next — what Alt+Arrow
// does. Punctuation and whitespace are skipped on the way.
std::size_t wordLeft(const Document& document, std::size_t offset);
std::size_t wordRight(const Document& document, std::size_t offset);

// Home and End. `lineStart` stops at the first non-blank rather than column
// zero when the caret is already past it, matching what editors do with a
// first Home press on an indented line.
//
// On a wrapped line both stop at the visual row's own edges first: Home on a
// continuation row goes to where that row begins on screen, and End to where it
// ends. Pressing Home again from there falls through to the logical line, which
// is the behaviour VSCode has and the one that makes a wrapped paragraph
// navigable without counting rows.
std::size_t
    lineStart(const Document& document, const LineMap& lines, std::size_t offset);
std::size_t
    lineEnd(const Document& document, const LineMap& lines, std::size_t offset);

// Up or down by `rows`, honouring the cursor's held column and setting it if
// this is the first vertical move of a run. Mutates the cursor's column state,
// which is why it takes the cursor rather than an offset.
//
// Visual rows, not logical lines. On a wrapped line the two differ, and moving
// by lines there means one press of Down skipping a whole paragraph — the caret
// leaving the screen position it was at is the thing vertical movement is
// supposed to preserve.
std::size_t vertical(const Document& document,
                     const LineMap& lines,
                     Cursor& cursor,
                     int rows);

std::size_t documentStart(const Document& document);
std::size_t documentEnd(const Document& document);
} // namespace Motion
} // namespace ecode
