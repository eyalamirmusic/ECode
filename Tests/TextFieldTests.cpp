#include <ECodeUI/TextField.h>

#include <NanoTest/NanoTest.h>

// Typing, the caret and the selection. All of it is plain logic over a string —
// what the field *draws* needs an atlas and is covered by the render tests, but
// what a key does to the text does not.

using namespace nano;
using namespace ecode;
using namespace eacp;

namespace
{
struct Fixture
{
    Fixture()
    {
        field.setBounds({0.f, 0.f, 200.f, 30.f});
        field.onTextChanged = [this](const std::string&) { ++changes; };
    }

    ChromeTheme theme;
    TextField field {theme};

    int changes = 0;
};

Graphics::KeyEvent keyEvent(std::uint16_t code, std::string characters)
{
    auto event = Graphics::KeyEvent {};

    event.keyCode = code;
    event.charactersIgnoringModifiers = characters;
    event.characters = std::move(characters);

    return event;
}

Graphics::KeyEvent typed(std::string characters)
{
    return keyEvent(Graphics::KeyCode::Unknown, std::move(characters));
}

Graphics::KeyEvent pressed(std::uint16_t code)
{
    return keyEvent(code, {});
}

Graphics::KeyEvent shifted(std::uint16_t code)
{
    auto event = pressed(code);
    event.modifiers.shift = true;

    return event;
}
} // namespace

// --- typing -----------------------------------------------------------------

auto tTypingAppends = test("TextField/typingAppendsAtTheCaret") = []
{
    auto fixture = Fixture {};

    fixture.field.keyDown(typed("a"));
    fixture.field.keyDown(typed("b"));

    check(fixture.field.text() == "ab");
    check(fixture.field.caret() == 2);
    check(fixture.changes == 2);
};

auto tTypingReplacesSelection = test("TextField/typingReplacesTheSelection") = []
{
    auto fixture = Fixture {};

    fixture.field.setText("hello");
    fixture.field.selectAll();

    fixture.field.keyDown(typed("x"));

    check(fixture.field.text() == "x");
    check(!fixture.field.hasSelection());
};

// setText is how a caller seeds the field — from the editor's selection when ⌘F
// opens it. Firing onTextChanged there would run a search the person never
// asked for, and in the find bar that means the view jumping before they have
// typed anything.
auto tSetTextIsSilent = test("TextField/setTextDoesNotReportAChange") = []
{
    auto fixture = Fixture {};

    fixture.field.setText("seeded");

    check(fixture.field.text() == "seeded");
    check(fixture.changes == 0);
};

// The keys that belong to whoever owns the field. If the field swallowed these,
// Return could not mean "find next" and Escape could not close the bar.
auto tPassesOnItsOwnersKeys = test("TextField/doesNotConsumeReturnEscapeOrTab") = []
{
    auto fixture = Fixture {};

    check(!fixture.field.keyDown(pressed(Graphics::KeyCode::Return)));
    check(!fixture.field.keyDown(pressed(Graphics::KeyCode::Escape)));
    check(!fixture.field.keyDown(pressed(Graphics::KeyCode::Tab)));

    check(fixture.field.text().empty());
};

// AppKit reports every function key as a codepoint in the Unicode private use
// area — NSUpArrowFunctionKey is U+F700 — which encodes to three ordinary UTF-8
// bytes and sails through a control-character test. The expensive direction is
// the one that inserts: a field that only rejected bytes below 0x20 would put a
// box-drawing character in the query on every press of Up, and the query would
// then match nothing for no visible reason.
auto tIgnoresFunctionKeyCharacters =
    test("TextField/ignoresThePrivateUseCodepointsAppKitSendsForFunctionKeys") = []
{
    auto fixture = Fixture {};

    // U+F700, as UTF-8. This is verbatim what arrives in `characters` for Up.
    check(!fixture.field.keyDown(typed("\xef\x9c\x80")));

    check(fixture.field.text().empty());
    check(fixture.changes == 0);
};

// --- deleting ---------------------------------------------------------------

auto tBackspaceRemovesACharacter =
    test("TextField/backspaceRemovesOneCharacter") = []
{
    auto fixture = Fixture {};

    fixture.field.setText("abc");
    fixture.field.keyDown(pressed(Graphics::KeyCode::Delete));

    check(fixture.field.text() == "ab");
    check(fixture.field.caret() == 2);
};

