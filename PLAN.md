# ECode — a GPU-drawn, VSCode-style code editor on eacp

**Status:** a working editor. Opens a file, highlights it with tree-sitter,
scrolls, and can be typed in — with selection, undo, clipboard and mouse.
Sections 1–5 are the design and the research behind it; **§6 is where things
stand and §7 is what to do next.**

## Decisions taken

- **VSCode-like, not Vim-like.** Modeless editing, mouse-first, standard chords, multi-cursor,
  command palette, sidebar tree, tabs, panel, status bar.
- **No webview.** Every pixel is drawn by us on the GPU.
- **macOS first**, Windows later — but the glyph-raster seam stays abstract from day one.
- **Shared text stack**: the glyph atlas + cell renderer get promoted into eacp as a new
  `eacp-text` module, consumed by both ECode and CowTerm.
- **Milestone 1 is a fast read-only viewer** — open, highlight, scroll at frame rate. Editing
  lands after the render core is proven.

---

## 1. The central architectural call

**One `GPU::GPUView` for the entire window, with our own widget tree inside it.**

Not one `Graphics::View` per widget. This is forced by three findings:

1. Every `Graphics::View` is backed by a real `NSView` (`View-macOS.mm:285` sets
   `wantsLayer = YES`). A file tree with 5,000 rows would be 5,000 NSViews.
2. `Graphics::View` chrome is composited by CoreAnimation while text is drawn by Metal — two
   pipelines, two vsync paths, and cross-boundary z-order becomes CALayer ordering rather than
   GPU z-order.
3. `GPUView::paint()` is `final`, so a GPU view cannot also use the `Graphics::Context` API.
   The two drawing models are disjoint by construction.

The precedent to read before designing: `Cameras::CameraView`, which renders its content and then
calls a `virtual void drawOverlay(Sprites::SpriteRenderer&, const Rect&)` hook **in the same render
pass** (`Apps/Mixed/MixedViews/Main.cpp` subclasses it). That is exactly the "chrome + embedded
custom text view" shape we need. For hand-rolled widgets with hover/press states and manual
layout, read `Apps/Graphics/ComplexGUI/TaskBoard.cpp`.

`Graphics::View` still gets used for the **window's root** and for genuinely native things (menu
bar, tray). It is not used for in-window UI.

---

## 2. Repository scaffold

Follows the `EACPExamples` house style — vendored CPM, then a `Find*.cmake` per dependency that
wraps `CPMAddPackage`.

```
ECode/
  CMakeLists.txt
  CMake/
    CPM.cmake               # vendored verbatim, CPM 0.40.2 (match eacp)
    FindEACP.cmake
    FindTreeSitter.cmake
    ECodeWarnings.cmake
  Lib/
    ECodeCore/              # buffer, edits, undo, selections — no GPU, no platform
    ECodeUI/                # widget tree, layout, theme
    ECodeRender/            # instanced glyph renderer, atlas client
  App/
    Main.cpp
    CMakeLists.txt
  Tests/                    # NanoTest
  justfile
```

`CMake/FindEACP.cmake`:

```cmake
include(CPM)

CPMAddPackage(
        NAME eacp
        GITHUB_REPOSITORY eyalamirmusic/eacp
        GIT_TAG main
        OPTIONS
            "EACP_ENABLE_EXAMPLES OFF"
            "EACP_ENABLE_TESTS OFF"
            "EACP_BUILD_WEBVIEW OFF")
```

Turning `EACP_BUILD_WEBVIEW OFF` is deliberate — we never use it, and it drops the WebView2 /
WKWebView surface and the Vite/npm codegen path from the build entirely.

Root `CMakeLists.txt` — note the standalone-bootstrap block, which is the part that is easy to get
wrong. eacp runs `eacp_default_setup()` **only when it is the root project**, so as the root we
must reproduce the pieces we depend on:

```cmake
cmake_minimum_required(VERSION 3.31)
project(ECode VERSION 0.1.0 LANGUAGES C CXX)

if (APPLE)
    enable_language(OBJCXX)
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if (MSVC)
    add_compile_options(/EHsc)   # must precede the CPM packages
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

include(CPM)
find_package(EACP REQUIRED)
find_package(TreeSitter REQUIRED)

# eacp appends its helper dir to CMAKE_MODULE_PATH only inside its own scope.
list(APPEND CMAKE_MODULE_PATH "${eacp_SOURCE_DIR}/CMake")

# eacp_setup_apple is a macro keyed off CMAKE_CURRENT_SOURCE_DIR, so it cannot be
# reused verbatim from here; point the plist var straight at eacp's template.
if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
    set(EACP_MACOS_PLIST "${eacp_SOURCE_DIR}/CMake/macOSBundleInfo.plist.in"
            CACHE INTERNAL "eacp macOS bundle Info.plist template")
endif ()

add_subdirectory(Lib)
add_subdirectory(App)
```

