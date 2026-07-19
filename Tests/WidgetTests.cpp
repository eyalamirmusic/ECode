#include <ECodeUI/WidgetHost.h>

#include <NanoTest/NanoTest.h>

// Layout, hit-testing, capture and focus traversal.
//
// None of this touches the GPU, which is the point of keeping WidgetHost
// separate from the view that feeds it: the routing rules are the part most
// likely to be wrong, and they are all plain logic over rectangles.
//
// What is *not* here is anything about what reaches the screen. Clipping and
// paint order are conventions rather than computations, and §9 of PLAN.md is
// the record of what happens when those are tested arithmetically — they
// belong in the render tests, which draw off-screen and read pixels back.

using namespace nano;
using namespace ecode;
using namespace eacp;

namespace
{
// Records what it was asked to do, so a test can assert on routing rather than
// on pixels.
struct ProbeWidget final : Widget
{
    bool wantsMouse() const override { return takesMouse; }
    bool acceptsFocus() const override { return takesFocus; }

    void mouseDown(const Graphics::MouseEvent&) override { ++downs; }
    void mouseDragged(const Graphics::MouseEvent&) override { ++drags; }
    void mouseUp(const Graphics::MouseEvent&) override { ++ups; }

    bool mouseWheel(const Graphics::MouseEvent&) override
    {
        ++wheels;
        return consumesWheel;
    }

    bool keyDown(const Graphics::KeyEvent&) override
    {
        ++keys;
        return consumesKey;
    }

    void focusGained() override { ++focusGains; }
    void focusLost() override { ++focusLosses; }

    bool takesMouse = true;
    bool takesFocus = false;
    bool consumesWheel = false;
    bool consumesKey = false;

    int downs = 0;
    int drags = 0;
    int ups = 0;
    int wheels = 0;
    int keys = 0;
    int focusGains = 0;
    int focusLosses = 0;
};

// Splits its bounds the way the real chrome does, which is the arrangement the
// y-down Rect fix was about: activity bar and sidebar off the left, tab bar off
// the top, status bar off the bottom, editor taking the rest.
struct ChromeWidget final : Widget
{
    ChromeWidget()
    {
        addChild(activityBar);
        addChild(sidebar);
        addChild(tabBar);
        addChild(statusBar);
        addChild(editor);
    }

    void layout() override
    {
        auto area = bounds();

        activityBar.setBounds(area.removeFromLeft(48.f));
        sidebar.setBounds(area.removeFromLeft(240.f));
        tabBar.setBounds(area.removeFromTop(35.f));
        statusBar.setBounds(area.removeFromBottom(22.f));
        editor.setBounds(area);
    }

    ProbeWidget activityBar;
    ProbeWidget sidebar;
    ProbeWidget tabBar;
    ProbeWidget statusBar;
    ProbeWidget editor;
};

Graphics::MouseEvent mouseAt(float x, float y)
{
    auto event = Graphics::MouseEvent {};
    event.pos = Graphics::Point {x, y};

    return event;
}
} // namespace

// --- layout -----------------------------------------------------------------

// Children get absolute bounds, not parent-relative ones. The whole layer rests
// on this: it is what lets a scissor rect and a hit test both use bounds()
// directly, with no transform stack in between.
auto tLayoutAssignsAbsoluteBounds = test("Widget/layoutAssignsAbsoluteBounds") = []
{
    auto chrome = ChromeWidget {};
    chrome.setBounds({0.f, 0.f, 1200.f, 800.f});

    check(chrome.activityBar.bounds().x == 0.f);
    check(chrome.activityBar.bounds().w == 48.f);

    // Absolute: the sidebar starts where the activity bar ended, rather than
    // at its own local zero.
    check(chrome.sidebar.bounds().x == 48.f);
    check(chrome.sidebar.bounds().w == 240.f);

    check(chrome.editor.bounds().x == 288.f);
    check(chrome.editor.bounds().w == 1200.f - 288.f);
};

// The tab bar along the top and the status bar along the bottom, which is the
// arrangement that was silently inverted while Rect's splitters were y-up.
auto tLayoutIsYDown = test("Widget/layoutPutsTheTabBarAtTheTop") = []
{
    auto chrome = ChromeWidget {};
    chrome.setBounds({0.f, 0.f, 1200.f, 800.f});

    check(chrome.tabBar.bounds().y == 0.f);
    check(chrome.tabBar.bounds().h == 35.f);

    check(chrome.statusBar.bounds().bottom() == 800.f);
    check(chrome.statusBar.bounds().y == 800.f - 22.f);

    // And the editor sits between them, touching neither.
    check(chrome.editor.bounds().y == 35.f);
    check(chrome.editor.bounds().bottom() == 800.f - 22.f);
};

