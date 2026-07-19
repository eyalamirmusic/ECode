#include "Common.h"

#include <ECodeRender/TextRenderer.h>
#include <ECodeCore/Cursor.h>
#include <ECodeSyntax/SyntaxHighlighter.h>

#include <eacp/Text/Text.h>

#include <set>

// The last link in the chain: highlighter -> renderer -> GPU -> pixels.
//
// Everything either side of this is already covered — the spans by
// SyntaxHighlighterTests, the glyph path by eacp's own text-rendering tests —
// but nothing yet proves the two meet correctly. A theme that resolved every
// kind to the same colour, or a renderer that ignored the spans it was handed,
// would pass all of those and still render a uniform wash.
//
// So this draws a real highlighted document off-screen and counts the distinct
// colours that came out. Self-skips without a GPU device or a font.

using namespace nano;
using namespace eacp;
using namespace ecode;

namespace
{
constexpr auto viewWidth = 420.f;
constexpr auto viewHeight = 200.f;

struct EditorTestView final : GPU::GPUView
{
    EditorTestView()
    {
        setSampleCount(1);
        setBounds({0.f, 0.f, viewWidth, viewHeight});
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

        renderer.emplace(*atlas, theme);
        glyphs.emplace();
        glyphs->setViewportSize({viewWidth, viewHeight});

        return true;
    }

    void render(GPU::Frame& frame) override
    {
        auto pass = frame.beginPass({theme.background});

        if (!renderer || !atlas || !glyphs)
            return;

        const auto area = Graphics::Rect {0.f, 0.f, viewWidth, viewHeight};

        auto sprites =
            Sprites::SpriteRenderer {{viewWidth, viewHeight}, sampleCount()};

        renderer->prepare(document, area, 0.f);
        atlas->commit();

        if (highlighter != nullptr)
            highlighter->update(document,
                                renderer->firstVisibleLine(0.f),
                                renderer->lastVisibleLine(document, area, 0.f));

        // The context binds the sprite pipeline on first use and flushes the
        // glyph batch on destruction, so neither is done by hand here.
        auto context = PaintContext {pass, sprites, *glyphs, *atlas, area, 1.f};

        auto overlay = EditorOverlay {};
        overlay.cursor = cursor;
        overlay.caretVisible = cursor != nullptr;
        overlay.matches = matches;
        overlay.currentMatch = currentMatch;

        renderer->draw(context, document, overlay, highlighter.get(), area, 0.f);
    }

    TextTheme theme;
    Document document;
    OwningPointer<SyntaxHighlighter> highlighter;
    const Cursor* cursor = nullptr;

    const eacp::Vector<SearchMatch>* matches = nullptr;
    int currentMatch = -1;

