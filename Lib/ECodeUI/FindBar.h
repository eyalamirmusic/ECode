#pragma once

#include "TextField.h"
#include "Theme.h"
#include "Widget.h"

#include <ECodeCore/Search.h>

#include <functional>
#include <string>

namespace ecode
{
// Find and replace: two fields, the options that change what counts as a match,
// and the buttons that move through the results.
//
// Pure UI. It holds no matches and does not know what a Document is — the search
// itself lives in EditorWidget, which already owns the text to search, the
// scroll offset that brings a hit into view, and the renderer that draws the
// hits. This is the query and the buttons, and it reports both.
//
// Unlike the command palette it is *not* an overlay covering the window. It sits
// against the top-right of the editor, the file stays clickable underneath it,
// and it stays open while the person works — so a click outside must not dismiss
// it, and it takes no backdrop.
class FindBar final : public Widget
{
public:
    explicit FindBar(const ChromeTheme& themeToUse);

    // `initialQuery` seeds the find field and is selected rather than appended
    // to, so ⌘F with something already in the box offers it and still lets the
    // next keystroke start a new search. Empty leaves whatever was there.
    void show(const std::string& initialQuery, bool withReplace);
    void hide();

    bool isOpen() const { return isVisible(); }
    bool isReplaceShown() const { return replaceShown; }

    // What the owner should focus after show(). Always the find field, including
    // when the replace row is up — ⌥⌘F is still a search, and Tab reaches the
    // other field.
    Widget& keyboardTarget() { return findField; }

    const SearchQuery& query() const { return currentQuery; }
    const std::string& replacement() const { return replaceField.text(); }

    // What the counter reads. Set by the owner after each search, because the
    // bar cannot count what it does not hold.
    void setMatchCount(int current, int total);

    // Fired whenever the query text or either option changes — that is, whenever
    // what counts as a match has changed and the highlighting is now stale.
    std::function<void()> onQueryChanged = [] {};

    std::function<void()> onFindNext = [] {};
    std::function<void()> onFindPrevious = [] {};
    std::function<void()> onReplace = [] {};
    std::function<void()> onReplaceAll = [] {};

    // Fired after the bar closes, however it closed, so the owner can put focus
    // back in the editor. The bar deliberately does not know about the host.
    std::function<void()> onClosed = [] {};

    // Tab moves between the two fields, and focus belongs to the host rather
    // than to any widget — so the bar asks instead of setting it.
    std::function<void(Widget&)> onFocusRequested = [](Widget&) {};

    // The size this bar needs. Bounds are assigned by parents in this tree, so a
    // widget that knows its own size publishes it rather than taking it: the
    // owner reads these two and hands down a rect.
    //
    // The height changes when the replace row appears, which is why show() asks
    // its parent to lay out again.
    float barHeight() const;
    float barWidth() const;

    void layout() override;

    bool wantsMouse() const override { return true; }

    void prepare(eacp::Text::GlyphAtlas& atlas,
                 const eacp::Graphics::Rect& visible) override;
    void paint(PaintContext& context) override;

    void mouseDown(const eacp::Graphics::MouseEvent& event) override;
    bool keyDown(const eacp::Graphics::KeyEvent& event) override;

private:
    // A clickable region drawn as a label.
    //
    // Not a Button widget: there is no such thing yet, nothing tracks the
    // pointer so there are no hover states to give one, and six labels that
    // toggle or fire is not enough to design a widget around. When hover lands,
    // this is the first thing that should become one.
    enum class Control
    {
        none,
        caseSensitive,
        wholeWord,
        previous,
        next,
        close,
        replaceOne,
        replaceEvery,
    };

    struct Hotspot
    {
        Control control = Control::none;
        eacp::Graphics::Rect area;
        std::string label;

        // Toggles draw lit when on; the rest draw the same way always.
        bool isToggle = false;
        bool isOn = false;
    };

    void rebuildHotspots();
    void activate(Control control);
    void queryChanged();

    Control controlAt(const eacp::Graphics::Point& point) const;

    eacp::Graphics::Rect findRowBounds() const;
    eacp::Graphics::Rect replaceRowBounds() const;

    const ChromeTheme& theme;

    TextField findField;
    TextField replaceField;

    SearchQuery currentQuery;

    eacp::Vector<Hotspot> hotspots;

    std::string counterText;

    bool replaceShown = false;

    // Set when the query found nothing, so the counter can say so in the colour
    // that means it rather than only in words.
    bool foundNothing = false;
};
} // namespace ecode
