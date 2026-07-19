#pragma once

#include <ECodeRender/PaintContext.h>

#include <eacp/Core/Core.h>
#include <eacp/Graphics/Graphics.h>

#include <functional>

namespace ecode
{
// A rectangle in the widget tree that can draw itself, lay out children and
// take input.
//
// The tree lives inside one GPU::GPUView rather than being one Graphics::View
// per widget — a file tree with 5,000 rows would otherwise be 5,000 NSViews,
// and chrome composited by CoreAnimation cannot share a z-order with text drawn
// by Metal. Nothing here touches the platform, so layout, hit-testing and focus
// are all testable without a device.
class Widget
{
public:
    Widget() = default;
    virtual ~Widget() = default;

    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    // --- tree ------------------------------------------------------------

    // For widgets that are members of a composite, which is how the chrome is
    // written: the parent owns them by construction and this only records the
    // relationship. The child must outlive the parent.
    void addChild(Widget& child);

    // For widgets whose number is not known until runtime — tree rows, tabs.
    // Returns a reference so the caller can keep using it after handing over.
    Widget& addChild(eacp::OwningPointer<Widget> child);

    void removeAllChildren();

    const eacp::Vector<Widget*>& children() const { return childList; }
    Widget* parent() const { return parentWidget; }

    // --- geometry --------------------------------------------------------

    // Bounds are absolute, in the host's coordinate space, and are assigned by
    // the parent's layout(). There is deliberately no transform stack: a parent
    // splits its own rect with Rect::removeFrom* and hands the pieces down.
    //
    // That is what the GPU wants — a scissor rect is absolute and there is only
    // one — and it makes hit-testing a plain contains() rather than a walk back
    // up the tree accumulating offsets. The cost is that moving a widget
    // relays out its subtree, which is nothing at the scale of IDE chrome.
    void setBounds(const eacp::Graphics::Rect& newBounds);
    const eacp::Graphics::Rect& bounds() const { return area; }

    // Position children within bounds(). Called whenever the bounds change.
    virtual void layout() {}

    // Natural height for a given width, for a container that lays out by
    // content rather than by division — a scroll view sizing its range, a list
    // sizing itself to its rows. Defaults to the height it already has, which
    // is the right answer for anything positioned by its parent.
    virtual float preferredHeight(float width) const;

    // --- painting --------------------------------------------------------

    // Rasterizes every glyph this widget's next paint will need.
    //
    // The atlas uploads once between this walk and the paint walk: a glyph
    // first touched during paint would mutate a texture the earlier draws in
    // the pass have already bound, and the symptom is text from the previous
    // frame appearing in this one. A widget that draws no text needs nothing
    // here.
    // `visible` is this widget's bounds already intersected with every
    // ancestor's, so a virtualised list rasterizes the rows it will actually
    // draw rather than all of them. It is the same rectangle paint() will see
    // as its clip, and the two agreeing is what the prepass depends on.
    virtual void prepare(eacp::Text::GlyphAtlas&,
                         const eacp::Graphics::Rect& visible)
    {
        (void) visible;
    }

    virtual void paint(PaintContext&) {}

    // Walks the subtree. Each widget is clipped to its own bounds before it
    // paints, so a child cannot draw outside the widget containing it and a
    // long line stops at the viewport edge rather than under the chrome.
    void paintTree(PaintContext& context);
    void prepareTree(eacp::Text::GlyphAtlas& atlas,
                     const eacp::Graphics::Rect& clip);

    // --- input -----------------------------------------------------------

    // Widgets are transparent to the mouse unless they say otherwise, so a
    // decorative panel does not swallow clicks meant for something beneath it.
    virtual bool wantsMouse() const { return false; }
    virtual bool acceptsFocus() const { return false; }

    // True for widgets that are a text box in their own right.
    //
    // The application asks before deciding what ⌘A, ⌘C and ⌘V mean: with a find
    // field focused they belong to the field, and everywhere else they belong to
    // the document. That is the job a keymap `when` clause does in VSCode, and
    // this is the one distinction that bites hard enough to be worth a virtual
    // before contexts exist — pasting a search term into the file being searched
    // is a mistake that edits the document.
    virtual bool isTextInput() const { return false; }

    virtual void mouseDown(const eacp::Graphics::MouseEvent&) {}
    virtual void mouseDragged(const eacp::Graphics::MouseEvent&) {}
    virtual void mouseUp(const eacp::Graphics::MouseEvent&) {}
    virtual void mouseMoved(const eacp::Graphics::MouseEvent&) {}

    // True when the widget consumed the scroll. Returning false hands it to the
    // nearest ancestor that will take it, so a list inside a scrolling panel
    // passes on what it cannot use itself.
    virtual bool mouseWheel(const eacp::Graphics::MouseEvent&) { return false; }

    // True when the key was consumed. Returning false bubbles to the parent and
    // finally back to the host's caller, which is what lets an unhandled key
    // fall through to application shortcuts.
    virtual bool keyDown(const eacp::Graphics::KeyEvent&) { return false; }

    virtual void focusGained() {}
    virtual void focusLost() {}

    void setVisible(bool shouldBeVisible);
    bool isVisible() const { return visible; }

    // Deepest visible descendant wanting the mouse at this point, or null.
    // Later children win, since they are painted last and so are on top.
    Widget* widgetAt(const eacp::Graphics::Point& point);

    // Asks for another frame. Walks to the root, which is where the host has
    // attached the callback that reaches GPUView::repaint().
    void repaint();

    // Set by the host on the root widget. Non-null by default so repaint() can
    // call it without a check.
    std::function<void()> onRepaintNeeded = [] {};

private:
    eacp::Vector<Widget*> childList;
    eacp::Vector<eacp::OwningPointer<Widget>> ownedChildren;

    Widget* parentWidget = nullptr;
    eacp::Graphics::Rect area;

    bool visible = true;
};
} // namespace ecode