    OwningPointer<Text::GlyphAtlas> atlas;
    std::optional<TextRenderer> renderer;
    std::optional<Text::GlyphRenderer> glyphs;
};

Document codeSample()
{
    return Document::fromText("// a comment\n"
                              "int value = 42;\n"
                              "const char* text = \"hello\";\n"
                              "void run() { return; }\n");
}

// Quantised so antialiased glyph edges, which are the background blended with a
// text colour, do not each register as their own colour.
std::set<int> distinctColors(const Graphics::Image& image)
{
    auto colors = std::set<int> {};

    for (auto y = 0; y < image.height(); ++y)
    {
        for (auto x = 0; x < image.width(); ++x)
        {
            const auto pixel = image.at(x, y);

            // Only near-full coverage counts as "this glyph's colour".
            // Antialiased edges are the background blended toward the text and
            // would otherwise register as colours of their own.
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

int inkPixels(const Graphics::Image& image)
{
    auto total = 0;

    for (auto y = 0; y < image.height(); ++y)
        for (auto x = 0; x < image.width(); ++x)
            if (image.at(x, y).r > 0.3f || image.at(x, y).g > 0.3f)
                ++total;

    return total;
}
} // namespace

// The claim, stated as a comparison rather than an absolute count: the same
// document drawn with highlighting must come out in strictly more colours than
// without it.
//
// Deliberately relative. An absolute threshold would be a test of how many
// quantisation buckets antialiased glyph edges happen to land in, which is a
// property of the font and the theme's contrast, not of the code under test.
auto tHighlightingAddsColours =
    test("RenderIntegration/highlightingAddsColours") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto plain = EditorTestView {};
    auto colored = EditorTestView {};

    if (!plain.build() || !colored.build())
        return;

    plain.document = codeSample();
    colored.document = codeSample();

    auto syntax = makeOwned<SyntaxHighlighter>();

    if (!syntax->isValid())
        return;

    colored.highlighter = std::move(syntax);

    auto plainImage = plain.renderToImage(1.f);
    auto coloredImage = colored.renderToImage(1.f);

    check(plainImage.isValid() && coloredImage.isValid());
    check(inkPixels(plainImage) > 50);

    const auto plainColors = distinctColors(plainImage).size();
    const auto coloredColors = distinctColors(coloredImage).size();

    check(coloredColors > plainColors);
};

// Highlighting must not change *where* glyphs land — only their colour. A
// comparable amount of ink either way catches a regression where colouring
// accidentally drops or duplicates glyphs.
auto tHighlightingDoesNotMoveGlyphs =
    test("RenderIntegration/highlightingOnlyChangesColour") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto plain = EditorTestView {};
    auto colored = EditorTestView {};

    if (!plain.build() || !colored.build())
        return;

    plain.document = codeSample();
    colored.document = codeSample();

    auto syntax = makeOwned<SyntaxHighlighter>();

    if (!syntax->isValid())
        return;

    colored.highlighter = std::move(syntax);

    auto plainImage = plain.renderToImage(1.f);
    auto coloredImage = colored.renderToImage(1.f);

    check(plainImage.isValid() && coloredImage.isValid());

    const auto plainInk = inkPixels(plainImage);
    const auto coloredInk = inkPixels(coloredImage);

    check(coloredInk > plainInk / 2);
    check(coloredInk < plainInk * 2);
};

// The line-number gutter is drawn in its own colour and clipped to its own
// region, so an empty document still puts ink on screen.
auto tGutterDrawsLineNumbers = test("RenderIntegration/gutterDrawsLineNumbers") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = EditorTestView {};

    if (!view.build())
        return;

    // Blank lines: anything drawn must be the gutter's line numbers.
    view.document = Document::fromText("\n\n\n\n");

    auto image = view.renderToImage(1.f);

    check(image.isValid());
    check(inkPixels(image) > 10);
};

// --- caret, selection and hit-testing ---------------------------------------
//
// The Editor's logic is covered elsewhere; what is only observable here is
// whether the caret and selection actually reach the screen, and whether a
// click maps back to the character under it.

namespace
{
Cursor caretAt(std::size_t offset)
{
    auto cursor = Cursor {};
    cursor.moveTo(offset);
    return cursor;
}

Cursor selectionOver(std::size_t from, std::size_t to)
{
    auto cursor = Cursor {};
    cursor.anchor = from;
    cursor.head = to;
    return cursor;
}
} // namespace

// A visible caret puts ink on screen that a hidden one does not. This is what
// the blink toggles, so it has to be observable.
auto tCaretDraws = test("RenderIntegration/caretIsDrawnWhenVisible") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto shown = EditorTestView {};
    auto hidden = EditorTestView {};

    if (!shown.build() || !hidden.build())
        return;

    // A blank document, so anything drawn is the caret rather than text.
    shown.document = Document::fromText("");
    hidden.document = Document::fromText("");

    const auto cursor = caretAt(0);
    shown.cursor = &cursor;
    hidden.cursor = nullptr;

    auto shownImage = shown.renderToImage(1.f);
    auto hiddenImage = hidden.renderToImage(1.f);

    check(shownImage.isValid() && hiddenImage.isValid());
    check(inkPixels(shownImage) > inkPixels(hiddenImage));
};

// The selection band goes behind the text, so a selected region is brighter
// than the same region unselected.
auto tSelectionDraws = test("RenderIntegration/selectionIsDrawnBehindTheText") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto plain = EditorTestView {};
    auto selected = EditorTestView {};

    if (!plain.build() || !selected.build())
        return;

    plain.document = codeSample();
    selected.document = codeSample();

    const auto none = caretAt(0);
    const auto range = selectionOver(0, 12);

    plain.cursor = &none;
    selected.cursor = &range;

    auto plainImage = plain.renderToImage(1.f);
    auto selectedImage = selected.renderToImage(1.f);

    check(plainImage.isValid() && selectedImage.isValid());
    check(inkPixels(selectedImage) > inkPixels(plainImage));
};

