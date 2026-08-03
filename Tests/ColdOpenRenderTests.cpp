#include <ECodeEditor/EditorWidget.h>
#include <ECodeWidgets/WidgetHost.h>
#include <ECodeSyntax/SyntaxHighlighter.h>

#include <NanoTest/NanoTest.h>

#include <eacp/GPU/GPU.h>
#include <eacp/Text/Text.h>

#include <optional>
#include <set>
#include <string>

// The cold open as the application assembles it: a real EditorWidget with a real
// budgeted SyntaxHighlighter in a real WidgetHost, rendered off-screen.
//
// ColdOpenTests covers what the highlighter does. What it cannot say is whether
// the thing around it behaves: that the frame which gave up on the parse still
// *drew the file*, and that something asks for the next one. A view that drew
// nothing while it waited, or one that never came back, would satisfy every
// assertion in that file.
//
// Off-screen rather than in a live window, for the reason PLAN.md §9 gives: no
// focus, no stolen screen, and it stays as a regression test afterwards.

using namespace nano;
using namespace eacp;
using namespace ecode;

namespace
{
constexpr auto viewWidth = 700.f;
constexpr auto viewHeight = 400.f;

// Long enough that parsing it cannot fit in the budget below, and made of real
// constructs so there is something to colour.
Document bigSample(int lines)
{
    auto text = std::string {};
    auto index = 0;

    while (static_cast<int>(std::count(text.begin(), text.end(), '\n')) < lines)
    {
        const auto n = std::to_string(index++);

        text += "// widget " + n + "\n";
        text += "struct Widget" + n + "\n{\n";
        text += "    int value = " + n + ";\n";
        text += "    const char* name = \"widget " + n + "\";\n\n";
        text += "    int compute(int factor) const\n    {\n";
        text += "        return value * factor;\n    }\n};\n\n";
    }

    return Document::fromText(std::move(text));
}

// One microsecond rather than the app's two milliseconds, for the reason
// ColdOpenTests gives: a budget that an optimised build clears and a debug build
// does not would make this assert different things per build.
constexpr auto tinyBudget = std::chrono::microseconds {1};

struct ColdOpenView final : GPU::GPUView
{
    ColdOpenView()
    {
        setSampleCount(1);
        setBounds({0.f, 0.f, viewWidth, viewHeight});

        open.file.editor().setDocument(bigSample(600));
        open.highlighter = makeOwned<SyntaxHighlighter>(tinyBudget);

        host.setRoot(editor);

        // What GPUView::repaint stands in for. Counting it is the only way to see
        // that the view asked to be drawn again, which on an app that draws on
        // demand is the difference between colours that arrive and colours that
        // never do. Widget::repaint walks to the root, and here the editor is it.
        editor.onRepaintNeeded = [this] { ++repaintsRequested; };
    }

    bool build()
    {
        auto request = Text::FontRequest {};
        request.family = "Menlo";
        request.pointSize = 13.f;
        request.scale = 1.f;

        if (!Text::GlyphRasterizer {request}.isValid())
            return false;

        atlas = makeOwned<Text::GlyphAtlas>(Text::rasterizerFaceFactory(),
                                            request,
                                            512,
                                            2048);

        renderer.emplace(*atlas, theme, 1.f);
        glyphs.emplace();
        glyphs->setViewportSize({viewWidth, viewHeight});

        editor.setRenderer(&renderer.value());

        editor.setBounds({0.f, 0.f, viewWidth, viewHeight});
        host.setFocus(&editor);

        return true;
    }

    void render(GPU::Frame& frame) override
    {
        auto pass = frame.beginPass({theme.background});

        if (!renderer || !atlas || !glyphs)
            return;

        auto sprites =
            Sprites::SpriteRenderer {{viewWidth, viewHeight}, sampleCount()};

        host.prepare(*atlas);
        atlas->commit();

        auto context = PaintContext {
            pass, sprites, *glyphs, *atlas, {0.f, 0.f, viewWidth, viewHeight}, 1.f};

        host.paint(context);
    }

    SyntaxHighlighter& highlighter()
    {
        return *static_cast<SyntaxHighlighter*>(open.highlighter.get());
    }

    TextTheme theme;

    OpenFile open;

    EditorWidget editor {open};
    WidgetHost host;

    int repaintsRequested = 0;

