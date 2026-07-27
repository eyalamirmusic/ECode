#include "EditorGroupView.h"

namespace ecode
{
using namespace eacp;

std::string displayName(const TextFile& file)
{
    auto name = file.name();

    return name.empty() ? "Untitled" : name;
}

EditorGroupView::EditorGroupView(const ChromeTheme& chromeTheme,
                                 Workspace& workspaceToShow)
    : files(&workspaceToShow)
    , tabs(chromeTheme)
    , text(workspaceToShow.active())
{
    addChild(tabs);
    addChild(text);
}

void EditorGroupView::setAtlas(Text::GlyphAtlas* atlasToUse,
                               const TextTheme& textTheme,
                               float scale)
{
    // Asked before the old renderer goes, because it is the only thing that can
    // turn the scroll offset back into a line and a column. An atlas arrives
    // here at a new font size as well as on a new display, and both change the
    // row height and the column width the offset was measured against. See
    // EditorWidget::topVisibleLine.
    const auto topLine = text.topVisibleLine();
    const auto leftColumn = text.leftVisibleColumn();

    // Let go of the old renderer through the widget before it is destroyed,
    // rather than relying on the two statements being adjacent: emplace() runs
    // the destructor of whatever was there, and the widget would be holding a
    // pointer into it for as long as that took.
    text.setRenderer(nullptr);

    if (atlasToUse == nullptr)
    {
        renderer.reset();
        return;
    }

    renderer.emplace(*atlasToUse, textTheme, scale);
    text.setRenderer(&renderer.value());
    text.scrollToTopLine(topLine);
    text.scrollToLeftColumn(leftColumn);
}

void EditorGroupView::refresh()
{
    text.setFile(files->active());

    auto items = Vector<TabItem> {};

    for (auto index = 0; index < files->count(); ++index)
    {
        const auto& file = files->at(index).file;

        auto item = TabItem {};

        item.title = displayName(file);
        item.modified = file.isDirty();
        item.conflicted = file.isConflicted();

        items.add(std::move(item));
    }

    tabs.setTabs(std::move(items));
    tabs.setActiveTab(files->activeIndex());
}

void EditorGroupView::setGroupActive(bool isActive)
{
    if (groupActive == isActive)
        return;

    groupActive = isActive;

    tabs.setGroupActive(isActive);
}

void EditorGroupView::layout()
{
    auto area = bounds();

    tabs.setBounds(area.removeFromTop(tabBarHeight));
    text.setBounds(area);
}
} // namespace ecode
