#include <ECodeRender/FontSettings.h>
#include <ECodeWorkbench/Keymap.h>

#include <NanoTest/NanoTest.h>

// The editor's font size, and the arithmetic ⌘+ and ⌘- drive.
//
// Two things here are easy to get wrong and silent when they are. The zoom is
// held apart from the configured size, so Reset goes back to what was set
// rather than to a constant — and the clamp is applied to the resulting size
// rather than to the accumulated zoom, so ⌘+ held down past the ceiling does
// not have to be pressed back down again before anything moves.

using namespace nano;
using namespace ecode;

auto tFontDefaultSize = test("FontSettings/sizeIsWhatWasConfiguredUntilZoomed") = []
{
    auto font = FontSettings {};

    check(font.size() == font.pointSize);

    font.pointSize = 16.f;

    check(font.size() == 16.f);
};

auto tFontZoomSteps = test("FontSettings/zoomStepsByWholePoints") = []
{
    auto font = FontSettings {};
    font.pointSize = 13.f;

    font.zoomBy(1);
    check(font.size() == 14.f);

    font.zoomBy(2);
    check(font.size() == 16.f);

    font.zoomBy(-3);
    check(font.size() == 13.f);
};

// The reason zoom is a separate number. Reset means "the size that was
// configured", which is 13 only until something configures anything else — and
// the day that changes, a Reset that went back to a constant would be a bug
// nobody could see in the code that caused it.
auto tFontResetReturnsToConfigured =
    test("FontSettings/resetReturnsToTheConfiguredSize") = []
{
    auto font = FontSettings {};
    font.pointSize = 16.f;

    font.zoomBy(4);
    check(font.size() == 20.f);

    font.resetZoom();
    check(font.size() == 16.f);
};

// Clamped on the size, not on the zoom. Holding ⌘+ against the ceiling and then
// pressing ⌘- once has to make the text smaller immediately; an accumulator
// left to run would have swallowed every press over the limit.
auto tFontZoomClampsOnSize = test("FontSettings/zoomStopsAtTheEndsOfTheRange") = []
{
    auto font = FontSettings {};

    for (auto press = 0; press < 200; ++press)
        font.zoomBy(1);

    check(font.size() == FontSettings::maximumSize);

    font.zoomBy(-1);
    check(font.size() == FontSettings::maximumSize - FontSettings::zoomStep);

    for (auto press = 0; press < 200; ++press)
        font.zoomBy(-1);

    check(font.size() == FontSettings::minimumSize);

    font.zoomBy(1);
    check(font.size() == FontSettings::minimumSize + FontSettings::zoomStep);
};

// What greys the commands out. Without it the two chords keep firing at the
// ends of the range, doing nothing and saying nothing about why.
auto tFontCanZoom = test("FontSettings/canZoomAnswersAtTheEnds") = []
{
    auto font = FontSettings {};

    check(font.canZoom(1));
    check(font.canZoom(-1));

    // A step of nothing changes nothing, whatever the size.
    check(!font.canZoom(0));

    font.pointSize = FontSettings::maximumSize;

    check(!font.canZoom(1));
    check(font.canZoom(-1));

    font.pointSize = FontSettings::minimumSize;

    check(font.canZoom(1));
    check(!font.canZoom(-1));
};

// What the frame compares to decide whether the atlas it holds is still the
// atlas the settings ask for.
auto tFontEquality = test("FontSettings/comparesFamilyAndSize") = []
{
    auto font = FontSettings {};
    auto other = FontSettings {};

    check(font == other);

    other.zoomBy(1);
    check(font != other);

    other.resetZoom();
    check(font == other);

    other.family = "Courier";
    check(font != other);
};

// The chords, from the other side. ⌘+ is ⇧⌘= on a US keyboard and ⌘= without
// the shift, and both have to reach the same command — but they are different
// chords, so both have to be bound. Written by the *unshifted* key, because
// that is what Chord::fromEvent matches punctuation on.
auto tFontZoomChords = test("FontSettings/bothSpellingsOfCommandPlusAreBound") = []
{
    auto keymap = Keymap {};

    keymap.bind("cmd+shift+=", "view.increaseFontSize");
    keymap.bind("cmd+=", "view.increaseFontSize");
    keymap.bind("cmd+-", "view.decreaseFontSize");
    keymap.bind("cmd+0", "view.resetFontSize");

    check(keymap.commandFor(Chord::parse("cmd+=")) == "view.increaseFontSize");
    check(keymap.commandFor(Chord::parse("cmd+shift+=")) == "view.increaseFontSize");
    check(keymap.commandFor(Chord::parse("cmd+-")) == "view.decreaseFontSize");
    check(keymap.commandFor(Chord::parse("cmd+0")) == "view.resetFontSize");

    // The unshifted spelling is what the menu and the palette print, which is
    // why it is bound second: shortcutFor hands back the binding that wins.
    check(!keymap.shortcutFor("view.increaseFontSize").single().modifiers.shift);
};
