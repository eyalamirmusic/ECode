#include <ECodeEditor/CodeEditorView.h>

#include <NanoTest/NanoTest.h>

#include <eacp/GPU/GPU.h>

// The embeddable view, drawn.
//
// The claim under test is the one a consumer is actually buying: construct the
// thing, give it text and a size, and text appears — no atlas to build, no
// sprite renderer to size, no prepare/commit/paint ordering to get right. All of
// that used to live in App/Main.cpp, so nothing but the app could prove it
// worked; these draw off-screen through the class itself.
//
// Deliberately links neither ECodeWorkbench nor ECodeSyntax, which is the other
// half of the claim: this file compiling and linking is what says an embedder
// does not pay for tabs, the palette or tree-sitter.
//
// Self-skips without a GPU device or a usable monospace font, the same way every
// other render test here does.

using namespace nano;
using namespace eacp;
using namespace ecode;

namespace
{
constexpr auto viewWidth = 420.f;
constexpr auto viewHeight = 200.f;

int inkPixels(const Graphics::Image& image)
{
    auto total = 0;

    for (auto y = 0; y < image.height(); ++y)
        for (auto x = 0; x < image.width(); ++x)
            if (image.at(x, y).r > 0.3f || image.at(x, y).g > 0.3f)
                ++total;

    return total;
}

std::string codeSample()
{
    return "int main()\n"
           "{\n"
           "    return 0;\n"
           "}\n";
}
} // namespace

auto tDrawsItsText =
    test("CodeEditorView/drawsTextWithNoSetupBeyondTheConstructor") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = CodeEditorView {};

    view.setBounds({0.f, 0.f, viewWidth, viewHeight});
    view.setText(codeSample());

    const auto image = view.renderToImage(1.f);

    if (!image.isValid())
        return;

    check(inkPixels(image) > 50);
};

// An empty document draws the background and the one gutter number, so it must
// come out with far less ink than a file with four lines in it. Stated as a
// comparison rather than a threshold: an absolute count would be a test of the
// font's stroke weight rather than of the view.
auto tEmptyDrawsLess =
    test("CodeEditorView/anEmptyDocumentDrawsLessThanAFullOne") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto empty = CodeEditorView {};
    auto full = CodeEditorView {};

    for (auto* view: {&empty, &full})
        view->setBounds({0.f, 0.f, viewWidth, viewHeight});

    full.setText(codeSample());

    const auto emptyImage = empty.renderToImage(1.f);
    const auto fullImage = full.renderToImage(1.f);

    if (!emptyImage.isValid() || !fullImage.isValid())
        return;

    check(inkPixels(fullImage) > inkPixels(emptyImage));
};

// Read-only is a property of *input*, not of drawing: the same file must come
// out identical either way. A flag that reached the renderer — greying the text,
// hiding the caret's row — would be a different feature, and a host showing a
// file for reference would find it looked wrong rather than merely uneditable.
auto tReadOnlyDrawsTheSame =
    test("CodeEditorView/readOnlyDrawsTheSameAsEditable") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto editable = CodeEditorView {};
    auto readOnly = CodeEditorView {};

    for (auto* view: {&editable, &readOnly})
    {
        view->setBounds({0.f, 0.f, viewWidth, viewHeight});
        view->setText(codeSample());
    }

    readOnly.setReadOnly(true);

    const auto editableImage = editable.renderToImage(1.f);
    const auto readOnlyImage = readOnly.renderToImage(1.f);

    if (!editableImage.isValid() || !readOnlyImage.isValid())
        return;

    check(inkPixels(editableImage) == inkPixels(readOnlyImage));
};

// The theme is held by value inside the renderer, so setting one after the
// renderer exists is the case that silently draws the old palette: the atlas is
// still valid, the font has not moved, and nothing else would rebuild it. See
// CodeEditorView::setTheme.
auto tThemeAppliesAfterFirstFrame =
    test("CodeEditorView/aThemeSetAfterTheFirstFrameIsDrawn") = []
{
    if (!GPU::Device::shared().isValid())
        return;

    auto view = CodeEditorView {};

    view.setBounds({0.f, 0.f, viewWidth, viewHeight});
    view.setText(codeSample());

    // Draws once, which is what builds the atlas and the renderer.
    if (!view.renderToImage(1.f).isValid())
        return;

    auto light = TextTheme {};
    light.background = {1.f, 1.f, 1.f};
    light.text = {0.f, 0.f, 0.f};

    view.setTheme(light);

    const auto image = view.renderToImage(1.f);

    if (!image.isValid())
        return;

    // The corner is background, well clear of the gutter and any glyph.
    const auto corner = image.at(image.width() - 2, image.height() - 2);

    check(corner.r > 0.8f && corner.g > 0.8f && corner.b > 0.8f);
};