// A whole sequence, not a byte. Half a sequence left behind is not merely wrong
// on screen — it stops the query matching anything, so the failure is a search
// that silently returns nothing.
auto tBackspaceRemovesAWholeSequence =
    test("TextField/backspaceRemovesAWholeUtf8Sequence") = []
{
    auto fixture = Fixture {};

    fixture.field.setText("café");
    fixture.field.keyDown(pressed(Graphics::KeyCode::Delete));

    check(fixture.field.text() == "caf");
};

auto tBackspaceAtTheStartDoesNothing =
    test("TextField/backspaceAtTheStartDoesNothing") = []
{
    auto fixture = Fixture {};

    fixture.field.setText("ab");
    fixture.field.keyDown(pressed(Graphics::KeyCode::Home));
    fixture.field.keyDown(pressed(Graphics::KeyCode::Delete));

    check(fixture.field.text() == "ab");
    check(fixture.changes == 0);
};

auto tBackspaceTakesTheSelection =
    test("TextField/backspaceTakesTheWholeSelection") = []
{
    auto fixture = Fixture {};

    fixture.field.setText("hello");
    fixture.field.selectAll();
    fixture.field.keyDown(pressed(Graphics::KeyCode::Delete));

    check(fixture.field.text().empty());
};

auto tForwardDeleteRemovesAhead = test("TextField/forwardDeleteRemovesAhead") = []
{
    auto fixture = Fixture {};

    fixture.field.setText("abc");
    fixture.field.keyDown(pressed(Graphics::KeyCode::Home));
    fixture.field.keyDown(pressed(Graphics::KeyCode::ForwardDelete));

    check(fixture.field.text() == "bc");
};

// --- the caret --------------------------------------------------------------

auto tCaretMovesByCharacter =
    test("TextField/theCaretMovesByCharacterNotByByte") = []
{
    auto fixture = Fixture {};

    fixture.field.setText("é");

    check(fixture.field.caret() == 2); // two bytes

    fixture.field.keyDown(pressed(Graphics::KeyCode::LeftArrow));

    check(fixture.field.caret() == 0);
};

auto tHomeAndEnd = test("TextField/homeAndEndGoToTheEnds") = []
{
    auto fixture = Fixture {};

    fixture.field.setText("hello");

    fixture.field.keyDown(pressed(Graphics::KeyCode::Home));
    check(fixture.field.caret() == 0);

    fixture.field.keyDown(pressed(Graphics::KeyCode::End));
    check(fixture.field.caret() == 5);
};

// Collapsing a selection leftwards goes to its start rather than stepping back
// from the head — the case that separates a field that treats a selection as a
// range from one that treats it as a caret with a flag.
auto tLeftCollapsesToTheStart =
    test("TextField/leftCollapsesASelectionToItsStart") = []
{
    auto fixture = Fixture {};

    fixture.field.setText("hello");
    fixture.field.selectAll(); // head at 5, anchor at 0

    fixture.field.keyDown(pressed(Graphics::KeyCode::LeftArrow));

    check(fixture.field.caret() == 0);
    check(!fixture.field.hasSelection());
};

auto tRightCollapsesToTheEnd =
    test("TextField/rightCollapsesASelectionToItsEnd") = []
{
    auto fixture = Fixture {};

    fixture.field.setText("hello");
    fixture.field.keyDown(pressed(Graphics::KeyCode::Home));
    fixture.field.keyDown(shifted(Graphics::KeyCode::End)); // select forwards

    fixture.field.keyDown(pressed(Graphics::KeyCode::RightArrow));

    check(fixture.field.caret() == 5);
    check(!fixture.field.hasSelection());
};

auto tShiftExtends = test("TextField/shiftExtendsTheSelection") = []
{
    auto fixture = Fixture {};

    fixture.field.setText("hello");
    fixture.field.keyDown(shifted(Graphics::KeyCode::LeftArrow));

    check(fixture.field.hasSelection());
    check(fixture.field.selectionStart() == 4);
    check(fixture.field.selectionEnd() == 5);
    check(fixture.field.selectedText() == "o");
};

// --- focus ------------------------------------------------------------------

// Two fields side by side, both drawing a caret, means the person cannot tell
// where their typing is going. The caret is only drawn when focused, and this
// is the state that decides it.
auto tTracksFocus = test("TextField/tracksWhetherItHasFocus") = []
{
    auto fixture = Fixture {};

    check(!fixture.field.hasFocus());

    fixture.field.focusGained();
    check(fixture.field.hasFocus());

    fixture.field.focusLost();
    check(!fixture.field.hasFocus());
};
