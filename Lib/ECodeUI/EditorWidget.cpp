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
void EditorWidget::prepare(Text::GlyphAtlas&)
{
    if (renderer == nullptr)
        return;

    renderer->prepare(document(), bounds(), scrollY);

    // Highlight exactly the lines about to be drawn: tree-sitter parses the
    // whole file, but querying all of it would put scrolling cost back in
    // proportion to file size.
    if (highlighter != nullptr)
        highlighter->update(document(),
                            renderer->firstVisibleLine(scrollY),
                            renderer->lastVisibleLine(document(), bounds(), scrollY));
}

void EditorWidget::paint(PaintContext& context)
{
    if (renderer == nullptr)
        return;

    renderer->draw(context,
                   document(),
                   &editor().cursor(),
                   caretVisible,
                   highlighter,
                   bounds(),
                   scrollY);
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