    OwningPointer<Text::GlyphAtlas> atlas;
    std::optional<TextRenderer> renderer;
    std::optional<Text::GlyphRenderer> glyphs;
};

// Pixels that are not the background, which for a text view is the text.
int inkIn(const Graphics::Image& image)
{
    auto total = 0;

    for (auto y = 0; y < image.height(); ++y)
        for (auto x = 0; x < image.width(); ++x)
            if (image.at(x, y).r > 0.3f || image.at(x, y).g > 0.3f)
                ++total;

    return total;
}

// Quantised, so an antialiased glyph edge — the background blended toward a text
// colour — does not register as a colour of its own.
std::set<int> distinctColors(const Graphics::Image& image)
{
    auto colors = std::set<int> {};

    for (auto y = 0; y < image.height(); ++y)
    {
        for (auto x = 0; x < image.width(); ++x)
        {
            const auto pixel = image.at(x, y);

            if (pixel.r + pixel.g + pixel.b < 1.4f)
                continue;

            const auto r = static_cast<int>(pixel.r * 6.f);
            const auto g = static_cast<int>(pixel.g * 6.f);
            const auto b = static_cast<int>(pixel.b * 6.f);

            colors.insert(r * 49 + g * 7 + b);
        }
    }

    return colors;
}
} // namespace

// The whole point, stated as a picture: the frame that ran out of parsing budget
// still shows the file.
//
// The ink count is what makes it a test. "The parse was unfinished" is satisfied
// by a view that drew a blank rectangle and waited, which is the behaviour being
// ruled out — and it is the same trap PLAN.md §9 records for the idle-frame test,
// where two blank frames compared equal.
auto tFirstFrameDrawsTheFile =
    test("ColdOpenRender/theFirstFrameDrawsTheTextAnyway") = []
{
    auto view = ColdOpenView {};

    if (!view.build())
        return;

    const auto first = view.renderToImage(1.f);

    check(first.isValid());
    check(view.highlighter().hasPendingWork());
    check(inkIn(first) > 500);
};

// And that it comes back. The app draws on demand, so a frame nobody asks for is
// a file that stays uncoloured until an unrelated event — a caret blink, a
// keystroke — happens to request one.
auto tPendingWorkAsksForAnotherFrame =
    test("ColdOpenRender/anUnfinishedParseAsksForAnotherFrame") = []
{
    auto view = ColdOpenView {};

    if (!view.build())
        return;

    view.repaintsRequested = 0;

    view.renderToImage(1.f);

    check(view.highlighter().hasPendingWork());
    check(view.repaintsRequested > 0);
};

// The colours arrive, and the frame that has them is a different picture from the
// frame that did not.
//
// Compared against each other rather than against a count anybody chose: what
// matters is that colouring the file changed what came out, and two renders of the
// same view are the only pair that can say so without pinning the theme.
auto tColoursArriveOverLaterFrames =
    test("ColdOpenRender/theColoursArriveOnceTheParseLands") = []
{
    auto view = ColdOpenView {};

    if (!view.build())
        return;

    const auto plain = view.renderToImage(1.f);

    check(view.highlighter().hasPendingWork());

    auto frames = 1;

    while (view.highlighter().hasPendingWork() && frames < 20000)
    {
        view.renderToImage(1.f);
        ++frames;
    }

    check(frames > 1);
    check(!view.highlighter().hasPendingWork());

    const auto coloured = view.renderToImage(1.f);

    check(distinctColors(coloured).size() > distinctColors(plain).size());

    // The same text either way: the file did not appear, it was recoloured. An
    // uncoloured first frame drawing nothing would satisfy the comparison above
    // on its own.
    check(inkIn(plain) > 500);
};

// And once it has landed, the view stops asking. A repaint requested every frame
// forever is an editor that never idles — which on this app means never stopping
// GPU work, and it is exactly what the on-demand renderer exists to avoid.
auto tSettledViewStopsAsking =
    test("ColdOpenRender/aFinishedParseStopsAskingForFrames") = []
{
    auto view = ColdOpenView {};

    if (!view.build())
        return;

    // do/while, because nothing is pending until a frame has asked: a loop that
    // tested first would settle nothing and then measure the very first frame.
    auto frames = 0;

    do
    {
        view.renderToImage(1.f);
        ++frames;
    } while (view.highlighter().hasPendingWork() && frames < 20000);

    check(frames > 1); // or the parse finished immediately and this proves nothing

    view.repaintsRequested = 0;

    view.renderToImage(1.f);

    check(view.repaintsRequested == 0);
};
