#include "EditorWidget.h"

#include <algorithm>

namespace ecode
{
using namespace eacp;

void EditorWidget::setRenderer(TextRenderer* rendererToUse)
{
    renderer = rendererToUse;

    // A new renderer means new metrics, so the offset that was in range for the
    // old line height may not be for this one.
    clampScroll();
    repaint();
}

void EditorWidget::layout()
{
    // A window made taller can leave the document scrolled further than there
    // is now content to justify.
    clampScroll();
}

void EditorWidget::clampScroll()
{
    if (renderer == nullptr)
        return;

    const auto content = renderer->contentHeight(document());

    // Stop at the last line rather than letting the document scroll off the
    // top, but never push a short document around.
    const auto lowest = std::min(0.f, bounds().h - content);

    scrollY = std::clamp(scrollY, lowest, 0.f);
}

void EditorWidget::scrollToLine(std::size_t line)
{
    if (renderer == nullptr)
        return;

    const auto lineHeight = renderer->lineHeight();
    const auto top = static_cast<float>(line) * lineHeight;

    // Already on screen: leave the view alone. Re-centring on every hit would
    // scroll the file out from under a match that was perfectly visible, and
    // ⌘G down a screenful of hits would judder rather than step.
    if (top + scrollY >= 0.f && top + lineHeight + scrollY <= bounds().h)
        return;

    // Otherwise centre it rather than bringing it just inside the edge. A hit
    // revealed by the smallest possible scroll lands hard against the top or
    // bottom with no context on the side it arrived from.
    scrollY = -top + (bounds().h - lineHeight) * 0.5f;

    clampScroll();
}

void EditorWidget::scrollToCaret()
{
    if (renderer == nullptr)
        return;

    const auto line = document().lineAt(editor().cursor().head);
    const auto lineHeight = renderer->lineHeight();

    const auto top = static_cast<float>(line) * lineHeight;
    const auto bottom = top + lineHeight;

    // Only move when the caret has actually left the viewport, so typing in the
    // middle of the screen does not drag the view around.
    if (top + scrollY < 0.f)
        scrollY = -top;
    else if (bottom + scrollY > bounds().h)
        scrollY = bounds().h - bottom;

    clampScroll();
}

int EditorWidget::visibleLines() const
{
    if (renderer == nullptr || renderer->lineHeight() <= 0.f)
        return 1;

    return std::max(1, static_cast<int>(bounds().h / renderer->lineHeight()) - 1);
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

    scrollToLine(document().lineAt(match->start));

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

    renderer->prepare(document(), bounds(), scrollY);

    // Highlight exactly the lines about to be drawn: tree-sitter parses the
    // whole file, but querying all of it would put scrolling cost back in
    // proportion to file size.
    if (highlighter != nullptr)
        highlighter->update(
            document(),
            renderer->firstVisibleLine(scrollY),
            renderer->lastVisibleLine(document(), bounds(), scrollY));
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

    renderer->draw(context, document(), overlay, highlighter, bounds(), scrollY);
}

void EditorWidget::mouseDown(const Graphics::MouseEvent& event)
{
    if (renderer == nullptr)
        return;

    const auto offset =
        renderer->offsetAtPoint(document(), event.pos, bounds(), scrollY);

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
        renderer->offsetAtPoint(document(), event.pos, bounds(), scrollY), true);

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
                            : event.delta.y * renderer->lineHeight() * 3.f;

    scrollY += points;
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
            editor().moveUp(shift, visibleLines());
            break;

        case Graphics::KeyCode::PageDown:
            editor().moveDown(shift, visibleLines());
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
