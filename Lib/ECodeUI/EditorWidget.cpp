#include "EditorWidget.h"

#include <algorithm>

namespace ecode
{
using namespace eacp;

DocumentView EditorWidget::documentView() const
{
    return {document(), editor().lineMap(), highlighter()};
}

void EditorWidget::setRenderer(TextRenderer* rendererToUse)
{
    renderer = rendererToUse;

    // A new renderer means new metrics: a different advance changes how many
    // columns fit, and a different row height changes what offsets are in range.
    updateWrapWidth();
    clampScroll();
    repaint();
}

void EditorWidget::setFile(OpenFile& fileToEdit)
{
    if (open == &fileToEdit)
        return;

    open = &fileToEdit;

    // The incoming file's line map was last given the wrap width of whatever
    // viewport it was in, which is not necessarily this one.
    updateWrapWidth();

    // The match list describes the file that was open a moment ago, and the
    // usual staleness check cannot see that: Editor::version counts per editor
    // and starts at zero in each, so the incoming file's count can equal the
    // outgoing file's and the stale list would survive comparison.
    refreshSearch();

    clampScroll();
    repaint();
}

void EditorWidget::setWordWrap(bool shouldWrap)
{
    if (wordWrap == shouldWrap)
        return;

    wordWrap = shouldWrap;

    updateWrapWidth();

    // The caret's row has moved even though its offset has not, so the view has
    // to follow it — turning wrapping on scrolls what was on screen a long way
    // down in a file with long lines.
    scrollToCaret();
    repaint();
}

void EditorWidget::updateWrapWidth()
{
    if (renderer == nullptr)
        return;

    editor().setWrapColumns(
        wordWrap ? renderer->wrapColumnsFor(bounds(), document().lineCount()) : 0);
}

void EditorWidget::layout()
{
    // A narrower window wraps at fewer columns, and a taller one can leave the
    // document scrolled further than there is now content to justify.
    updateWrapWidth();
    clampScroll();
}

void EditorWidget::clampScroll()
{
    if (renderer == nullptr)
        return;

    const auto content = renderer->contentHeight(documentView());

    // Stop at the last row rather than letting the document scroll off the top,
    // but never push a short document around.
    const auto lowest = std::min(0.f, bounds().h - content);

    open->scrollY = std::clamp(open->scrollY, lowest, 0.f);
}

void EditorWidget::scrollToRow(std::size_t row)
{
    if (renderer == nullptr)
        return;

    const auto rowHeight = renderer->rowHeight();
    const auto top = renderer->rowTop(row);

    // Already on screen: leave the view alone. Re-centring on every hit would
    // scroll the file out from under a match that was perfectly visible, and
    // ⌘G down a screenful of hits would judder rather than step.
    if (top + open->scrollY >= 0.f && top + rowHeight + open->scrollY <= bounds().h)
        return;

    // Otherwise centre it rather than bringing it just inside the edge. A hit
    // revealed by the smallest possible scroll lands hard against the top or
    // bottom with no context on the side it arrived from.
    open->scrollY = -top + (bounds().h - rowHeight) * 0.5f;

    clampScroll();
}

void EditorWidget::scrollToCaret()
{
    if (renderer == nullptr)
        return;

    const auto row =
        editor().lineMap().rowOfOffset(document(), editor().cursor().head);
    const auto rowHeight = renderer->rowHeight();

    const auto top = renderer->rowTop(row);
    const auto bottom = top + rowHeight;

    // Only move when the caret has actually left the viewport, so typing in the
    // middle of the screen does not drag the view around.
    if (top + open->scrollY < 0.f)
        open->scrollY = -top;
    else if (bottom + open->scrollY > bounds().h)
        open->scrollY = bounds().h - bottom;

    clampScroll();
}

int EditorWidget::visibleRows() const
{
    if (renderer == nullptr || renderer->rowHeight() <= 0.f)
        return 1;

    return std::max(1, static_cast<int>(bounds().h / renderer->rowHeight()) - 1);
}

void EditorWidget::wake()
{
    caretVisible = true;
    blinkPhase = 0;

    scrollToCaret();
    onStateChanged();
    repaint();
}

bool EditorWidget::tickCaretBlink()
{
    // An editor nobody is typing into shows no caret, so there is nothing to
    // blink and no reason to ask for a frame.
    if (!caretVisible && blinkPhase == 0)
        return false;

    if (++blinkPhase < 2)
        return false;

    caretVisible = !caretVisible;
    repaint();

    return true;
}

void EditorWidget::focusGained()
{
    caretVisible = true;
    blinkPhase = 0;
    repaint();
}

void EditorWidget::focusLost()
{
    caretVisible = false;
    repaint();
}

void EditorWidget::refreshSearch()
{
    finder.refresh(document());
    searchedVersion = editor().version();
}

void EditorWidget::setSearchQuery(const SearchQuery& query, std::size_t from)
{
    finder.setQuery(query);

    refreshSearch();
    finder.selectAtOrAfter(from);

    // Deliberately does not move the caret. A query still being typed should
    // highlight what it has found without dragging the insertion point across
    // the file on every keystroke — the caret follows only on an explicit find.
    repaint();
}

void EditorWidget::clearSearch()
{
    finder.setQuery({});

    refreshSearch();
    repaint();
}

void EditorWidget::goToCurrentMatch()
{
    const auto* match = finder.currentMatch();

    if (match == nullptr)
        return;

    // Selected rather than merely scrolled to, so that closing the find bar
    // leaves the caret on what was being looked for and ready to be typed over.
    editor().placeCaret(match->start);
    editor().placeCaret(match->end, true);

    caretVisible = true;
    blinkPhase = 0;

    scrollToRow(editor().lineMap().rowOfOffset(document(), match->start));

    onStateChanged();
    repaint();
}

void EditorWidget::findNext()
{
    refreshSearch();

    // From the end of whatever is selected, so a hit that is already selected is
    // stepped over rather than found again.
    finder.selectAtOrAfter(editor().cursor().end());
    goToCurrentMatch();
}

void EditorWidget::findPrevious()
{
    refreshSearch();

    finder.selectBefore(editor().cursor().start());
    goToCurrentMatch();
}

void EditorWidget::replaceCurrent(std::string_view replacement)
{
    const auto* match = finder.currentMatch();

    if (match == nullptr)
        return;

    const auto at = match->start;

    replaceMatch(editor(), *match, replacement);

    refreshSearch();

    // Past the replacement rather than at it. Replacing "a" with "aa" produces
    // text that matches the query again, and resuming at the same offset would
    // find the replacement and replace it forever.
    finder.selectAtOrAfter(at + replacement.size());

    goToCurrentMatch();
}

int EditorWidget::replaceAllMatches(std::string_view replacement)
{
    const auto replaced = replaceAll(editor(), finder.query(), replacement);

    if (replaced > 0)
    {
        refreshSearch();

        onStateChanged();
        repaint();
    }

    return replaced;
}

std::size_t EditorWidget::caretLine() const
{
    return document().lineAt(editor().cursor().head) + 1;
}

std::size_t EditorWidget::caretColumn() const
{
    return document().columnAt(editor().cursor().head) + 1;
}

// The atlas goes unused because TextRenderer holds its own reference to the
// same one; the parameter is what every other widget needs.
void EditorWidget::prepare(Text::GlyphAtlas&, const Graphics::Rect&)
{
    if (renderer == nullptr)
        return;

    // The match list describes text as it was when the search last ran, so an
    // edit anywhere invalidates it — typing, undo, or a reload from disk.
    //
    // Checked here, once a frame, rather than on every keystroke: a scan is
    // linear in the file and doing one per key would be felt on a large one.
    // The empty-query case costs nothing, so an editor nobody is searching in
    // pays nothing for this.
    if (!finder.query().isEmpty() && searchedVersion != editor().version())
        refreshSearch();

    // Here as well as in layout(), because the width also moves with the
    // *document*: the gutter widens the first time a file passes nine lines, and
    // the text area loses a column to it. Costs nothing on the frames where
    // nothing changed — LineMap ignores a width it already has.
    updateWrapWidth();

    const auto view = documentView();

    // Highlight exactly the lines about to be drawn: tree-sitter parses the
    // whole file, but querying all of it would put scrolling cost back in
    // proportion to file size.
    //
    // Lines, not rows — a highlighter works in the document's own coordinates
    // and knows nothing about wrapping — so the visible band is converted back
    // through the map. The last visible row is exclusive and the last line has
    // to be inclusive of it, which is the +1.
    //
    // Before the glyph prepass, not after: the renderer keeps each row's
    // laid-out glyphs and colours, and a reparse changes the colours. Asking
    // afterwards would have the prepass decide a row needs nothing while the
    // draw that follows it finds the row stale and lays it out again — with the
    // atlas already uploaded, so a glyph first needed by the new colouring
    // would be sampled from texels not yet on the GPU.
    if (highlighter() != nullptr)
    {
        const auto& lines = editor().lineMap();

        const auto first = renderer->firstVisibleRow(open->scrollY);
        const auto last = renderer->lastVisibleRow(view, bounds(), open->scrollY);

        highlighter()->update(document(),
                              lines.lineOfRow(document(), first),
                              last > first
                                  ? lines.lineOfRow(document(), last - 1) + 1
                                  : lines.lineOfRow(document(), first));

        // A highlighter is allowed to run out of time and leave the rest for the
        // next frame, which is what keeps opening a large file from waiting on
        // its parse. Nothing else would ask again: the app draws on demand, so
        // without this the file would sit uncoloured until something unrelated —
        // a caret blink, a keystroke — happened to request a frame.
        if (highlighter()->hasPendingWork())
            repaint();
    }

    renderer->prepare(view, bounds(), open->scrollY);
}

void EditorWidget::paint(PaintContext& context)
{
    if (renderer == nullptr)
        return;

    auto overlay = EditorOverlay {};
    overlay.cursor = &editor().cursor();
    overlay.caretVisible = caretVisible;
    overlay.matches = &finder.matches();
    overlay.currentMatch = finder.currentIndex();

    renderer->draw(context, documentView(), overlay, bounds(), open->scrollY);
}

void EditorWidget::mouseDown(const Graphics::MouseEvent& event)
{
    if (renderer == nullptr)
        return;

    const auto offset =
        renderer->offsetAtPoint(documentView(), event.pos, bounds(), open->scrollY);

    if (event.button == Graphics::MouseButton::Right)
    {
        // A right-click inside the selection leaves it alone, because the menu
        // that follows is about to act on it and collapsing it first would mean
        // Copy quietly copied nothing. Outside it, the caret moves first, so
        // the menu refers to where the person actually pointed.
        if (!isInsideSelection(offset))
            editor().placeCaret(offset, false);

        wake();
        onContextMenuRequested(event.pos);

        return;
    }

    if (event.clickCount >= 3)
        editor().selectLineAt(offset);
    else if (event.clickCount == 2)
        editor().selectWordAt(offset);
    else
        editor().placeCaret(offset, event.modifiers.shift);

    wake();
}

void EditorWidget::mouseDragged(const Graphics::MouseEvent& event)
{
    if (renderer == nullptr)
        return;

    // Always an extension: the anchor was set on mouse-down.
    editor().placeCaret(
        renderer->offsetAtPoint(documentView(), event.pos, bounds(), open->scrollY),
        true);

    wake();
}

bool EditorWidget::mouseWheel(const Graphics::MouseEvent& event)
{
    if (renderer == nullptr)
        return false;

    // A trackpad reports points; a notched wheel reports lines, and only this
    // widget knows how tall a line is.
    const auto points = event.preciseScrolling
                            ? event.delta.y
                            : event.delta.y * renderer->rowHeight() * 3.f;

    open->scrollY += points;
    clampScroll();
    repaint();

    return true;
}

bool EditorWidget::keyDown(const Graphics::KeyEvent& event)
{
    const auto shift = event.modifiers.shift;
    const auto word = event.modifiers.alt;

    // Command chords are the application's, not the editor's: they are the same
    // keys whatever has focus, so they are matched above the widget tree.
    if (event.modifiers.command)
        return false;

    switch (event.keyCode)
    {
        case Graphics::KeyCode::LeftArrow:
            word ? editor().moveWordLeft(shift) : editor().moveLeft(shift);
            break;

        case Graphics::KeyCode::RightArrow:
            word ? editor().moveWordRight(shift) : editor().moveRight(shift);
            break;

        case Graphics::KeyCode::UpArrow:
            editor().moveUp(shift);
            break;

        case Graphics::KeyCode::DownArrow:
            editor().moveDown(shift);
            break;

        case Graphics::KeyCode::Home:
            editor().moveToLineStart(shift);
            break;

        case Graphics::KeyCode::End:
            editor().moveToLineEnd(shift);
            break;

        case Graphics::KeyCode::PageUp:
            editor().moveUp(shift, visibleRows());
            break;

        case Graphics::KeyCode::PageDown:
            editor().moveDown(shift, visibleRows());
            break;

        case Graphics::KeyCode::Delete:
            word ? editor().deleteWordBefore() : editor().backspace();
            break;

        case Graphics::KeyCode::ForwardDelete:
            word ? editor().deleteWordAfter() : editor().deleteForward();
            break;

        case Graphics::KeyCode::Return:
            editor().insert("\n");
            break;

        case Graphics::KeyCode::Tab:
            editor().insert("    ");
            break;

        default:
            // Control characters would be inserted literally and rasterize as
            // boxes; `characters` carries the resolved text for everything
            // else, including dead-key composition.
            if (event.characters.empty() || event.modifiers.control
                || static_cast<unsigned char>(event.characters[0]) < 0x20)
                return false;

            editor().insert(event.characters);
            break;
    }

    wake();

    return true;
}
} // namespace ecode
