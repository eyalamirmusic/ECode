#include <ECodeUI/EditorWidget.h>
#include <ECodeUI/FindBar.h>
#include <ECodeUI/WidgetHost.h>

#include <NanoTest/NanoTest.h>

#include <eacp/GPU/GPU.h>
#include <eacp/Text/Text.h>

#include <array>
#include <cmath>
#include <optional>

// Find and replace as the application actually assembles it: a real EditorWidget
// and a real FindBar in a real WidgetHost, wired the way Main.cpp wires them,
// driven by synthesized keys and clicks, and rendered off-screen.
//
// This exists because the unit tests could not have caught the bug that shipped
// past them. FindBarTests knows what the bar reports and SearchTests knows what
// the search finds, but the current hit came out painted in the selection's blue
// — the two halves were each correct and the composition was not. Nothing short
// of drawing the whole thing together says otherwise.
//
// Driving the assembled tree rather than the live app is also the difference
// between a test and a person watching a window: this needs no focus, steals no
// keyboard, and runs in CI.

using namespace nano;
using namespace eacp;
using namespace ecode;

namespace
{
constexpr auto viewWidth = 900.f;
constexpr auto viewHeight = 420.f;
constexpr auto findMargin = 14.f;

// Exactly one occurrence per line, so a band can be checked without working out
// which of several hits it caught, and a capitalised one at the end so case
// sensitivity has something to change: four hits folded, three exact.
constexpr auto sampleText = "int counter = 0;\n"
                            "value = counter + 1;\n"
                            "return counter;\n"
                            "struct Counter {};\n";

// The application shell, reduced to the two widgets under test and the wiring
// between them. Kept in step with Main.cpp by hand — which is the cost of the
// application owning that wiring, and cheap next to the alternative of the
// widgets knowing about each other.
struct FindTestView final : GPU::GPUView
{
    FindTestView()
    {
        setSampleCount(1);
        setBounds({0.f, 0.f, viewWidth, viewHeight});

        file.editor().setDocument(Document::fromText(sampleText));

        root.addChild(editor);
        root.addChild(bar);

        bar.onQueryChanged = [this]
        {
            editor.setSearchQuery(bar.query(), searchOrigin);
            pushCount();
        };

        bar.onFindNext = [this]
        {
            editor.findNext();
            pushCount();
        };

        bar.onFindPrevious = [this]
        {
            editor.findPrevious();
            pushCount();
        };

        bar.onReplace = [this]
        {
            editor.replaceCurrent(bar.replacement());
            pushCount();
        };

        bar.onReplaceAll = [this]
        {
            editor.replaceAllMatches(bar.replacement());
            pushCount();
        };

        bar.onClosed = [this]
        {
            editor.clearSearch();
            host.setFocus(&editor);
            layOut();
        };

        bar.onFocusRequested = [this](Widget& target) { host.setFocus(&target); };

        host.setRoot(root);
    }

    void pushCount()
    {
        bar.setMatchCount(editor.search().currentNumber(), editor.search().count());
    }

    // The same split Main.cpp's WindowLayout does, minus the chrome that is not
    // under test here.
    void layOut()
    {
        const auto area = Graphics::Rect {0.f, 0.f, viewWidth, viewHeight};

        root.setBounds(area);
        editor.setBounds(area);

        const auto width = std::min(bar.barWidth(), area.w);

        bar.setBounds(
            {area.right() - findMargin - width, area.y, width, bar.barHeight()});
    }

    bool build()
    {
        auto request = Text::FontRequest {};
        request.family = "Menlo";
        request.pointSize = 13.f;
        request.scale = 1.f;

        auto rasterizer = makeOwned<Text::GlyphRasterizer>(request);

        if (!rasterizer->isValid())
            return false;

        atlas = makeOwned<Text::GlyphAtlas>(
            OwningPointer<Text::GlyphSource> {std::move(rasterizer)}, 512, 2048);

        renderer.emplace(*atlas, textTheme);
        glyphs.emplace();
        glyphs->setViewportSize({viewWidth, viewHeight});

        editor.setRenderer(&renderer.value());

        layOut();
        host.setFocus(&editor);

        return true;
    }

