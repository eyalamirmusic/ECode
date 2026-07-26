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

    // Which end the head is on. A selection grown leftwards shrinks from the
    // left when Shift+Right is pressed, so the direction is not cosmetic.
    bool isReversed() const { return head < anchor; }

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

    // Covers `offset` in the half-open sense a selection already uses, except
    // that a bare caret covers the one offset it sits at. A zero-length range
    // contains nothing under the half-open rule, and ⌥-clicking a caret to take
    // it away again has to be able to find it.
    bool covers(std::size_t offset) const
    {
        if (!hasSelection())
            return offset == head;

        return offset >= start() && offset < end();
    }
};

// The cursors an editor edits through: at least one, in document order, and
// never overlapping.
//
// Those three properties are the whole reason this is a type rather than a
// vector. Two cursors in the same place type every character twice; two out of
// order make the edit at one shift the other by a delta computed from an offset
// that has already moved. Both are silent — the text simply comes out wrong —
// so the repair runs after every operation rather than at each call site that
// could forget it. transform() is the only way in, and it always repairs.
class CursorSet
{
public:
    // The cursor that everything written against a single one still means: the
    // status bar's line and column, the offset a search resumes from, the caret
    // the view scrolls to follow. It is the most recently added, which is what
    // makes ⌥-click then ⌘F search from where the click landed.
    const Cursor& primary() const { return carets[primaryIndex]; }

    int count() const { return carets.size(); }
    bool hasMultiple() const { return carets.size() > 1; }

    const Cursor& operator[](int index) const { return carets[index]; }

    auto begin() const { return carets.begin(); }
    auto end() const { return carets.end(); }

    // Back to exactly one cursor. The set is never empty, so this is how a
    // plain click replaces it rather than clearing and rebuilding.
    void reset(Cursor only);

    // Back to one, keeping the primary — Escape.
    void collapseToPrimary() { reset(primary()); }

    // Adds a cursor and makes it the primary. False when it merged into one
    // already there, so ⌥-clicking inside an existing selection is reported as
    // having added nothing while still moving the primary onto it.
    bool add(Cursor extra);

    // Takes away the cursor covering `offset`. False when there is none, and
    // false for the last one left: the set is never empty, and a click that
    // silently emptied it would leave a window with no caret.
    bool removeCovering(std::size_t offset);

    // -1 when no cursor covers it.
    int indexCovering(std::size_t offset) const;

    void makePrimary(int index) { primaryIndex = index; }

    // Applies `change` to every cursor in document order, then repairs the
    // invariant. Every mutation of the set goes through this, so there is one
    // place to get the repair right rather than a dozen places to forget it.
    //
    // Document order matters to more than tidiness: an edit loop accumulates
    // how far the text below it has shifted, and that is only valid walking
    // upwards through cursors that are already sorted.
    template <typename Change>
    void transform(Change&& change)
    {
        for (auto& caret: carets)
            change(caret);

        normalize();
    }

    // The same, for the one gesture that means a single cursor even when there
    // are several: a drag, or a Shift+click, extends the cursor it started
    // from and leaves the others where they are.
    template <typename Change>
    void transformPrimary(Change&& change)
    {
        change(carets[primaryIndex]);

        normalize();
    }

private:
    // Sorts, then merges what overlaps, keeping track of which entry the
    // primary ended up inside.
    void normalize();

    eacp::Vector<Cursor> carets {Cursor {}};

    int primaryIndex = 0;
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
