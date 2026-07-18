#include "Common.h"

#include <ECodeRender/TextRenderer.h>
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
        batch.emplace();
        batch->setViewportSize({viewWidth, viewHeight});

        return true;
    }

    void render(GPU::Frame& frame) override
    {
        auto pass = frame.beginPass({theme.background});

        if (!renderer || !atlas || !batch)
            return;

        const auto area = Graphics::Rect {0.f, 0.f, viewWidth, viewHeight};

        auto sprites =
            Sprites::SpriteRenderer {{viewWidth, viewHeight}, sampleCount()};
        sprites.begin(pass);

        renderer->prepare(document, area, 0.f);
        atlas->commit();

        if (highlighter != nullptr)
            highlighter->update(document,
                                renderer->firstVisibleLine(0.f),
                                renderer->lastVisibleLine(document, area, 0.f));

        renderer->draw(
            pass, sprites, *batch, document, highlighter.get(), area, 0.f, 1.f);
    }

    TextTheme theme;
    Document document;
    OwningPointer<SyntaxHighlighter> highlighter;

    OwningPointer<Text::GlyphAtlas> atlas;
    std::optional<TextRenderer> renderer;
    std::optional<GlyphBatch> batch;
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