// Clicking at a column's own x must return that column. The round trip that
// click-to-place-caret depends on, and the thing that silently drifts if the
// gutter width or padding is accounted for twice.
auto tHitTestRoundTrips =
    test("RenderIntegration/clickingAColumnReturnsThatColumn") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = EditorTestView {};

    if (!view.build())
        return;

    const auto document = Document::fromText("hello world\nsecond line");
    const auto area = Graphics::Rect {0.f, 0.f, viewWidth, viewHeight};
    const auto gutter = view.renderer->gutterWidth(document.lineCount());
    const auto lineHeight = view.renderer->lineHeight();

    for (std::size_t line = 0; line < document.lineCount(); ++line)
    {
        for (std::size_t column = 0; column <= document.line(line).size(); ++column)
        {
            const auto x =
                gutter + 8.f + view.renderer->columnToX(document.line(line), column);
            const auto y = (static_cast<float>(line) + 0.5f) * lineHeight;

            const auto offset =
                view.renderer->offsetAtPoint(document, {x, y}, area, 0.f);

            check(document.lineAt(offset) == line);
            check(document.columnAt(offset) == column);
        }
    }
};

// --- search highlighting ----------------------------------------------------
//
// Whether a hit is tinted, and whether the *current* hit is tinted differently
// from the rest, are the two questions a logic test cannot reach: the match list
// is right either way, and a renderer that ignored it — or that painted both
// colours the same — would pass everything in SearchTests.

namespace
{
// Average colour of a band of pixels, so a comparison is against the region
// rather than against whichever pixel a glyph happened to land on.
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

// The band a line of text occupies, out to a width that covers a short word.
Graphics::Rect bandOfLine(const TextRenderer& renderer,
                          const Document& document,
                          std::size_t line)
{
    const auto lineHeight = renderer.lineHeight();

    return {renderer.gutterWidth(document.lineCount()) + 8.f,
            static_cast<float>(line) * lineHeight + 2.f,
            40.f,
            lineHeight - 4.f};
}

Document repeatedWordSample()
{
    // One occurrence per line, so each hit can be looked at on its own without
    // working out where in a line it landed.
    return Document::fromText("target one\n"
                              "target two\n"
                              "target three\n");
}
} // namespace

auto tMatchesAreTinted = test("RenderIntegration/searchHitsAreTinted") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto plain = EditorTestView {};
    auto found = EditorTestView {};

    if (!plain.build() || !found.build())
        return;

    plain.document = repeatedWordSample();
    found.document = repeatedWordSample();

    auto query = SearchQuery {};
    query.text = "target";

    const auto matches = findMatches(found.document, query);

    check(matches.size() == 3);

    found.matches = &matches;
    found.currentMatch = -1;

    const auto plainImage = plain.renderToImage(1.f);
    const auto foundImage = found.renderToImage(1.f);

    check(plainImage.isValid() && foundImage.isValid());

    const auto band = bandOfLine(*plain.renderer, plain.document, 0);

    const auto before = averageOver(plainImage, band);
    const auto after = averageOver(foundImage, band);

    // The highlight is a warm fill, so red rises and blue falls. Asserting on
    // both directions rather than on brightness alone: a band that simply got
    // lighter could be the text being drawn twice.
    check(after.r > before.r + 0.05f);
    check(after.b < before.b + 0.02f);
};

// Two hits, one of them current, compared against *each other* in the same
// image. That is what makes this a test of the distinction rather than of
// either colour: it fails if both are painted the same, and it cannot be
// satisfied by the theme being changed to make hits brighter overall.
//
// The margin is deliberately coarse. An 8-bit drawable makes "slightly
// different" free — see PLAN.md §9 — and the two colours here differ in red by
// 0.27, so a tenth is comfortably clear of both quantisation and the glyphs
// drawn on top.
auto tCurrentMatchIsDistinct =
    test("RenderIntegration/theCurrentHitIsTintedDifferentlyFromTheRest") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = EditorTestView {};

    if (!view.build())
        return;

    view.document = repeatedWordSample();

    auto query = SearchQuery {};
    query.text = "target";

    const auto matches = findMatches(view.document, query);

    view.matches = &matches;
    view.currentMatch = 1; // the hit on the second line

    const auto image = view.renderToImage(1.f);

    check(image.isValid());

    const auto ordinary =
        averageOver(image, bandOfLine(*view.renderer, view.document, 0));
    const auto current =
        averageOver(image, bandOfLine(*view.renderer, view.document, 1));

    check(current.r > ordinary.r + 0.1f);
};

