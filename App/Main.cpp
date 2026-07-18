#include <eacp/GPU/GPU.h>
#include <eacp/Graphics/Graphics.h>
#include <eacp/Sprites/Sprites.h>

#include <algorithm>
#include <optional>

using namespace eacp;

namespace ecode
{

// M0 skeleton: proves the window, the Metal surface and the sprite path are all
// wired before any editor code exists. The real editor view replaces this.
struct EditorView final : GPU::GPUView
{
    EditorView()
    {
        // Text is grayscale-antialiased in the glyph atlas, so multisampling the
        // whole surface buys nothing and costs bandwidth. eacp defaults to 4.
        setSampleCount(1);
        setHandlesMouseEvents(true);
    }

    void resized() override
    {
        GPUView::resized();

        const auto bounds = getLocalBounds();

        if (bounds.w > 0 && bounds.h > 0)
            sprites.emplace(Graphics::Point {bounds.w, bounds.h}, sampleCount());

        repaint();
    }

    // Logical points -> render-target pixels, which is what setScissorRect takes.
    Graphics::Rect toPixels(const Graphics::Rect& rect) const
    {
        const auto scale = backingScale();
        return {rect.x * scale, rect.y * scale, rect.w * scale, rect.h * scale};
    }

    void render(GPU::Frame& frame) override
    {
        auto pass = frame.beginPass({background});

        if (!sprites)
            return;

        sprites->begin(pass);

        // Placeholder chrome, in the proportions the real layout will use:
        // activity bar, sidebar, status bar.
        const auto bounds = getLocalBounds();

        sprites->fillRect({0.f, 0.f, activityBarWidth, bounds.h}, activityBar);
        sprites->fillRect({activityBarWidth, 0.f, sidebarWidth, bounds.h}, sidebar);

        // Scissor exercise, standing in for the file tree's scroll viewport: the
        // rows are deliberately wider and taller than the sidebar, so anything
        // reaching the editor pane means clipping is not working.
        const auto viewport =
            Graphics::Rect {activityBarWidth, 0.f, sidebarWidth, bounds.h - statusBarHeight};

        pass.setScissorRect(toPixels(viewport));

        for (auto row = 0; row < rowCount; ++row)
        {
            const auto y = viewport.y + scrollOffset + (float) row * rowHeight;

            sprites->fillRect({viewport.x + 12.f, y + 3.f, sidebarWidth * 1.6f, rowHeight - 6.f},
                              row % 2 == 0 ? rowEven : rowOdd);
        }

        pass.clearScissorRect();

        sprites->fillRect({0.f, bounds.h - statusBarHeight, bounds.w, statusBarHeight},
                          statusBar);
    }

    void mouseWheel(const Graphics::MouseEvent& event) override
    {
        // A trackpad reports points and can be applied directly; a notched wheel
        // reports lines, which only this view can convert, since only it knows
        // what a line is worth.
        const auto points = event.preciseScrolling ? event.delta.y
                                                   : event.delta.y * rowHeight;

        const auto viewportHeight = getLocalBounds().h - statusBarHeight;
        const auto contentHeight = (float) rowCount * rowHeight;
        const auto lowest = std::min(0.f, viewportHeight - contentHeight);

        scrollOffset = std::clamp(scrollOffset + points, lowest, 0.f);
        repaint();
    }

    static constexpr float activityBarWidth = 48.f;
    static constexpr float sidebarWidth = 240.f;
    static constexpr float statusBarHeight = 22.f;

    static constexpr auto background = Graphics::Color {0.118f, 0.125f, 0.149f};
    static constexpr auto activityBar = Graphics::Color {0.094f, 0.098f, 0.118f};
    static constexpr auto sidebar = Graphics::Color {0.102f, 0.110f, 0.129f};
    static constexpr auto statusBar = Graphics::Color {0.180f, 0.192f, 0.235f};
    static constexpr auto rowEven = Graphics::Color {0.400f, 0.600f, 0.900f};
    static constexpr auto rowOdd = Graphics::Color {0.900f, 0.500f, 0.350f};

    static constexpr int rowCount = 40;
    static constexpr float rowHeight = 24.f;

    float scrollOffset = 0.f;
    std::optional<Sprites::SpriteRenderer> sprites;
};

Graphics::WindowOptions windowOptions()
{
    auto options = Graphics::WindowOptions {};

    options.width = 1200;
    options.height = 800;
    options.minWidth = 480;
    options.minHeight = 320;
    options.title = "ECode";
    options.backgroundColor = EditorView::background;

    return options;
}

struct App
{
    App() { window.setContentView(view); }

    EditorView view;
    Graphics::Window window {windowOptions()};
};

} // namespace ecode

int main()
{
    return eacp::Apps::run<ecode::App>();
}