`App/CMakeLists.txt` tail — the helper call order matters:

```cmake
target_link_libraries(ECode PRIVATE ECodeCore ECodeUI ECodeRender eacp-sprites)

res_embed_add(ECode DIRECTORY Resources)     # JetBrains Mono, themes, icons

set(BUNDLE_ID "com.eacp.ecode")
set_target_properties(ECode PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "ECode"
        MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_ID}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_ID})

eacp_set_gui_subsystem(ECode)     # WIN32_EXECUTABLE + /ENTRY:mainCRTStartup
set_default_target_setting(ECode) # warnings, LTO in Release, stamps the plist
eacp_set_app_icon(ECode IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/Icon.png")
```

Local iteration against a checkout: `-DCPM_eacp_SOURCE=/Users/eyalamir/Code/eacp`.
Always configure with `-DEACP_UNITY_BUILD=OFF` so `compile_commands.json` is per-file and LSP
tooling stays accurate.

---

## 3. Upstream work in eacp (blocking, do first)

These are framework gaps, not app gaps. Each blocks something structural, and each is small
relative to the app work it unblocks. Ordered by how hard they block.

Every framework change ships with **unit tests and a live example**. Tests go under `Tests/<Module>`
(NanoTest, registered in that directory's `CMakeLists.txt`); examples go under `Apps/<Module>/<Name>`
and are added to `Apps/GPU/CMakeLists.txt` et al. GPU state with no CPU-side observable — a scissor
rect returns nothing and can't be queried — is tested by rendering off-screen through
`View::renderToImage(scale)` and reading the pixels back, which is what `Tests/GPU/GPUSnapshotTests.cpp`
already does.

Two lessons worth carrying.

**Check that a new test fails without the change.** The first version of the `scrollWheel:`
registration test passed either way, because `respondsToSelector:` is satisfied by `NSView`'s own
inherited implementation. It only became a real test once it compared the resolved method against
the *immediate superclass's*. Same discipline caught nothing wrong in the texture-region suite,
but confirmed it: forcing the upload origin to (0,0) fails exactly the one test that asserts a
second region leaves the first alone.

**Build for iOS before calling anything done.** `APPLE` is true on iOS, so
`if (APPLE)` in a `CMakeLists.txt` pulls macOS-only sources into the iOS build — that is how the
`ScrollWheelTests-macOS.mm` AppKit include broke it. The guard is `if (APPLE AND NOT IOS)`. The CI
invocation, worth running locally on anything touching `Graphics`, `GPU` or a `CMakeLists.txt`:

```bash
cmake -G Xcode -B build-ios -DCMAKE_SYSTEM_NAME=iOS \
      -DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_ARCHITECTURES=arm64 \
      -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO -DEACP_CI_BUILD=ON
cmake --build build-ios --config Debug -- -sdk iphonesimulator
```

Note `EACP_CI_BUILD=ON` turns unity builds on, which changes what compiles together — a source
file can be fine alone and break in a unity blob. Existing gating to copy: `Tests/GPU` is already
excluded on iOS (`AND NOT IOS`), and `Lib/eacp/Video` likewise.

**Done so far**, on the eacp branch `ecode-editor-support` (497 eacp tests pass, up from 469):

- **Gap 1 — scissor rects.** `RenderPass::setScissorRect(Rect)` / `clearScissorRect()`, in
  render-target pixels, top-left origin, clamped to the target so a partly-scrolled-off region
  clips instead of tripping Metal's API validation. Metal and D3D12 backends both done; `Frame`
  now passes the target's pixel size into the pass. Verified in ECode: rows drawn 1.6× the
  sidebar width are cut exactly at its edge.
- **Gap 2 — macOS scroll wheel.** `scrollWheel:` registered on the backing view. Uses
  `scrollingDelta*` (not `delta*`, which is already quantised back to whole lines and loses
  trackpad smoothness), and adds `MouseEvent::preciseScrolling` plus a `ScrollPhase` enum
  covering gesture phase *and* momentum. Verified end-to-end with synthesized events.
- **Gap 8 — backing scale.** `GPUView::backingScale()` now public, plus an
  `onBackingScaleChanged` callback. The real bug behind this one:
  `viewDidChangeBackingProperties` updated only the native layer's `contentsScale` and never told
  the C++ side, so a `CAMetalLayer`'s `drawableSize` — and any glyph atlas — silently kept the old
  display's scale. Added `View::backingScaleChanged()` as the virtual hook and routed it.
- **Bonus — `Graphics::Color` is now `constexpr`** (inline in the header, out-of-line bodies
  removed). A theme is a table of named colors; it should live in rodata, not run at static-init.

Tests and example added alongside:
- `Tests/GPU/ScissorTests.cpp` — 8 tests, off-screen render + pixel readback: clips on both axes,
  clamps an out-of-bounds rect, discards on empty and fully-outside rects, restores on clear, and
  confirms the rect is in pixels rather than points.
- `Tests/GPU/BackingScaleTests.cpp` — 7 tests: scale usable before layout, `bounds * scale` equals
  the rendered pixel size, a resize at unchanged scale does *not* notify, and the hook is virtual
  on the base `View`.
- `Tests/Graphics/ScrollWheelTests.cpp` (+ `-macOS.mm`) — 8 tests: routing to the view under the
  cursor, view-local coordinates, precise/phase fields surviving the trip, wheel ignoring
  mouse-down capture, and the native class implementing `scrollWheel:` itself.
- `Tests/Graphics/ColorTests.cpp` — 5 tests plus `static_assert`s that fail the *build* if a
  `Color` definition moves back out of line.
- `Tests/GPU/TextureRegionTests.cpp` — 8 tests. There is no texture read-back API, so these get
  one: draw the texture 1:1 into an off-screen `GPUView` with Nearest sampling and read *that*
  back, one texel per pixel. Covers origin placement, leaving the rest untouched, region height,
  source stride, out-of-bounds rejection, empty/null no-ops.
- `Apps/GPU/Clipping` — two independently scrollable panes whose rows are drawn wider than their
  pane, so the scissor rect is what keeps them apart.
- `Apps/GPU/TextureAtlas` — a 512² atlas filled one 16×16 tile at a time, the glyph-atlas pattern.
  Logs the running totals: after 192 tiles, 192 KB uploaded by region versus 196,608 KB had each
  tile re-sent the whole atlas.

Also added since: `Files::writeFileAtomically` (temp sibling plus rename,
keeping permissions and following symlinks) and `File::modificationTime`, which
together are what saving a file safely needs. 10 tests in
`Tests/Core/FilesTests.cpp`; the live example is ECode's Cmd+S.

Still open: gap 3 (IME), 7 (cursor shapes), 9 (UTF-8 helpers), 10 (file watching).

| # | Gap | Why it blocks | Shape of fix |
|---|-----|---------------|--------------|
| 1 | **No scissor/clip anywhere.** `RenderPass` has no `setScissorRect` or `setViewport`; `Graphics::Context` has no clip method. | Every scrolling region — editor viewport, file tree, minimap, dropdowns, panel — needs it. | Add `setScissorRect` to `RenderPass`. Metal `setScissorRect:` / D3D12 `RSSetScissorRects`. Both backends already exist. Contained, ~an afternoon. |
| 2 | **macOS scroll wheel is never delivered.** `MouseEventType::Wheel` exists and dispatches, but a repo-wide grep for `scrollWheel` returns zero hits; the only producer is the Windows path. | No scrolling on the primary target platform. | Implement `scrollWheel:` on the macOS backing view — **plus** momentum phase, precise deltas, and rubber-band state. None of that is plumbed. |
| 3 | **No IME / composition.** No `NSTextInputClient`, no `interpretKeyEvents:`, no `WM_IME_*`. | CJK input, dead keys (`Option+e` → é), and the emoji picker are all broken. Cannot be layered on from app code. | Implement `NSTextInputClient` on the macOS backing view: marked-text range, composition callbacks, candidate-window rect. Real Objective-C++ work; the largest of these. |
| 4 | ~~**Clipboard is copy-only.**~~ — **done.** | Cmd+V. | `getText`/`hasText` added across all four backends. Named to match `jamierpond/eacp`, which had it first. |
| 5 | ~~**`Texture::update()` re-uploads the whole texture**~~ — **done.** | One new glyph cost a full-atlas upload. CowTerm eats 16 MB per new glyph. | `update(region, pixels, bytesPerRow)` added. Metal `replaceRegion:` at an offset; D3D12 asks `GetCopyableFootprints` at the *region's* size and places the copy with `CopyTextureRegion`. Out-of-bounds is **dropped, not clamped** — a clamped region keeps consuming source rows at the original width and silently uploads skewed pixels. |
| 6 | ~~**Keycode table is incomplete.**~~ — **done.** Punctuation, Home/End/PageUp/PageDown, forward delete and the keypad added, with the Windows OEM mapping. Originally: No punctuation, brackets, semicolon, quote, slash, backslash, minus, equals; no Home/End/PageUp/PageDown/Insert; no keypad. | An editor needs all of these, everywhere. CowTerm works around it with hand-defined macOS raw virtual keycodes — a portability landmine we should not inherit. | Extend `KeyCode` and both platform translation tables. |
| 7 | **No cursor-shape API.** Only `NSCursor` hide/unhide for mouse lock. | I-beam over text, col-resize over splitters, pointer over links. | Per-View cursor + `NSTrackingArea` / `cursorUpdate:`. |
| 8 | **Backing scale is not publicly readable.** `platformBackingScale` is internal. | Glyphs must rasterize at the true device scale, and re-rasterize when the window moves between Retina and non-Retina displays. CowTerm captures scale once at atlas construction and never updates it. | Expose the accessor + a `onBackingScaleChanged` hook. |
| 9 | **No UTF-8 support in `Strings`.** No codepoint iteration, no grapheme clusters, no width tables. | Cursor movement, selection, backspace all operate on graphemes, not bytes. | Either add to eacp or vendor a small UTF-8/grapheme library into `ECodeCore`. |
| 10 | **No file watching, no directory enumeration.** | File tree, external-change detection. | FSEvents on macOS; app-level is acceptable initially — ECode polls `File::modificationTime` once a second, which is the placeholder this replaces. |

**Not a gap, contrary to first impressions:** instanced rendering is first-class —
`ShaderProgram::instanceInput(&Instance::field, slot)` + `RenderPass::drawInstanced()`, with a
1000-instance worked example at `Apps/GPU/Instancing/Main.cpp`. `Sprites::SpriteRenderer` happens
not to use it (one draw call per quad), but the machinery underneath is good. We write our own
instanced glyph shader rather than fixing `SpriteRenderer`.

---

## 4. `eacp-text` — the new shared module

Promoted out of CowTerm, generalized for proportional text and ligatures, consumed by both apps.

CowTerm's `GlyphAtlas.h` is the right seam and should be kept: `(codepoint, bold, italic) →
GlyphSlot` plus one `GPU::Texture&` and scalar metrics, with the platform split done by a `Pimpl`
whose implementation CMake selects — no virtual interface. Three changes:

- **`GlyphSlot` gains bearing and advance.** Today it is `{src, colored, valid}`, which is why
  CowTerm can only do a monospace grid. Ligatures and proportional UI text need per-glyph
  positioning.
- **A shaping pass.** CowTerm maps codepoint→glyph one at a time and shapes nothing. Ligatures
  (Fira Code, JetBrains Mono) and any complex script need real shaping — CoreText line shaping on
  macOS, DirectWrite on Windows, behind one interface.
- **Grayscale atlas in `R8Unorm`**, which eacp documents as the natural mask format — ¼ the memory
  of CowTerm's RGBA8. Color emoji go to a second RGBA8 atlas.

Keep from CowTerm as-is, all verified good:
- Grayscale AA, subpixel/LCD AA explicitly **off** on both platforms
  (`CGContextSetShouldSmoothFonts(false)`, `D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE`) — the atlas must
  be tintable, so ClearType would bake subpixel colour into it.
- White-RGB + coverage-in-alpha storage, straight (un-premultiplied), so one atlas entry tints to
  any colour.
- The **prepass**: rasterize every glyph the frame needs, then force the single texture upload,
  *then* issue draws. Without it a mid-frame `update()` mutates a texture already bound by earlier
  draws in the same pass.
- The DPI convention: atlas rasterizes in **device pixels**, everything else is in logical points,
  accessors divide the scale back out.

Fix, don't inherit:
- **Atlas eviction.** CowTerm has none — it flushes the whole atlas when full, and `glyph()`
  returns a reference into a map that a later reset invalidates. A 2048² atlas rarely wraps for a
  terminal at one size; an editor with multiple sizes and weights hits it much sooner. Needs LRU
  and stable handles.
- **Gamma-correct blending.** Absent in CowTerm; coverage alpha is blended in whatever space the
  drawable is in. This is the most likely visual-quality gap versus native text, and it shows
  worst on light-on-dark, which is the default theme.

### Status: the module exists

`eacp-text` is in place — `Lib/eacp/Text`, target `eacp-text`, linking `eacp-gpu`:

- `Font.h` — `FontStyle` (the four faces a code editor switches between mid-line),
  `FontRequest` (points + scale, so the rasterizer works in pixels and callers in points),
  `FontMetrics`.
- `GlyphBitmap.h` — pixels plus **bearings and advance**, the thing CowTerm's `GlyphSlot` lacked
  and the reason it could only ever draw a fixed grid.
- `GlyphRasterizer` — the only platform-specific file (`-Apple.mm`; CoreText works on iOS too, so
  it is not named `-macOS`). Rasterizes each glyph into a bitmap sized to its own bounding box.
- `ShelfPacker` — pure arithmetic, no GPU, no fonts.
- `GlyphAtlas` — cache, packing, growth, dual mask/colour pages, incremental upload.

**The structural change worth keeping:** the rasterizer sits behind a `GlyphSource` interface, so
the atlas is driven by a stub in tests. CowTerm fuses rasterizer and atlas into one Pimpl, which
is why its two backends duplicate packing, blitting and upload line-for-line across 867 lines, and
why none of it could be tested without a real font.

Two deviations from what this section originally said:

- **Grow, don't evict.** The plan called for LRU with stable handles. What shipped doubles the
  atlas up to a cap, keeping every existing placement — a shelf only ever extends right and down,
  so nothing is re-rasterized and no slot goes stale. Only at the cap does it clear, and
  `generation()` ticks so callers notice. This is what Ghostty does, and LRU can wait for a
  profile that asks for it.
- **Slots are returned by value.** CowTerm returns a reference into its cache that a reset
  invalidates — a dangling read waiting for the first time the atlas fills. A slot is four floats
  and two flags.

### What Ghostty and Alacritty actually do

Read from both source trees rather than from blog posts, and it corrects two things I assumed
earlier.

**Do not do subpixel positioning.** I had assumed it was near-universal. It isn't: neither
terminal does it. Ghostty's cache key is a packed u64 of `{index, glyph, opts}` with no fractional
field; Alacritty's is `{character, font_key, size}`. On a fixed monospace grid, integer cell
origins mean every occurrence of a glyph shares one subpixel phase, so consistency costs **one**
atlas entry instead of four. Zed pays for four variants because GPUI renders proportional UI text
— a code grid doesn't have that problem. Start with integer cell snapping.

**Grayscale everywhere, and solve gamma instead of subpixel AA.** The two projects split the hard
problems and neither solved both: Alacritty did subpixel AA via dual-source blending and ignored
gamma entirely (`with_srgb(Some(false))`); Ghostty is grayscale-only into an `r8unorm` atlas and
solved gamma. Subpixel AA also forces a permanent trade — it cannot coexist with a transparent
window background (Direct2D, WebRender, foot and kitty all hit the same wall), and macOS dropped
it in Mojave anyway. Take Ghostty's side.

Its `linear-corrected` mode is worth copying exactly: render into an sRGB target so blending is
linear, then remap the coverage alpha so the result still *looks* like the familiar sRGB blend —
`a' = (blend_l - bg_l) / (fg_l - bg_l)`. This is Skia's `SkMaskGamma` run backwards (Skia
pre-distorts the mask and blends wrong; Ghostty blends right and post-distorts the alpha).

**The constraint that dictates our shader interface:** that remap needs the **per-cell background
colour inside the text shader**. Ghostty gets it because `cell_text_vertex` reads a flat
`bg_colors[row * cols + col]` buffer. The same data path is what enables minimum-contrast later.
Design it in from the start — it is genuinely hard to retrofit.

**Backgrounds want a fullscreen pass, not instanced quads.** Ghostty moved cell backgrounds to a
flat `[4]u8` array read by a fullscreen-triangle shader, which cut GPU memory ~20% and shrank its
text instance from 56 to 32 bytes. Its floor is three draw calls per frame: clear, cell
backgrounds, then one instanced call for all glyph quads. That is a better target than CowTerm's
run-length rects.

**Atlas packing:** Ghostty uses a skyline packer starting at 512² and doubles the same texture
when full; Alacritty uses a shelf packer at 1024² and pushes a new atlas onto a vector. Growing
avoids atlas-switch batch breaks, appending avoids the realloc-and-recopy stall. Either beats
CowTerm's flush-everything.

**Damage:** Alacritty does real compositor damage (double-buffered, `swap_buffers_with_damage`);
Ghostty has only CPU-side dirty tracking. Follow Alacritty.

---

## 5. ECode's own architecture

### `ECodeCore` — no GPU, no platform, fully unit-testable

- **Text buffer.** A rope or piece table. CowTerm's `std::vector<Cell>` grid at 16 bytes/cell is
  right for an 80×24 terminal and wrong for a 100 MB file. Note `eacp::File` is a chunked bounded
  reader (`read(offset, span)`), so large-file open without a full load is available.
  `EA::CopyOnWrite` and `EA::CircularBuffer` from `cpp_data_structures` are worth evaluating for
  the undo stack.
- **Edit transactions + undo/redo.** Every mutation is a transaction; undo is a stack of inverses.
  Design this in from the start — it is very hard to retrofit.
- **Multi-cursor / multi-selection.** VSCode semantics: N cursors, each with an anchor, all edits
  applied per-cursor with offset fixup. CowTerm's single contiguous `CellRef` range and O(1)
  `isSelected` test do not generalize; plan for N selections from the beginning.
- **Logical ↔ visual line mapping.** Soft wrap, folding, and variable line heights all break the
  `row * cellH` assumption. This is the single biggest structural difference from a terminal grid,
  and the mapping layer should exist even in the read-only milestone.
- **Syntax:** tree-sitter (C library, CPM-friendly), incremental reparse on edit, styles attached
  to ranges rather than baked into cells.

### `ECodeRender`

- Instanced glyph shader written in eacp's C++ shader EDSL — shaders are C++ structs,
  MSL and HLSL and the vertex layout are all generated, and `pass.draw(shader)` binds
  pipeline + vertices + uniforms + textures in one call. No `.metal` files.
- One instance buffer of `{rect, uvRect, fgColor, flags}`; one `drawInstanced` per atlas.
- **Keep** CowTerm's run-length background coalescing — it collapses long same-colour spans into
  one quad and skips default-background cells entirely, since the pass clear already painted them.
- **Real damage tracking.** CowTerm's `changeVersion` machinery exists but its only consumer is
  write-only dead code, so every repaint redraws the whole grid. A dirty-line bitset lets us skip
  both the glyph prepass and the draw loop for unchanged rows.
- On-demand rendering (`GPUView` default) — idle submits zero GPU work. `setContinuous(true)` only
  while animating. `renderNow()` is available for the lowest-latency keystroke→glass path.
- MSAA off (`setSampleCount(1)`); text is grayscale-AA'd in the atlas already.
- Note `SpriteRenderer`'s logical size is baked at construction, so CowTerm reconstructs it on
  every resize — recompiling the shader mid-drag. Our renderer takes a settable logical size.

### `ECodeUI`

A widget tree living inside the single `GPUView`. Everything here is ours — eacp contributes a
base class and nothing else.

Available for free: `Graphics::Rect`'s JUCE-style `removeFromLeft/Right/Top/Bottom`,
`inset`, `fromLeft` … which suit IDE chrome well (activity bar `removeFromLeft(48)`, status bar
`removeFromBottom(22)`). Mouse capture already works correctly — `mouseDownTarget` is latched on
Down and all Drag/Up route to it, which is exactly what splitter dragging and text selection need.
`clickCount` is present for double/triple-click word and line selection.

Must be built: widget base + layout pass, focus traversal (none exists), scrollbar, virtualized
list, tree view, tab bar, splitter, popup/overlay, in-window context menu (`Graphics::Menu` is the
native menu bar only — no `popup(at:)`), minimap, tooltip, status bar, animation/easing.

Lift from CowTerm: `FuzzyMatch.h` (62-line header-only fzf-style scorer) and `Palette` — a command
palette almost for free. Its **peek** pattern (navigating the list live-switches the background
view, Enter commits, Esc restores) maps directly onto file-preview-on-highlight.
`SessionView`'s recursive split-pane tree maps onto editor groups.

### Config and theming

Miro reflection, exactly as CowTerm does it — the struct *is* the schema, `MIRO_REFLECT(...)`,
`Miro::fromJSONString`, unknown keys ignored and missing keys defaulted for free, five lines total.
Two changes: **themes as data** (CowTerm hardcodes them in C++) and **file watching for reload**
(CowTerm reads config once at construction).

### Keybindings

CowTerm has three unrelated mechanisms and no unified keymap — a leader-key `bool`, an if-chain on
Cmd chords, and config bindings limited to a single character with no modifier support and no way
to name a command. Do not inherit this. Build a **command registry** plus a keymap table
(`{"keys": "cmd+shift+p", "command": "workbench.showPalette", "when": "editorFocus"}`) from day
one; the palette then enumerates the registry for free.

Steal one detail: `charactersIgnoringModifiers` is the correct field for matching shortcuts (so
Cmd+C is "c" on any layout) while `characters` is the correct field for text insertion.

---

## 6. Where things stand

Everything through M4 is done and verified on screen. Commit hashes are in the
log; this is the shape of it.

**Done in eacp** (branch `ecode-editor-support`, 583 tests):
scissor rects, macOS scroll-wheel delivery with precise/momentum plumbing,
backing-scale notification, texture sub-region upload, `constexpr` colours,
`ShaderProgram::prepare`'s blend mode, clipboard `getText`/`hasText`, the
missing key codes, `Files::writeFileAtomically` and `File::modificationTime`,
and the whole `eacp-text` module — rasterizer/atlas split, per-glyph bearings,
R8 mask + RGBA colour atlases, growth over eviction, incremental upload, and
`Text::GlyphRenderer`.

**Done in ECode** (124 tests): `Document` with an incremental line index,
`TextEdit`/`EditHistory` with step grouping, `Cursor`/`Editor`, `TextRenderer`
drawing only the visible slice with clipped gutter and text, `GlyphRenderer`
batching, tree-sitter highlighting with incremental reparse, the full typing
loop — keys, mouse, selection, undo, clipboard, blink, scroll-to-caret — and
`TextFile`: the file lifecycle, saving included.

**Proven elsewhere**: CowTerm ported onto `eacp-text` (−904/+208), rendering
CJK and colour emoji correctly. That was the test of whether the extraction was
real rather than a rearrangement, and it exposed two genuine gaps —
`GlyphRenderer` missing entirely, and a clipboard API that duplicated Jamie's.

---

## 7. What to do next

Ordered by what unblocks the most, with the reasoning rather than just the list.

### 7.1 ~~Save and the file lifecycle~~ — done

`TextFile` is an `Editor` plus the file its text came from: path, dirty flag,
disk stamp, `save`/`saveOverwriting`/`reload`. Cmd+S in the app, and `ECode
<path>` so it can open something other than its own source.

Four decisions worth recording, because each went against the obvious version:

- **The dirty flag follows undo, not a change counter.** `Editor::version()`
  only counts, so undoing back to the saved text still reads as dirty. Each
  undo step now carries an id that travels onto the redo stack and back
  (`EditHistory::stateId`), so "back where I saved" is a comparison. The case
  that forced it is the one a *depth* comparison also gets wrong, and gets
  wrong in the expensive direction: save, undo, type something else — same
  depth, different text, reported clean, save silently skipped.
- **A save that would clobber someone else's write is refused, not merged.**
  `save()` returns `changedOnDisk` and writes nothing. There is no dialog to
  ask in until the widget layer exists, so the title bar carries the question
  and a second Cmd+S answers it. Replace that with a real prompt in 7.4.
- **A deleted file is not a conflict.** Nothing can be clobbered, so refusing
  would only strand the text in the buffer.
- **External changes are polled, once a second.** eacp still has no file
  watching. Clean buffer plus a changed file means the new version is simply
  taken, which is what makes a `git checkout` or a formatter run appear.
  FSEvents replaces the poll upstream later; the seam is `hasChangedOnDisk`.

Atomic writing went upstream as `Files::writeFileAtomically`, since every app
that saves anything wants it. The two things a naive temp-plus-rename loses —
an existing file's permission bits, and a symlink, which it replaces rather
than writes through — are what its tests actually pin.

**Not verified on screen.** The unsaved dot in the tab and the title-bar text
are the one part of this that no test covers, and the session it was written in
could not capture the display. Worth a look.

### 7.2 Multi-cursor — decide the data model now, even if the UI waits

This is the one piece of sequencing I would not get wrong. `Editor` holds a
single `Cursor`, and every operation on it assumes that. The longer the widget
layer, the palette and find/replace are written against a single cursor, the
more code has to change when it becomes a set.

The change itself is contained — `Cursor` becomes `Vector<Cursor>`, edits apply
per cursor from the highest offset down so earlier edits do not shift later
ones, and overlapping cursors merge after each operation. Doing it while
`Editor` is the only caller is a couple of hours; doing it after M5 is a
refactor across the app.

### 7.3 The widget layer — the big one, and the next real design decision

The chrome is still hardcoded rectangles in `drawChrome()`. Everything in 7.4
depends on this, and eacp deliberately provides none of it: `GPUWidgets` is
path tessellation, not widgets, and `Graphics::View` is one `NSView` per widget,
which a 5,000-row file tree cannot use.

What it needs: a widget base with a paint that takes the `GlyphRenderer` and
`SpriteRenderer`, a layout pass (`Rect::removeFrom*` suits IDE chrome well),
hit-testing, focus with tab traversal, and scroll containers built on
`setScissorRect`. Then the concrete widgets — scrollbar, virtualised list, tree,
tab bar, splitter, overlay panel, context menu.

Two things to get right at the start, because both are painful later:
- **Variable line height.** `TextRenderer` places row *n* at `n * lineHeight`.
  Soft wrap, folding, inline diagnostics and image lines all break that
  assumption, and it is the single biggest structural difference between a
  terminal grid and an editor. A logical-line to visual-line mapping should
  exist before anything else is built on the current assumption.
- **Damage tracking.** Every frame currently redraws everything visible. Fine
  at this size, wrong once a file tree and a minimap are also on screen.

### 7.4 IDE chrome, on top of the widget layer

File tree, editor tabs, splitters, status bar contents, find/replace, and the
command palette. CowTerm's `FuzzyMatch.h` (62 lines, header-only) and its
`Palette` peek pattern are directly liftable and MIT-licensed — the peek
behaviour, where navigating the list live-previews the highlighted item and
Escape restores, maps straight onto file preview.

A command registry should come with the palette rather than after it: bindings
name commands (`{"keys": "cmd+shift+p", "command": "workbench.showPalette"}`),
the palette enumerates the registry, and neither needs a hand-maintained list.
CowTerm's three unrelated keybinding mechanisms are the counter-example.

### 7.5 IME — the largest remaining framework gap

Still absent from eacp: no `NSTextInputClient`, no `interpretKeyEvents:`, no
`WM_IME_*`. CJK, dead keys and the emoji picker are all unavailable, and it
cannot be layered on from app code.

The implementation can wait; the **marked-text range in the cursor model should
not**. Composition means the document holds provisional text that is styled
differently and is not yet a real edit. Retrofitting that through `Editor`,
`EditHistory` and the renderer after multi-cursor and the widget layer exist is
exactly the kind of change this plan has been trying to avoid.

### 7.6 The rope, when files get big

`Document` is a `std::string` with a flat line index. The index is now repaired
incrementally, but it is still linear in line count per edit, and the string
itself makes every insert move the tail.

Deliberately deferred, and cheap to defer: the mutation API is only
`replace(start, end, text)` plus `line(i)`, so the storage can change without
the renderer or the highlighter noticing. Do it when a real file makes it hurt,
not before — and measure first, because the line index may bite sooner than the
string does.

### 7.7 Carried over, not forgotten

- **Windows renders no text.** `GlyphRasterizer-Windows.cpp` is a documented
  stub returning `isValid() == false`. The porting notes are in its header.
- **Gamma-correct blending.** Planned in M2 and not done. This is the difference
  between "looks native" and "looks slightly off", worst on light-on-dark. It
  needs the per-cell background colour plumbed into the glyph shader, which is
  the part that is hard to retrofit — see §4.
- **Shaping and ligatures.** `GlyphAtlas` maps one codepoint to one glyph.
  Fira Code's `=>` needs CoreText/DirectWrite line shaping behind the existing
  seam, plus a run cache — Ghostty measured shaping at 96% of frame time before
  adding one.
- **LSP.** `Processes::runAsync` returning `Async<T>` is the right foundation;
  diagnostics, completion and go-to-definition after the chrome exists.

---

## 8. Risks worth naming

- **eacp is self-declared alpha**: "APIs will change without notice between
  commits." ECode tracks `GIT_TAG main`, as do eacp's own four dependencies.
  Worth pinning to a SHA now that ECode depends on real behaviour rather than
  just compiling.
- **Two eacp branches have diverged.** Ours has `eacp-text`; `jamierpond/eacp
  jp/fancy-terminal` has ~22 commits we lack. The CowTerm PR is blocked on
  reconciling them, which is a coordination problem rather than a coding one.
  Duplicated work has already happened once (clipboard read).
- **eacp's README and CLAUDE.md predate the GPU stack** and describe it as Core
  Graphics only. Read the tree, not the docs.
- **`if (APPLE)` includes iOS.** It has broken the build once. The guard is
  `if (APPLE AND NOT IOS)`, and the CI invocation is in CLAUDE.md.
- **No Linux GUI path exists in eacp at all** — the whole `Graphics`/`GPU` tree
  is gated behind `(APPLE OR WIN32)`. Linux is not "later", it is a separate
  project.

---

## 9. What this project has learned about testing itself

Recorded because each of these cost something to find out.

- **Verify a new test fails without the change.** A test here passed with the
  feature deleted, because `respondsToSelector:` was satisfied by `NSView`'s
  own inherited implementation.
- **And check it fails in the direction that costs you something.** The first
  dirty-flag test picked a sequence where the naive implementation happened to
  answer correctly, so it passed against the thing it was written to rule out.
  A wrong answer has two directions and usually only one of them hurts: here,
  falsely *clean* skips the save and loses the work, while falsely dirty just
  writes a file twice. Aim the test at the expensive one.
- **Verify the mutation applied.** Two mutation checks silently no-op'd because
  clang-format had reflowed the text the string replace was looking for. Print
  whether the edit landed.
- **Test optimisations against an oracle, not against cases.** The incremental
  line index is compared to a full rebuild after every edit; the incremental
  reparse to a fresh parse. The oracle caught a real bug on its first run that
  no hand-written case covered — a trailing newline becoming interior once more
  text is typed after it.
- **GPU state with no CPU-side observable is tested by drawing.** Scissor rects,
  blend modes and glyph colour all return nothing queryable; render off-screen
  with `renderToImage` and assert on pixels.
- **Run the app.** The red-text bug — an R8 mask through a tint-multiplying
  shader — passed every test that existed and was obvious in one screenshot.
