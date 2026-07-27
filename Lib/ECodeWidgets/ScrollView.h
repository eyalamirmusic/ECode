#pragma once

#include "Theme.h"
#include "Widget.h"

#include <functional>

namespace ecode
{
// A vertical scrollbar.
//
// Speaks in content coordinates, not thumb pixels: the owner sets a range and a
// position and is told a new position, and the conversion to and from the
// thumb's travel lives here. Getting that conversion wrong in both directions
// is how a scrollbar ends up almost working — the thumb tracks the pointer but
// the content lags, because one side divided by the track and the other by the
// travel.
class ScrollBar final : public Widget
{
public:
    explicit ScrollBar(const ChromeTheme& themeToUse)
        : theme(themeToUse)
    {
    }

    void setRange(float contentHeight, float viewportHeight);
    void setPosition(float scrollY);

    float position() const { return scrollY; }
    float maxScroll() const;

    // True when the content is taller than the viewport. A bar that cannot
    // move is worse than no bar: it says "there is more" when there is not.
    bool isNeeded() const { return maxScroll() > 0.f; }

    std::function<void(float)> onScrolled = [](float) {};

    bool wantsMouse() const override { return true; }

    void paint(PaintContext& context) override;
    void mouseDown(const eacp::Graphics::MouseEvent& event) override;
    void mouseDragged(const eacp::Graphics::MouseEvent& event) override;
    void mouseUp(const eacp::Graphics::MouseEvent& event) override;

    eacp::Graphics::Rect thumbBounds() const;

private:
    void scrollTo(float newPosition);

    // Where along the track the thumb's top sits for a given position, and the
    // inverse. The pair the class exists to keep consistent.
    float thumbTravel() const;

    const ChromeTheme& theme;

    float content = 0.f;
    float viewport = 0.f;
    float scrollY = 0.f;

    // Where inside the thumb the drag started, so it does not jump to centre
    // itself on the pointer the moment it is grabbed.
    float grabOffset = 0.f;
    bool dragging = false;
};

// A viewport onto a taller widget.
//
// The content is laid out at its full preferred height and offset upward by
// the scroll position; this widget's own bounds are what clips it, through the
// same ClipScope every widget gets in paintTree. That is the whole mechanism —
// there is no separate scrolling code path, only a child positioned outside its
// parent and clipped back to it.
class ScrollView : public Widget
{
public:
    explicit ScrollView(const ChromeTheme& themeToUse)
        : bar(themeToUse)
    {
        addChild(bar);

        bar.onScrolled = [this](float y) { setScrollPosition(y); };
    }

    // The content must outlive this view, and is added as a child. Added
    // *before* the bar so the bar paints over it and takes clicks first.
    void setContent(Widget& contentToShow);

    void setScrollPosition(float y);
    float scrollPosition() const { return position; }
    float maxScroll() const;

    // Brings a band of content coordinates into view, moving as little as
    // possible — the same rule the editor's caret follows.
    void scrollToShow(float top, float height);

    void layout() override;

    bool wantsMouse() const override { return true; }
    bool mouseWheel(const eacp::Graphics::MouseEvent& event) override;

private:
    // Width taken from the content only when the bar is actually needed.
    static constexpr auto scrollBarWidth = 10.f;

    float viewportHeight() const { return bounds().h; }
    float contentHeight() const;

    Widget* content = nullptr;
    ScrollBar bar;

    float position = 0.f;
};
} // namespace ecode
