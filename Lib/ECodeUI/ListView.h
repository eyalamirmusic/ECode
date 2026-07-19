#pragma once

#include "Widget.h"

#include <functional>

namespace ecode
{
// A list of uniform-height rows, virtualised.
//
// Only the rows meeting the clip are prepared and painted, so a 5,000-entry
// file tree costs the same per frame as a 20-entry one. That property is the
// reason the whole editor is one GPUView with its own widget tree rather than
// an NSView per row, so it is worth stating where it is implemented: the clip
// PaintContext hands to paint() has already been intersected with every
// ancestor's bounds, which makes it exactly the visible band of this list. No
// separate notion of a viewport is needed, and none is kept.
//
// Rows are drawn by a callback rather than by row widgets. Five thousand
// widgets to display five thousand filenames is the cost this design exists to
// avoid, and a row that is a rectangle plus a string does not need identity.
class ListView : public Widget
{
public:
    void setRowCount(std::size_t count);
    std::size_t rowCount() const { return rows; }

    void setRowHeight(float height);
    float rowHeight() const { return height; }

    // -1 for no selection.
    void setSelectedRow(int row);
    int selectedRow() const { return selected; }

    eacp::Graphics::Rect boundsOfRow(std::size_t row) const;

    // The half-open range of rows meeting `area`. Clamped to the row count, so
    // the result is always safe to iterate.
    std::size_t firstRowIn(const eacp::Graphics::Rect& area) const;
    std::size_t lastRowIn(const eacp::Graphics::Rect& area) const;

    // Row at a point, or -1 outside the rows that exist — which includes the
    // empty space below a short list.
    int rowAt(const eacp::Graphics::Point& point) const;

    // What a row looks like is the owner's business; which rows exist and
    // which are worth drawing is this class's.
    std::function<void(PaintContext&, std::size_t row, const eacp::Graphics::Rect&, bool selected)>
        paintRow = [](PaintContext&, std::size_t, const eacp::Graphics::Rect&, bool) {};

    std::function<void(eacp::Text::GlyphAtlas&, std::size_t row)> prepareRow =
        [](eacp::Text::GlyphAtlas&, std::size_t) {};

    std::function<void(std::size_t row, int clickCount)> onRowClicked =
        [](std::size_t, int) {};

    // The selection moved, from a click or from the keyboard. A list inside a
    // ScrollView uses this to keep the selected row on screen: arrowing off the
    // bottom with nothing following is the keyboard equivalent of the caret
    // scrolling out of view. -1 when the selection was cleared.
    std::function<void(int row)> onSelectionChanged = [](int) {};

    // A list inside something that owns the keyboard itself opts out of focus:
    // in the command palette the query field is what has focus and the list
    // only follows it, so a click on a row must not move focus off the owner
    // and leave the next keystroke going nowhere.
    void setFocusable(bool shouldTakeFocus) { focusable = shouldTakeFocus; }

    float preferredHeight(float width) const override;

    bool wantsMouse() const override { return true; }
    bool acceptsFocus() const override { return focusable; }

    void prepare(eacp::Text::GlyphAtlas& atlas,
                 const eacp::Graphics::Rect& visible) override;
    void paint(PaintContext& context) override;

    void mouseDown(const eacp::Graphics::MouseEvent& event) override;
    bool keyDown(const eacp::Graphics::KeyEvent& event) override;

private:
    std::size_t rows = 0;
    float height = 22.f;
    int selected = -1;
    bool focusable = true;
};
} // namespace ecode
