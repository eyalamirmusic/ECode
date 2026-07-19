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
// Written as a list holding one item rather than as a single hardcoded tab.
// There is one file today, but every part of this — the per-tab rect, the hit
// test, the active index — is the same code for one tab or ten, and the version
// that assumes one has to be thrown away rather than extended.
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

    std::function<void(int)> onTabSelected = [](int) {};

    bool wantsMouse() const override { return true; }

    void prepare(eacp::Text::GlyphAtlas& atlas,
                 const eacp::Graphics::Rect& visible) override;
    void paint(PaintContext& context) override;
    void mouseDown(const eacp::Graphics::MouseEvent& event) override;

private:
    eacp::Graphics::Rect boundsOfTab(int index) const;
    int tabAt(const eacp::Graphics::Point& point) const;

    const ChromeTheme& theme;

    eacp::Vector<TabItem> tabs;
    int active = 0;
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