    // ⌘F, as the application handles it: seed from the selection, remember where
    // to search from, show, focus the field.
    void openFind(bool withReplace = false)
    {
        searchOrigin = file.editor().cursor().start();

        bar.show(file.editor().selectedText(), withReplace);
        host.setFocus(&bar.keyboardTarget());

        layOut();
    }

    void type(const std::string& text)
    {
        for (auto character: text)
        {
            auto event = Graphics::KeyEvent {};

            event.keyCode = Graphics::KeyCode::Unknown;
            event.characters = std::string {character};
            event.charactersIgnoringModifiers = event.characters;

            host.keyDown(event);
        }
    }

    void press(std::uint16_t code)
    {
        auto event = Graphics::KeyEvent {};
        event.keyCode = code;

        host.keyDown(event);
    }

    void clickAt(const Graphics::Point& point)
    {
        auto event = Graphics::MouseEvent {};
        event.pos = point;

        host.mouseDown(event);
        host.mouseUp(event);
    }

    void render(GPU::Frame& frame) override
    {
        auto pass = frame.beginPass({textTheme.background});

        if (!atlas || !glyphs || !renderer)
            return;

        auto sprites =
            Sprites::SpriteRenderer {{viewWidth, viewHeight}, sampleCount()};

        host.prepare(*atlas);
        atlas->commit();

        auto context = PaintContext {
            pass, sprites, *glyphs, *atlas, {0.f, 0.f, viewWidth, viewHeight}, 1.f};

        host.paint(context);
    }

    // Exactly where a word sits on screen.
    //
    // Measured through the renderer's own columnToX rather than assumed to be a
    // fixed strip at the start of the line: "Counter" on the last line begins
    // seven characters in, and a band wide enough to cover it from column zero
    // would also swallow whatever is beside it. Getting this wrong makes a test
    // that reads the background and reports the highlight missing.
    Graphics::Rect bandOfWord(std::size_t line, std::string_view word) const
    {
        const auto text = file.document().line(line);
        const auto column = text.find(word);

        if (column == std::string_view::npos)
            return {};

        const auto left = renderer->columnToX(text, column);
        const auto right = renderer->columnToX(text, column + word.size());

        const auto lineHeight = renderer->lineHeight();
        const auto x = renderer->gutterWidth(file.document().lineCount()) + 8.f;

        return {x + left,
                static_cast<float>(line) * lineHeight + 2.f,
                right - left,
                lineHeight - 4.f};
    }

    ChromeTheme theme;
    TextTheme textTheme;

    TextFile file;

    Widget root;
    EditorWidget editor {file};
    FindBar bar {theme};

    WidgetHost host;

    std::size_t searchOrigin = 0;

    OwningPointer<Text::GlyphAtlas> atlas;
    std::optional<TextRenderer> renderer;
    std::optional<Text::GlyphRenderer> glyphs;
};

Graphics::Color averageOver(const Graphics::Image& image, const Graphics::Rect& area)
{
    auto r = 0.f;
    auto g = 0.f;
    auto b = 0.f;
    auto count = 0;

    const auto x1 = std::min(static_cast<int>(area.right()), image.width());
    const auto y1 = std::min(static_cast<int>(area.bottom()), image.height());

    for (auto y = std::max(0, static_cast<int>(area.y)); y < y1; ++y)
    {
        for (auto x = std::max(0, static_cast<int>(area.x)); x < x1; ++x)
        {
            const auto pixel = image.at(x, y);

            r += pixel.r;
            g += pixel.g;
            b += pixel.b;

            ++count;
        }
    }

    if (count == 0)
        return {};

    const auto scale = 1.f / static_cast<float>(count);

    return {r * scale, g * scale, b * scale};
}

// A band that is warmer than the page behind it — which is what both hit
// colours are, and what neither the background nor a selection is.
bool looksHighlighted(const Graphics::Image& image,
                      const Graphics::Rect& band,
                      const Graphics::Color& page)
{
    const auto average = averageOver(image, band);

    return average.r > page.r + 0.05f && average.r > average.b;
}
} // namespace