// A resize relays out the subtree without anyone asking it to, because bounds
// are absolute and every one of them moved.
auto tResizeRelaysOut = test("Widget/resizeRelaysOutTheSubtree") = []
{
    auto chrome = ChromeWidget {};

    chrome.setBounds({0.f, 0.f, 1200.f, 800.f});
    chrome.setBounds({0.f, 0.f, 600.f, 400.f});

    check(chrome.editor.bounds().w == 600.f - 288.f);
    check(chrome.statusBar.bounds().y == 400.f - 22.f);
};

// --- hit-testing ------------------------------------------------------------

auto tHitTestFindsTheWidget = test("Widget/hitTestFindsTheWidgetUnderThePoint") = []
{
    auto chrome = ChromeWidget {};
    chrome.setBounds({0.f, 0.f, 1200.f, 800.f});

    check(chrome.widgetAt({10.f, 400.f}) == &chrome.activityBar);
    check(chrome.widgetAt({100.f, 400.f}) == &chrome.sidebar);
    check(chrome.widgetAt({600.f, 10.f}) == &chrome.tabBar);
    check(chrome.widgetAt({600.f, 790.f}) == &chrome.statusBar);
    check(chrome.widgetAt({600.f, 400.f}) == &chrome.editor);
};

// Rect::contains is half-open, so tiling widgets meet without overlapping and a
// point on a shared edge belongs to exactly one of them.
auto tHitTestEdgesDoNotOverlap = test("Widget/hitTestEdgesBelongToOneWidget") = []
{
    auto chrome = ChromeWidget {};
    chrome.setBounds({0.f, 0.f, 1200.f, 800.f});

    // x = 48 is the activity bar's right edge and the sidebar's left.
    check(chrome.widgetAt({47.f, 400.f}) == &chrome.activityBar);
    check(chrome.widgetAt({48.f, 400.f}) == &chrome.sidebar);

    // Outside the tree entirely.
    check(chrome.widgetAt({-1.f, 400.f}) == nullptr);
    check(chrome.widgetAt({1200.f, 400.f}) == nullptr);
};

// A decorative widget must not swallow clicks meant for something under it.
auto tHitTestSkipsTransparent = test("Widget/hitTestSkipsWidgetsNotWantingMouse") = []
{
    auto parent = ProbeWidget {};
    auto child = ProbeWidget {};

    parent.addChild(child);
    parent.setBounds({0.f, 0.f, 100.f, 100.f});
    child.setBounds({0.f, 0.f, 100.f, 100.f});

    child.takesMouse = false;

    // Falls through the child to the parent rather than returning null.
    check(parent.widgetAt({50.f, 50.f}) == &parent);

    parent.takesMouse = false;
    check(parent.widgetAt({50.f, 50.f}) == nullptr);
};

// Later children paint on top, so they take the click. An overlay or a popup is
// only usable if this is the way round.
auto tHitTestPrefersTheTopmost = test("Widget/hitTestPrefersTheTopmostChild") = []
{
    auto parent = ProbeWidget {};
    auto under = ProbeWidget {};
    auto over = ProbeWidget {};

    parent.addChild(under);
    parent.addChild(over);

    parent.setBounds({0.f, 0.f, 100.f, 100.f});
    under.setBounds({0.f, 0.f, 100.f, 100.f});
    over.setBounds({0.f, 0.f, 100.f, 100.f});

    check(parent.widgetAt({50.f, 50.f}) == &over);
};

auto tHiddenWidgetsAreNotHit = test("Widget/hiddenWidgetsAreNotHit") = []
{
    auto parent = ProbeWidget {};
    auto child = ProbeWidget {};

    parent.addChild(child);
    parent.setBounds({0.f, 0.f, 100.f, 100.f});
    child.setBounds({0.f, 0.f, 100.f, 100.f});

    child.setVisible(false);

    check(parent.widgetAt({50.f, 50.f}) == &parent);
};

// --- mouse routing and capture ----------------------------------------------

// The case this exists for: a selection drag that leaves the editor keeps
// extending the selection instead of being handed to whatever it passed over.
auto tCaptureHoldsThroughDrag = test("WidgetHost/dragStaysWithTheWidgetPressed") = []
{
    auto chrome = ChromeWidget {};
    auto host = WidgetHost {};

    host.setRoot(chrome);
    host.setBounds({0.f, 0.f, 1200.f, 800.f});

    host.mouseDown(mouseAt(600.f, 400.f));
    check(chrome.editor.downs == 1);

    // Out of the editor and over the sidebar.
    host.mouseDragged(mouseAt(100.f, 400.f));
    host.mouseUp(mouseAt(100.f, 400.f));

    check(chrome.editor.drags == 1);
    check(chrome.editor.ups == 1);

    // The sidebar it passed over saw none of it.
    check(chrome.sidebar.downs == 0);
    check(chrome.sidebar.drags == 0);
    check(chrome.sidebar.ups == 0);
};

