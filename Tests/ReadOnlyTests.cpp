#include <ECodeEditor/EditorWidget.h>

#include <NanoTest/NanoTest.h>

// A read-only view of a file.
//
// The half that matters is not that typing is refused — it is that everything
// else still works. A viewer nobody can select out of, scroll or search is not a
// viewer, and each of those goes through a path that *could* have been gated by
// the same flag. So every test below that asserts a refusal is paired with one
// asserting something that must still happen.
//
// What is drawn is covered by the render tests; this is what a key does to the
// text, which needs no device.

using namespace nano;
using namespace ecode;
using namespace eacp;

namespace
{
struct Fixture
{
    Fixture()
    {
        open.file.setText("alpha\nbeta\ngamma\n");
        widget.setBounds({0.f, 0.f, 400.f, 300.f});
    }

    std::string text() const { return open.file.document().text(); }

    OpenFile open;
    EditorWidget widget {open};
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
    return keyEvent(0, std::move(characters));
}

Graphics::KeyEvent pressed(std::uint16_t code)
{
    return keyEvent(code, {});
}
} // namespace

auto tEditableByDefault = test("ReadOnly/editableByDefault") = []
{
    auto fixture = Fixture {};

    check(!fixture.widget.isReadOnly());
};

auto tRefusesTypedCharacters = test("ReadOnly/refusesTypedCharacters") = []
{
    auto fixture = Fixture {};
    fixture.widget.setReadOnly(true);

    const auto before = fixture.text();

    check(!fixture.widget.keyDown(typed("x")));
    check(fixture.text() == before);
};

auto tRefusesEditingKeys = test("ReadOnly/refusesReturnTabAndBothDeletes") = []
{
    auto fixture = Fixture {};
    fixture.widget.setReadOnly(true);

    fixture.widget.editor().placeCaret(3);

    const auto before = fixture.text();

    check(!fixture.widget.keyDown(pressed(Graphics::KeyCode::Return)));
    check(!fixture.widget.keyDown(pressed(Graphics::KeyCode::Tab)));
    check(!fixture.widget.keyDown(pressed(Graphics::KeyCode::Delete)));
    check(!fixture.widget.keyDown(pressed(Graphics::KeyCode::ForwardDelete)));

    check(fixture.text() == before);
};

// The refusal is a *decline*, not a swallow: the key falls through so a host can
// bind Return or Tab to something of its own over a read-only view. The second
// half is what makes this a claim about read-only rather than about the key.
auto tRefusedKeyIsNotConsumed = test("ReadOnly/aRefusedEditingKeyIsNotConsumed") = []
{
    auto fixture = Fixture {};

    fixture.widget.setReadOnly(true);
    check(!fixture.widget.keyDown(typed("x")));

    fixture.widget.setReadOnly(false);
    check(fixture.widget.keyDown(typed("x")));
};

auto tStillMovesTheCaret = test("ReadOnly/stillMovesTheCaret") = []
{
    auto fixture = Fixture {};
    fixture.widget.setReadOnly(true);

    fixture.widget.editor().placeCaret(0);

    check(fixture.widget.keyDown(pressed(Graphics::KeyCode::RightArrow)));
    check(fixture.widget.editor().cursor().head == 1);

    check(fixture.widget.keyDown(pressed(Graphics::KeyCode::DownArrow)));
    check(fixture.widget.caretLine() == 2);
};

auto tStillSelects = test("ReadOnly/stillSelectsAndCopiesOut") = []
{
    auto fixture = Fixture {};
    fixture.widget.setReadOnly(true);

    fixture.widget.editor().selectAll();

    check(fixture.widget.editor().selectedText() == fixture.text());
};

auto tStillSearches = test("ReadOnly/stillSearches") = []
{
    auto fixture = Fixture {};
    fixture.widget.setReadOnly(true);

    auto query = SearchQuery {};
    query.text = "beta";

    fixture.widget.setSearchQuery(query, 0);

    check(fixture.widget.search().count() == 1);
};

// Replace reaches the document without a keystroke, so gating keyDown alone
// would leave a read-only view editable through the find bar.
auto tRefusesReplace = test("ReadOnly/refusesReplace") = []
{
    auto fixture = Fixture {};

    auto query = SearchQuery {};
    query.text = "beta";

    fixture.widget.setSearchQuery(query, 0);
    fixture.widget.setReadOnly(true);

    const auto before = fixture.text();

    fixture.widget.replaceCurrent("BETA");
    check(fixture.text() == before);

    check(fixture.widget.replaceAllMatches("BETA") == 0);
    check(fixture.text() == before);
};

auto tClearingLetsKeysThrough = test("ReadOnly/clearingLetsTheSameKeysThrough") = []
{
    auto fixture = Fixture {};

    fixture.widget.setReadOnly(true);
    fixture.widget.keyDown(typed("x"));

    fixture.widget.setReadOnly(false);
    fixture.widget.editor().placeCaret(0);
    fixture.widget.keyDown(typed("x"));

    check(fixture.text().starts_with("xalpha"));
};

// setText is how a host hands over a string it already has. A buffer with no
// path has nothing on disk to disagree with, so it must not come up marked as
// unsaved work — a viewer showing a dirty dot over text nobody edited is
// reporting a change that never happened.
auto tSetTextIsClean = test("ReadOnly/textSetIntoAnUntitledBufferReadsAsClean") = []
{
    auto fixture = Fixture {};

    check(!fixture.open.file.isDirty());
    check(fixture.text() == "alpha\nbeta\ngamma\n");
};
