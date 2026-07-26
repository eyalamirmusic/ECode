#pragma once

#include "Theme.h"
#include "Widget.h"

#include <functional>
#include <string>

namespace ecode
{
// A flat block of colour. The activity bar and the sidebar are this and nothing
// else until there is a tree to put in them.
class Panel final : public Widget
{
public:
    explicit Panel(const eacp::Graphics::Color& colourToUse)
        : colour(colourToUse)
    {
    }

    void setColour(const eacp::Graphics::Color& newColour) { colour = newColour; }

    void paint(PaintContext& context) override;

private:
    eacp::Graphics::Color colour;
};

// One open file's entry in the tab strip.
struct TabItem
{
    std::string title;

    // Unsaved edits, and a save refused because the file moved underneath us.
    // Two states rather than one because they mean opposite things to the
    // person looking at them: one is work to save, the other a question to
    // answer.
    bool modified = false;
    bool conflicted = false;
};

// The strip of open files.
//
// Tabs share the width available, shrinking from a comfortable width down to a
// floor below which a filename stops being readable at all. Past that floor the
// strip is wider than its own bounds and carries a horizontal offset, kept so
// the active tab is always on screen — the alternative, shrinking without
// limit, turns twenty open files into twenty slivers naming none of them.
class TabBar final : public Widget
{
public:
    explicit TabBar(const ChromeTheme& themeToUse)
        : theme(themeToUse)
    {
    }

    void setTabs(eacp::Vector<TabItem> newTabs);
    void setActiveTab(int index);
    int activeTab() const { return active; }
    int tabCount() const { return tabs.size(); }

    std::function<void(int)> onTabSelected = [](int) {};
    std::function<void(int)> onTabClosed = [](int) {};

    bool wantsMouse() const override { return true; }

    void layout() override;

    void prepare(eacp::Text::GlyphAtlas& atlas,
                 const eacp::Graphics::Rect& visible) override;
    void paint(PaintContext& context) override;

    void mouseDown(const eacp::Graphics::MouseEvent& event) override;
    void mouseUp(const eacp::Graphics::MouseEvent& event) override;
    void mouseMoved(const eacp::Graphics::MouseEvent& event) override;
    void mouseExited() override;
    bool mouseWheel(const eacp::Graphics::MouseEvent& event) override;

    // Where a tab and its close button sit. Public because the render tests
    // have to click them, and a test computing the geometry itself would pass
    // against a strip that lays out somewhere else entirely.
    eacp::Graphics::Rect boundsOfTab(int index) const;
    eacp::Graphics::Rect closeBoundsOfTab(int index) const;

    int tabAt(const eacp::Graphics::Point& point) const;

    // The tab under the pointer, or -1. Public for the reason
    // WidgetHost::hovered is: a hover that is never cleared draws identically
    // to one that is, until the pointer is somewhere the eye can check.
    int hoveredTab() const { return hovered; }

private:
    // Shared evenly, between a comfortable width and a floor.
    float tabWidth() const;

    void clampOffset();

    // Brings the active tab fully into view, which is the only thing that makes
    // an overflowing strip usable from the keyboard.
    void scrollToActive();

    void setHover(int tab, bool onClose);

    const ChromeTheme& theme;

    eacp::Vector<TabItem> tabs;
    int active = 0;

    // How far the strip is scrolled left, in points. Zero unless the tabs are
    // wider than the room for them.
    float offsetX = 0.f;

    int hovered = -1;
    bool hoveredClose = false;

    // The tab a press armed for closing, and whether the press was a middle
    // click rather than a hit on the ×.
    //
    // The release decides, not the press — the same rule the context menu
    // follows, and what lets a press that landed on the wrong × be backed out
    // of by dragging off it before letting go.
    int armedClose = -1;
    bool armedByMiddleButton = false;
};

// The bar along the bottom. Two runs of text, one against each end.
class StatusBar final : public Widget
{
public:
    explicit StatusBar(const ChromeTheme& themeToUse)
        : theme(themeToUse)
    {
    }

    void setText(std::string left, std::string right);

    void prepare(eacp::Text::GlyphAtlas& atlas,
                 const eacp::Graphics::Rect& visible) override;
    void paint(PaintContext& context) override;

private:
    const ChromeTheme& theme;

    std::string leftText;
    std::string rightText;
};
} // namespace ecode
