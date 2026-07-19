#include "FileTreeView.h"

#include "UIText.h"

namespace ecode
{
using namespace eacp;

namespace
{
constexpr auto rowHeight = 22.f;
constexpr auto indentPerLevel = 14.f;
constexpr auto leftPadding = 8.f;
constexpr auto markerWidth = 14.f;

// Disclosure markers. Geometric shapes rather than an icon set, since there is
// no icon pipeline yet; a face without them rasterizes nothing and the row
// still reads, because the indentation is what carries the structure.
constexpr auto collapsedMarker = "\xe2\x96\xb8"; // U+25B8, small right triangle
constexpr auto expandedMarker = "\xe2\x96\xbe";  // U+25BE, small down triangle
} // namespace

FileTreeView::FileTreeView(const ChromeTheme& themeToUse)
    : ScrollView(themeToUse)
    , theme(themeToUse)
{
    list.setRowHeight(rowHeight);

    list.paintRow = [this](PaintContext& context,
                           std::size_t index,
                           const Graphics::Rect& area,
                           bool selected) { paintRow(context, index, area, selected); };

    list.prepareRow = [this](Text::GlyphAtlas& atlas, std::size_t index)
    {
        UIText::prepare(atlas, model.row(index).name);
        UIText::prepare(atlas, model.row(index).expanded ? expandedMarker
                                                         : collapsedMarker);
    };

    list.onRowClicked = [this](std::size_t index, int) { rowClicked(index); };

    setContent(list);
}

void FileTreeView::setRoot(const FilePath& path)
{
    model.setRoot(path);
    list.setRowCount(model.rowCount());

    setScrollPosition(0.f);
    layout();
    repaint();
}

void FileTreeView::refresh()
{
    model.refresh();
    list.setRowCount(model.rowCount());

    // Expanding or collapsing changes how tall the content is, so the scroll
    // range has to be recomputed before the next paint reads it.
    layout();
    repaint();
}

void FileTreeView::rowClicked(std::size_t index)
{
    const auto& row = model.row(index);

    if (row.isDirectory)
    {
        model.toggle(index);
        list.setRowCount(model.rowCount());

        layout();
        repaint();

        return;
    }

    onFileChosen(row.path);
}

void FileTreeView::paintRow(PaintContext& context,
                            std::size_t index,
                            const Graphics::Rect& area,
                            bool selected)
{
    const auto& row = model.row(index);

    if (selected)
        context.sprites().fillRect(area, theme.rowSelected);

    auto content = area;

    content.removeFromLeft(leftPadding
                           + static_cast<float>(row.depth) * indentPerLevel);

    const auto baseline = UIText::centredBaseline(context.atlas(), area);
    const auto marker = content.removeFromLeft(markerWidth);

    // Files take the marker's width as indentation rather than drawing one, so
    // names line up with the directory names above them.
    if (row.isDirectory)
        UIText::draw(context,
                     row.expanded ? expandedMarker : collapsedMarker,
                     marker.x,
                     baseline,
                     theme.rowText);

    UIText::draw(context,
                 row.name,
                 content.x,
                 baseline,
                 row.isDirectory ? theme.rowDirectoryText : theme.rowText);
}
} // namespace ecode