// The whole path, end to end: ⌘F, type a query, and every occurrence is warm
// while the untouched lines are not.
auto tTypingAQueryLightsTheFile =
    test("FindRender/typingAQueryLightsEveryOccurrence") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = FindTestView {};

    if (!view.build())
        return;

    const auto before = view.renderToImage(1.f);

    view.openFind();
    view.type("counter");

    const auto after = view.renderToImage(1.f);

    check(before.isValid() && after.isValid());

    check(view.editor.search().count() == 4);

    // One per line, the last of them capitalised.
    const auto words = std::array {std::string_view {"counter"},
                                   std::string_view {"counter"},
                                   std::string_view {"counter"},
                                   std::string_view {"Counter"}};

    for (std::size_t line = 0; line < words.size(); ++line)
    {
        const auto band = view.bandOfWord(line, words[line]);

        check(!band.isEmpty());
        check(!looksHighlighted(before, band, view.textTheme.background));
        check(looksHighlighted(after, band, view.textTheme.background));
    }
};

// The composition bug, pinned at the level it actually occurred: the hit the
// find bar is on is *also* the selection, and it has to keep reading as a hit.
auto tCurrentHitOutranksItsSelection =
    test("FindRender/theCurrentHitIsNotPaintedOverByItsOwnSelection") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = FindTestView {};

    if (!view.build())
        return;

    view.openFind();
    view.type("counter");

    // Return is find-next, which selects the hit it lands on.
    view.press(Graphics::KeyCode::Return);
    view.press(Graphics::KeyCode::Return);

    const auto image = view.renderToImage(1.f);

    check(image.isValid());
    check(view.editor.editor().cursor().hasSelection());

    const auto current = view.editor.search().currentIndex();

    check(current >= 0);

    const auto line =
        view.file.document().lineAt(view.editor.search().matches()[current].start);

    const auto onIt = averageOver(image, view.bandOfWord(line, "counter"));
    const auto elsewhere =
        averageOver(image, view.bandOfWord(line == 0 ? 1 : 0, "counter"));

    // Warmer than the other hits, and warm rather than blue — the selection
    // drawn on top would satisfy neither.
    check(onIt.r > elsewhere.r + 0.1f);
    check(onIt.r > onIt.b);
};

// Clicking the case toggle changes what counts as a match, and the file has to
// follow immediately — the highlighting is stale the instant the option flips,
// and a bar that reported the change without redrawing would look identical
// until the next keystroke.
auto tCaseToggleChangesTheFile = test("FindRender/theCaseToggleRedrawsTheFile") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = FindTestView {};

    if (!view.build())
        return;

    view.openFind();
    view.type("counter");

    check(view.editor.search().count() == 4);

    const auto insensitive = view.renderToImage(1.f);

    // The "Aa" chip: after the bar's padding and its 200pt field, plus the gap.
    const auto toggle = Graphics::Point {
        view.bar.bounds().x + 8.f + 200.f + 6.f + 15.f, view.bar.bounds().y + 17.f};

    view.clickAt(toggle);

    check(view.bar.query().caseSensitive);
    check(view.editor.search().count() == 3);

    const auto sensitive = view.renderToImage(1.f);

    check(insensitive.isValid() && sensitive.isValid());

    // "Counter" on the last line is no longer a match, so its band goes cold
    // while the first line's stays lit.
    const auto page = view.textTheme.background;
    const auto capitalised = view.bandOfWord(3, "Counter");

    check(looksHighlighted(insensitive, capitalised, page));
    check(!looksHighlighted(sensitive, capitalised, page));

    check(looksHighlighted(sensitive, view.bandOfWord(0, "counter"), page));
};

