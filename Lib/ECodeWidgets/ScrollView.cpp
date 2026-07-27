#include "ScrollView.h"

#include <algorithm>

namespace ecode
{
using namespace eacp;

namespace
{
// Below this the thumb stops being grabbable, however long the content is.
constexpr auto minimumThumbHeight = 24.f;

// A notched wheel reports lines rather than points, and a list's line is a row.
constexpr auto linesPerNotch = 3.f;
constexpr auto pointsPerLine = 18.f;
} // namespace

// --- ScrollBar --------------------------------------------------------------

float ScrollBar::maxScroll() const
{
    return std::max(0.f, content - viewport);
}

void ScrollBar::setRange(float contentHeight, float viewportHeight)
{
    content = contentHeight;
    viewport = viewportHeight;

    // Shrinking the content can leave the old position past the new end.
    scrollY = std::clamp(scrollY, 0.f, maxScroll());
}

void ScrollBar::setPosition(float newPosition)
{
    scrollY = std::clamp(newPosition, 0.f, maxScroll());
}

float ScrollBar::thumbTravel() const
{
    const auto height = thumbBounds().h;

    return std::max(0.f, bounds().h - height);
}

Graphics::Rect ScrollBar::thumbBounds() const
{
    const auto track = bounds();

    if (content <= 0.f || viewport <= 0.f)
        return {};

    // Proportional to how much of the content is on screen, which is what makes
    // a scrollbar a size indicator and not just a position one.
    const auto proportion = std::min(1.f, viewport / content);
    const auto height =
        std::max(minimumThumbHeight, std::round(track.h * proportion));

    const auto range = maxScroll();
    const auto travel = std::max(0.f, track.h - height);
    const auto offset = range > 0.f ? travel * (scrollY / range) : 0.f;

    return {track.x, track.y + offset, track.w, height};
}

void ScrollBar::scrollTo(float newPosition)
{
    const auto clamped = std::clamp(newPosition, 0.f, maxScroll());

    if (clamped == scrollY)
        return;

    scrollY = clamped;
    onScrolled(scrollY);
    repaint();
}

void ScrollBar::paint(PaintContext& context)
{
    if (!isNeeded())
        return;

    const auto thumb = thumbBounds();

    if (thumb.isEmpty())
        return;

    context.sprites().fillRect(thumb.inset(2.f, 2.f),
                               dragging ? theme.scrollThumbActive
                                        : theme.scrollThumb);
}

void ScrollBar::mouseDown(const Graphics::MouseEvent& event)
{
    if (!isNeeded())
        return;

    const auto thumb = thumbBounds();

    if (thumb.contains(event.pos))
    {
        dragging = true;
        grabOffset = event.pos.y - thumb.y;
    }
    else
    {
        // A click on bare track puts the thumb's centre under the pointer and
        // then drags from there, so the gesture continues rather than needing a
        // second grab.
        dragging = true;
        grabOffset = thumb.h * 0.5f;

        mouseDragged(event);
    }

    repaint();
}

void ScrollBar::mouseDragged(const Graphics::MouseEvent& event)
{
    if (!dragging)
        return;

    const auto travel = thumbTravel();

    if (travel <= 0.f)
        return;

    // Divided by the travel rather than by the track: the thumb's top can only
    // reach `travel`, so that is what maps onto the whole scroll range.
    const auto offset = event.pos.y - grabOffset - bounds().y;

    scrollTo(offset / travel * maxScroll());
}

void ScrollBar::mouseUp(const Graphics::MouseEvent&)
{
    dragging = false;
    repaint();
}

// --- ScrollView -------------------------------------------------------------

void ScrollView::setContent(Widget& contentToShow)
{
    content = &contentToShow;

    // Ahead of the bar in the child list, so the bar paints last and is hit
    // first. widgetAt walks children back to front for exactly this reason.
    removeAllChildren();
    addChild(contentToShow);
    addChild(bar);

    layout();
}

float ScrollView::contentHeight() const
{
    return content != nullptr ? content->preferredHeight(bounds().w) : 0.f;
}

float ScrollView::maxScroll() const
{
    return std::max(0.f, contentHeight() - viewportHeight());
}

void ScrollView::layout()
{
    auto area = bounds();

    const auto height = contentHeight();
    const auto needsBar = height > area.h;

    bar.setVisible(needsBar);

    if (needsBar)
        bar.setBounds(area.removeFromRight(scrollBarWidth));

    position = std::clamp(position, 0.f, std::max(0.f, height - bounds().h));

    bar.setRange(height, bounds().h);
    bar.setPosition(position);

    if (content == nullptr)
        return;

    // Positioned above its parent by the scroll offset and taller than it; the
    // clip in paintTree is what turns that into scrolling.
    content->setBounds({area.x, area.y - position, area.w, std::max(height, area.h)});
}

void ScrollView::setScrollPosition(float y)
{
    const auto clamped = std::clamp(y, 0.f, maxScroll());

    if (clamped == position)
        return;

    position = clamped;

    layout();
    repaint();
}

void ScrollView::scrollToShow(float top, float height)
{
    if (top < position)
        setScrollPosition(top);
    else if (top + height > position + viewportHeight())
        setScrollPosition(top + height - viewportHeight());
}

bool ScrollView::mouseWheel(const Graphics::MouseEvent& event)
{
    if (maxScroll() <= 0.f)
        return false;

    // A trackpad reports points, a notched wheel reports lines. Positive y
    // means the content should move down, which is a *decrease* in how far
    // down the content we are.
    const auto points = event.preciseScrolling
                            ? event.delta.y
                            : event.delta.y * pointsPerLine * linesPerNotch;

    setScrollPosition(position - points);

    return true;
}
} // namespace ecode