// The arrangement the app is *always* in while searching, and the one no other
// test here builds: finding a hit selects it, so the current match and the
// selection cover exactly the same characters.
//
// Drawn in the wrong order the selection covers the current-match colour, and
// the hit being looked at comes out the same blue as any ordinary selection —
// the distinct colour existing but never once being visible. Every other test
// in this file passes either way, because none of them sets a cursor and a
// match list at the same time. This one was written after seeing it on screen.
auto tCurrentMatchSurvivesItsOwnSelection =
    test("RenderIntegration/theCurrentHitStaysAHitWhenItIsAlsoTheSelection") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = EditorTestView {};

    if (!view.build())
        return;

    view.document = repeatedWordSample();

    auto query = SearchQuery {};
    query.text = "target";

    const auto matches = findMatches(view.document, query);

    view.matches = &matches;
    view.currentMatch = 1;

    // Exactly what findNext leaves behind: the second hit, selected.
    const auto selected = selectionOver(matches[1].start, matches[1].end);
    view.cursor = &selected;

    const auto image = view.renderToImage(1.f);

    check(image.isValid());

    const auto ordinary =
        averageOver(image, bandOfLine(*view.renderer, view.document, 0));
    const auto current =
        averageOver(image, bandOfLine(*view.renderer, view.document, 1));

    // Still the warm colour, not the selection's blue. Both halves matter: the
    // selection is bluer than the hit colour, so testing red alone could be
    // satisfied by a wash that happened to be bright.
    check(current.r > ordinary.r + 0.1f);
    check(current.r > current.b);
};

// A hit scrolled off the top must not be drawn against the first visible line.
// The bug this rules out is a highlight loop that indexes by match rather than
// by document position, which looks right until the file is scrolled.
auto tMatchesFollowTheScroll =
    test("RenderIntegration/hitsAreDrawnWhereTheTextIsNotWhereTheyAreInTheList") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = EditorTestView {};

    if (!view.build())
        return;

    // Only the last line holds the word, so nothing should be tinted while the
    // document is drawn from the top.
    view.document = Document::fromText("nothing here\n"
                                       "nor here\n"
                                       "target\n");

    auto query = SearchQuery {};
    query.text = "target";

    const auto matches = findMatches(view.document, query);

    check(matches.size() == 1);

    view.matches = &matches;
    view.currentMatch = 0;

    const auto image = view.renderToImage(1.f);

    check(image.isValid());

    const auto firstLine =
        averageOver(image, bandOfLine(*view.renderer, view.document, 0));
    const auto thirdLine =
        averageOver(image, bandOfLine(*view.renderer, view.document, 2));

    check(thirdLine.r > firstLine.r + 0.1f);
};

// A click past the end of a line lands at its end rather than wrapping onto the
// next one or running off the document.
auto tHitTestClamps = test("RenderIntegration/clicksOutsideTheTextClamp") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = EditorTestView {};

    if (!view.build())
        return;

    const auto document = Document::fromText("short\nlonger line here");
    const auto area = Graphics::Rect {0.f, 0.f, viewWidth, viewHeight};

    // Far right of the first line.
    const auto right = view.renderer->offsetAtPoint(
        document, {viewWidth * 4.f, view.renderer->lineHeight() * 0.5f}, area, 0.f);

    check(document.lineAt(right) == 0);
    check(document.columnAt(right) == document.line(0).size());

    // Above the first line, and far below the last.
    const auto above =
        view.renderer->offsetAtPoint(document, {0.f, -500.f}, area, 0.f);
    const auto below =
        view.renderer->offsetAtPoint(document, {0.f, 5000.f}, area, 0.f);

    check(above == 0);
    check(document.lineAt(below) == document.lineCount() - 1);
};