// Replace All through the button, and the text on screen has to be the new text
// — not merely the buffer. A replace that updated the document without
// invalidating the match list would leave the old hits lit over the new words.
auto tReplaceAllRewritesTheFile =
    test("FindRender/replaceAllRewritesWhatIsDrawn") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = FindTestView {};

    if (!view.build())
        return;

    view.openFind(true);
    view.type("counter");

    check(view.editor.search().count() == 4);

    // Where the words are, measured before they are replaced.
    const auto bands = std::array {view.bandOfWord(0, "counter"),
                                   view.bandOfWord(1, "counter"),
                                   view.bandOfWord(2, "counter"),
                                   view.bandOfWord(3, "Counter")};

    // Into the replace field, which Tab reaches.
    view.press(Graphics::KeyCode::Tab);
    view.type("total");

    // The "All" chip on the second row: padding, field, gap, the 68pt Replace
    // button, gap.
    const auto all =
        Graphics::Point {view.bar.bounds().x + 8.f + 200.f + 6.f + 68.f + 6.f + 20.f,
                         view.bar.bounds().y + 34.f + 17.f};

    view.clickAt(all);

    check(view.file.document().text().find("counter") == std::string::npos);
    check(view.file.document().text().find("total") != std::string::npos);

    // Case-insensitively there were four, and "Counter" became "total" too.
    check(view.editor.search().count() == 0);

    const auto image = view.renderToImage(1.f);

    check(image.isValid());

    // Nothing is a hit any more, so nothing is warm.
    for (const auto& band: bands)
        check(!looksHighlighted(image, band, view.textTheme.background));
};

// Typing in the *document* while the bar is open invalidates the match list, and
// the highlighting has to follow. The list is rebuilt during the paint prepass
// when the document's revision has moved, so this renders and then reads back:
// stale hits would go on being drawn at offsets the text has since left, which
// puts the tint over whatever slid into their place.
auto tEditingTheDocumentRefreshesTheHits =
    test("FindRender/editingTheDocumentRefreshesTheHighlighting") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = FindTestView {};

    if (!view.build())
        return;

    view.openFind();
    view.type("counter");

    check(view.editor.search().count() == 4);

    // Back to the document, and add another occurrence ahead of the rest.
    view.host.setFocus(&view.editor);
    view.editor.editor().moveToDocumentStart();
    view.type("counter ");

    // The refresh happens in the prepass, so the count is only right after a
    // frame has been drawn — which is exactly the coupling worth pinning.
    const auto image = view.renderToImage(1.f);

    check(image.isValid());
    check(view.editor.search().count() == 5);

    // bandOfWord finds the first occurrence on the line, which is the one just
    // typed at column zero.
    check(looksHighlighted(
        image, view.bandOfWord(0, "counter"), view.textTheme.background));
};

// Escape closes the bar and takes the highlighting with it. A file left covered
// in orange with nothing on screen explaining why is the failure this rules out.
auto tEscapeClearsTheHighlighting =
    test("FindRender/closingTheBarClearsTheHighlighting") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = FindTestView {};

    if (!view.build())
        return;

    view.openFind();
    view.type("counter");

    const auto searching = view.renderToImage(1.f);

    view.press(Graphics::KeyCode::Escape);

    check(!view.bar.isOpen());
    check(view.editor.search().count() == 0);

    const auto closed = view.renderToImage(1.f);

    check(searching.isValid() && closed.isValid());

    const auto page = view.textTheme.background;

    const auto band = view.bandOfWord(0, "counter");

    check(looksHighlighted(searching, band, page));
    check(!looksHighlighted(closed, band, page));
};