// A drag with no press behind it goes nowhere, rather than to whatever happens
// to be under the pointer.
auto tDragWithoutCaptureGoesNowhere = test("WidgetHost/dragWithoutAPressIsDropped") = []
{
    auto chrome = ChromeWidget {};
    auto host = WidgetHost {};

    host.setRoot(chrome);
    host.setBounds({0.f, 0.f, 1200.f, 800.f});

    host.mouseDragged(mouseAt(600.f, 400.f));

    check(chrome.editor.drags == 0);
};

// The capture is released on Up, so the next drag can go elsewhere.
auto tCaptureIsReleased = test("WidgetHost/captureIsReleasedOnMouseUp") = []
{
    auto chrome = ChromeWidget {};
    auto host = WidgetHost {};

    host.setRoot(chrome);
    host.setBounds({0.f, 0.f, 1200.f, 800.f});

    host.mouseDown(mouseAt(600.f, 400.f));
    host.mouseUp(mouseAt(600.f, 400.f));

    host.mouseDragged(mouseAt(600.f, 400.f));

    check(chrome.editor.drags == 0);
};

// Wheel ignores the capture: it belongs to whatever is under the pointer, which
// is what the platform does and what eacp's own scroll-wheel tests pin.
auto tWheelIgnoresCapture = test("WidgetHost/wheelGoesUnderThePointerNotToTheCapture") = []
{
    auto chrome = ChromeWidget {};
    auto host = WidgetHost {};

    chrome.editor.consumesWheel = true;
    chrome.sidebar.consumesWheel = true;

    host.setRoot(chrome);
    host.setBounds({0.f, 0.f, 1200.f, 800.f});

    host.mouseDown(mouseAt(600.f, 400.f));
    host.mouseWheel(mouseAt(100.f, 400.f));

    check(chrome.sidebar.wheels == 1);
    check(chrome.editor.wheels == 0);
};

// A scroll a child cannot use is offered to its ancestors, so a list inside a
// scrolling panel passes on what it has no room for.
auto tWheelBubbles = test("WidgetHost/unusedWheelBubblesToTheParent") = []
{
    auto parent = ProbeWidget {};
    auto child = ProbeWidget {};
    auto host = WidgetHost {};

    parent.addChild(child);
    parent.setBounds({0.f, 0.f, 100.f, 100.f});
    child.setBounds({0.f, 0.f, 100.f, 100.f});

    parent.consumesWheel = true;
    child.consumesWheel = false;

    host.setRoot(parent);

    check(host.mouseWheel(mouseAt(50.f, 50.f)));
    check(child.wheels == 1);
    check(parent.wheels == 1);
};

// --- focus ------------------------------------------------------------------

auto tClickFocuses = test("WidgetHost/clickingFocusesTheWidget") = []
{
    auto chrome = ChromeWidget {};
    auto host = WidgetHost {};

    chrome.editor.takesFocus = true;
    chrome.sidebar.takesFocus = true;

    host.setRoot(chrome);
    host.setBounds({0.f, 0.f, 1200.f, 800.f});

    host.mouseDown(mouseAt(600.f, 400.f));
    check(host.focused() == &chrome.editor);
    check(chrome.editor.focusGains == 1);

    host.mouseDown(mouseAt(100.f, 400.f));
    check(host.focused() == &chrome.sidebar);
    check(chrome.editor.focusLosses == 1);
};

// Clicking something that does not take focus leaves focus where it was — a
// decorative status bar should not silently defocus the editor.
auto tClickingDecorationKeepsFocus = test("WidgetHost/clickingADecorationKeepsFocus") = []
{
    auto chrome = ChromeWidget {};
    auto host = WidgetHost {};

    chrome.editor.takesFocus = true;

    host.setRoot(chrome);
    host.setBounds({0.f, 0.f, 1200.f, 800.f});

    host.mouseDown(mouseAt(600.f, 400.f));
    host.mouseDown(mouseAt(600.f, 790.f));

    check(host.focused() == &chrome.editor);
    check(chrome.editor.focusLosses == 0);
};

// Clicking a row inside a focusable list focuses the list, not nothing.
auto tFocusWalksUpToAnAncestor = test("WidgetHost/clickingAChildFocusesItsFocusableParent") = []
{
    auto parent = ProbeWidget {};
    auto child = ProbeWidget {};
    auto host = WidgetHost {};

    parent.addChild(child);
    parent.setBounds({0.f, 0.f, 100.f, 100.f});
    child.setBounds({0.f, 0.f, 100.f, 100.f});

    parent.takesFocus = true;
    child.takesFocus = false;

    host.setRoot(parent);
    host.mouseDown(mouseAt(50.f, 50.f));

    check(host.focused() == &parent);
};

