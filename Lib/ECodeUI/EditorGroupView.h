#pragma once

#include "Chrome.h"
#include "EditorWidget.h"

#include <optional>
#include <string>

namespace ecode
{
// The name a file goes by in a tab or a window title. Untitled is what a buffer
// with no path is called everywhere, and it has to be called something.
std::string displayName(const TextFile& file);

// One editor group: the strip of tabs over the file it is showing, and the text
// under it.
//
// **Its own TextRenderer, and therefore its own RowCache.** PLAN.md §7.8 left
// this note for whoever added a second visible editor, and it is the one thing
// about groups that could not be discovered by looking: the cache is a window of
// laid-out rows keyed by row *index*, stamped with the document revision it came
// from. Two groups drawing different files through one cache would each find the
// stamp wrong on every frame, throw the whole thing away, and lay out every
// visible row again — which draws exactly the same picture as a cache that
// works. The only evidence would be the clock, so the coupling is written down
// instead of measured (§9).
//
// A group is a Widget rather than a rectangle the window draws into, because
// everything in it is already one: the strip has hover states and a scroll
// offset, and the editor has focus, capture and a caret. What the window keeps
// is the arrangement — where the seams between groups are — and that is exactly
// the split Splitter's header describes.
class EditorGroupView final : public Widget
{
public:
    EditorGroupView(const ChromeTheme& chromeTheme, Workspace& workspaceToShow);

    Workspace& workspace() { return *files; }
    TabBar& tabBar() { return tabs; }
    EditorWidget& editor() { return text; }

    // Null until the atlas has been built, which cannot happen until the view is
    // on a display and its scale is known — and again whenever the display
    // changes, since glyphs cached for the old one are the wrong size. Each
    // group builds its own from the shared atlas.
    void setAtlas(eacp::Text::GlyphAtlas* atlasToUse,
                  const TextTheme& textTheme,
                  float scale);

    // Points the editor at whatever this group's workspace made active and
    // refills the strip. Cheap enough to call on every keystroke: the strip and
    // the editor both compare before they store.
    void refresh();

    // Whether this is the group being worked in. Said on the tab strip because
    // the caret is the only other thing that says so, and a caret is one pixel
    // column that spends half its time invisible.
    void setGroupActive(bool isActive);
    bool isGroupActive() const { return groupActive; }

    // The text's own rectangle, which is what an overlay positions itself over.
    // The strip above it is not part of the document.
    const eacp::Graphics::Rect& editorArea() const { return text.bounds(); }

    // Null before the atlas exists. Public for the reason RowCache's counters
    // are public: "each pane keeps its own laid-out rows" is a claim about work
    // that was *not* done, and a cache thrashing between two panes draws
    // pixel-for-pixel what one that never thrashes draws. Only the counters can
    // tell the two apart.
    const TextRenderer* textRenderer() const
    {
        return renderer ? &renderer.value() : nullptr;
    }

    void layout() override;

    static constexpr auto tabBarHeight = 35.f;

private:
    // Never null. A group is a workspace and there is always one.
    Workspace* files;

    TabBar tabs;
    EditorWidget text;

    std::optional<TextRenderer> renderer;

    bool groupActive = true;
};
} // namespace ecode
