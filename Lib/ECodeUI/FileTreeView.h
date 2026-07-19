#pragma once

#include "ListView.h"
#include "ScrollView.h"
#include "Theme.h"

#include <ECodeCore/FileTree.h>

#include <functional>

namespace ecode
{
// The sidebar's file tree: a FileTreeModel drawn through a virtualised list,
// inside a scroll view.
//
// The model does the directory work and the list does the virtualisation, so
// what is left here is what a row looks like and what a click means. That split
// is why a tree of five thousand entries is not five thousand widgets — see
// ListView.
class FileTreeView final : public ScrollView
{
public:
    explicit FileTreeView(const ChromeTheme& themeToUse);

    void setRoot(const eacp::FilePath& path);
    void refresh();

    // A file was chosen — a directory click expands instead and never reaches
    // here.
    std::function<void(const eacp::FilePath&)> onFileChosen =
        [](const eacp::FilePath&) {};

    // What the tree is currently showing, read-only. A status readout or a
    // reveal-this-file command wants it, and it is what makes the click
    // wiring testable without a device.
    const FileTreeModel& treeModel() const { return model; }

    // What inside the tree takes the keyboard. A scroll view is not itself a
    // focus stop — what the arrow keys drive is the list inside it — so a
    // focus-the-explorer command needs to be pointed at that rather than here.
    Widget& keyboardTarget() { return list; }

private:
    void rowClicked(std::size_t index);
    void paintRow(PaintContext& context,
                  std::size_t index,
                  const eacp::Graphics::Rect& area,
                  bool selected);

    const ChromeTheme& theme;

    FileTreeModel model;
    ListView list;
};
} // namespace ecode