auto tKeysGoToTheFocused = test("WidgetHost/keysGoToTheFocusedWidget") = []
{
    auto chrome = ChromeWidget {};
    auto host = WidgetHost {};

    chrome.editor.takesFocus = true;
    chrome.editor.consumesKey = true;

    host.setRoot(chrome);
    host.setBounds({0.f, 0.f, 1200.f, 800.f});
    host.setFocus(&chrome.editor);

    check(host.keyDown(Graphics::KeyEvent {}));
    check(chrome.editor.keys == 1);
    check(chrome.sidebar.keys == 0);
};

// A key nobody wants comes back as unhandled, which is what lets the app fall
// through to its own shortcuts rather than swallowing everything.
auto tUnhandledKeysComeBack = test("WidgetHost/unhandledKeysAreReportedUnhandled") = []
{
    auto parent = ProbeWidget {};
    auto child = ProbeWidget {};
    auto host = WidgetHost {};

    parent.addChild(child);
    child.takesFocus = true;

    host.setRoot(parent);
    host.setFocus(&child);

    check(!host.keyDown(Graphics::KeyEvent {}));

    // Offered to the child and then to its parent, in that order.
    check(child.keys == 1);
    check(parent.keys == 1);
};

auto tKeysWithNoFocusAreUnhandled = test("WidgetHost/keysWithNothingFocusedAreUnhandled") = []
{
    auto chrome = ChromeWidget {};
    auto host = WidgetHost {};

    host.setRoot(chrome);

    check(!host.keyDown(Graphics::KeyEvent {}));
};

// --- focus traversal --------------------------------------------------------

auto tFocusOrderIsPaintOrder = test("WidgetHost/focusOrderFollowsTheTree") = []
{
    auto chrome = ChromeWidget {};
    auto host = WidgetHost {};

    chrome.sidebar.takesFocus = true;
    chrome.editor.takesFocus = true;

    host.setRoot(chrome);

    const auto order = host.focusableWidgets();

    check(order.size() == 2);
    check(order[0] == &chrome.sidebar);
    check(order[1] == &chrome.editor);
};

auto tTabCycles = test("WidgetHost/tabCyclesThroughFocusableWidgets") = []
{
    auto chrome = ChromeWidget {};
    auto host = WidgetHost {};

    chrome.sidebar.takesFocus = true;
    chrome.editor.takesFocus = true;

    host.setRoot(chrome);

    // With nothing focused, the first Tab takes the first widget.
    host.focusNext();
    check(host.focused() == &chrome.sidebar);

    host.focusNext();
    check(host.focused() == &chrome.editor);

    // And wraps rather than stopping at the end.
    host.focusNext();
    check(host.focused() == &chrome.sidebar);
};

// Shift+Tab off the first widget lands on the last. Written because the obvious
// `(index - 1) % count` goes negative there and indexes out of bounds.
auto tShiftTabWrapsBackwards = test("WidgetHost/shiftTabWrapsToTheLastWidget") = []
{
    auto chrome = ChromeWidget {};
    auto host = WidgetHost {};

    chrome.sidebar.takesFocus = true;
    chrome.editor.takesFocus = true;

    host.setRoot(chrome);
    host.setFocus(&chrome.sidebar);

    host.focusPrevious();
    check(host.focused() == &chrome.editor);
};

auto tHiddenWidgetsAreSkipped = test("WidgetHost/hiddenWidgetsAreSkippedByTab") = []
{
    auto chrome = ChromeWidget {};
    auto host = WidgetHost {};

    chrome.sidebar.takesFocus = true;
    chrome.editor.takesFocus = true;
    chrome.sidebar.setVisible(false);

    host.setRoot(chrome);

    host.focusNext();
    check(host.focused() == &chrome.editor);

    host.focusNext();
    check(host.focused() == &chrome.editor);
};

// Focus and capture are raw pointers into the old tree, so both have to go.
auto tSettingRootClearsState = test("WidgetHost/settingANewRootClearsFocusAndCapture") = []
{
    auto first = ChromeWidget {};
    auto second = ChromeWidget {};
    auto host = WidgetHost {};

    first.editor.takesFocus = true;

    host.setRoot(first);
    host.setBounds({0.f, 0.f, 1200.f, 800.f});
    host.mouseDown(mouseAt(600.f, 400.f));

    check(host.focused() == &first.editor);

    host.setRoot(second);
    check(host.focused() == nullptr);

    // The old capture is gone too: this drag must not reach the old editor.
    host.mouseDragged(mouseAt(600.f, 400.f));
    check(first.editor.drags == 0);
};
