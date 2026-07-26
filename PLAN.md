# ECode — a GPU-drawn, VSCode-style code editor on eacp

**Status:** a working editor. Opens files — several at once, each in its own tab
with its own undo history, syntax tree and scroll offset — highlights them with
tree-sitter, scrolls, can be typed in with selection, undo, clipboard and mouse,
and saves, with external-change detection on every open file (§7.8). The chrome
is a widget tree, the sidebar holds a real file tree you can scroll and open
files from, every command is named in a registry that a keymap, a fuzzy-matching
command palette, a native menu bar and a right-click context menu all read from,
⌘O and ⇧⌘O open a file or a project folder, and ⌘F finds and replaces with the
hits lit up in the file. Right-click gives a context menu and the sidebar resizes
by dragging its seam, with the pointer changing shape over both. Every widget the
plan called for now exists (§7.4), and ⌥Z soft-wraps, on a logical-to-visual line
mapping that the renderer, the cursor and the scroll offset all now go through —
the larger of the two structural debts §7.3 named. **Both of those debts are now
closed**: an idle frame no longer works out what it already knows, which took
measuring the frame before building anything and finding the plan's premise wrong
in two directions at once (§7.3). The cold open is closed too, and measuring
*that* found the premise wrong again and turned up a worse bug behind it — every
keystroke had been reparsing the whole file (§7.9). And **multi-cursor** is in —
⌥-click, ⌥⌘↑/↓, ⌘D, ⇧⌘L — the last piece of sequencing this plan had called the
one not to get wrong, and the third time running that the estimate written down
here turned out not to survive contact (§7.2). Sections 1–5 are the design and
the research behind it; **§6 is where things stand and §7 is what to do next.**

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
    ECodeCore/              # buffer, edits, undo, selections, file tree model
    ECodeRender/            # glyph renderer, atlas client, paint context + clip
    ECodeUI/                # widget tree, layout, theme (depends on ECodeRender)
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

**Done so far**, now on eacp's `main` rather than the `ecode-editor-support`
branch this originally landed on (542 eacp tests pass, up from 469):

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

And the y-axis fix, which is the largest correction made upstream so far:

- `Tests/Graphics/RectTests.cpp` — 15 tests. `Rect` had none at all, which is
  how its splitters stayed y-up in a y-down framework. Covers the splitters,
  the edge accessors, exact tiling with no gap or overlap, half-open
  `contains`, and the new `isEmpty`/`intersects`/`intersection`.
- `Tests/GPU/CoordinateSpaceTests.cpp` — 3 tests, and the ones that matter.
  They fill slices taken with `removeFromTop`/`Bottom`/`Left` and assert which
  end of the rendered image each came out at, plus that `setScissorRect` clips
  the same way up. Arithmetic tests cannot catch a convention error; see §9.
- `Tests/Graphics/RenderToImageTests.cpp` — one added test pinning that a
  layer's path space is y-down. The existing layer cases all used full-bounds
  paths, which are orientation-symmetric and pass either way up.

Still open: gap 3 (IME), 9 (UTF-8 helpers), 10 (file watching). Gap 7 is done on
macOS — see §6.

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
| 9 | **No UTF-8 support in `Strings`.** No codepoint iteration, no grapheme clusters, no width tables, and no case folding. | Cursor movement, selection, backspace all operate on graphemes, not bytes — and search's case-insensitive match folds ASCII only, so "Ä" does not match "ä". Soft wrap adds a consumer: with no width table a CJK character counts as one column rather than two, so a wrapped line of it breaks late. | Either add to eacp or vendor a small UTF-8/grapheme library into `ECodeCore`. `ecode::Utf8` now carries `next`, `previousBoundary` and `nextBoundary`, which is the shape the eacp version wants. |
| 10 | **No file watching, no directory enumeration.** | File tree, external-change detection. | FSEvents on macOS; app-level is acceptable initially. ECode polls `File::modificationTime` once a second for the open file, and enumerates directories through `std::filesystem` behind `eacp::toStdPath`. The seams FSEvents replaces are `TextFile::hasChangedOnDisk` and `FileTreeModel::refresh`. |

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
Ghostty has only CPU-side dirty tracking. ~~Follow Alacritty.~~ **Corrected, and
it was the load-bearing half of this section that was wrong.** Alacritty's damage
goes through EGL, which takes damage rectangles at *present* time. Metal has no
equivalent: a `CAMetalDrawable` is presented whole, and the drawable
`nextDrawable` hands back comes from a rotating pool, so loading its previous
contents gives a frame two or three old rather than the last one — Apple
documents the contents as undefined and promises nothing about which drawable
you get. So "redraw only the damaged rows" is not available at all; it would
leave the rest of the window showing an older frame. Ghostty's side is the one
that was open, and CPU-side dirty tracking is what shipped. See §7.3.

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
  **Done** — `CursorSet`, and the fixup runs upwards with a running total
  rather than downwards; see §7.2.
- **Logical ↔ visual line mapping.** Soft wrap, folding, and variable line heights all break the
  `row * cellH` assumption. This is the single biggest structural difference from a terminal grid,
  and the mapping layer should exist even in the read-only milestone. **Done** —
  `LineMap`, with soft wrap as its first consumer; see §7.3.
- **Syntax:** tree-sitter (C library, CPM-friendly), incremental reparse on edit, styles attached
  to ranges rather than baked into cells.
- **The open set.** `Workspace` — the files open at once and which is active, one
  syntax tree and one scroll offset each, never empty. Model only: the tab strip
  reads it and it knows nothing about widgets. See §7.8.

### `ECodeRender`

- Instanced glyph shader written in eacp's C++ shader EDSL — shaders are C++ structs,
  MSL and HLSL and the vertex layout are all generated, and `pass.draw(shader)` binds
  pipeline + vertices + uniforms + textures in one call. No `.metal` files.
- One instance buffer of `{rect, uvRect, fgColor, flags}`; one `drawInstanced` per atlas.
- **Keep** CowTerm's run-length background coalescing — it collapses long same-colour spans into
  one quad and skips default-background cells entirely, since the pass clear already painted them.
- **Real damage tracking.** CowTerm's `changeVersion` machinery exists but its only consumer is
  write-only dead code, so every repaint redraws the whole grid. ~~A dirty-line bitset lets us skip
  both the glyph prepass and the draw loop for unchanged rows.~~ **Done, and half
  of that sentence turned out to be impossible** — the draw loop cannot be
  skipped, because the frame has nowhere to skip *to*: see the correction to §4's
  damage note. The glyph prepass and the *layout* are what got skipped. §7.3.
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
`removeFromBottom(22)`) — and which are only safe to use that way since the
y-axis fix; before it they returned the opposite edge. `intersection` was added
for nesting clip regions, since the GPU has one scissor rect and no stack.
Mouse capture already works correctly — `mouseDownTarget` is latched on
Down and all Drag/Up route to it, which is exactly what splitter dragging and text selection need.
`clickCount` is present for double/triple-click word and line selection.

Built: widget base + layout pass, focus traversal, scroll view + scrollbar, virtualised list,
tree view, tab bar, status bar, the command palette — the first overlay — and a reusable
single-line `TextField`, used three times over: the find bar's two fields and the palette's
query — and since: the splitter and the in-window context menu (`Graphics::Menu`
is the native menu bar only — no `popup(at:)`), and a tab strip that is a real
control rather than a readout. Still to build: minimap, tooltip and
animation/easing. Hover is no longer a gap in the framework — `Widget::mouseExited`
and `WidgetHost::hovered` arrived with the tab strip (§7.8) — only in the widgets
that have not adopted it. A `Button` widget is the obvious one left: the find
bar's six controls are a hotspot table inside it rather than widgets, and now
that hover states are available that has stopped being the right call.

Lifted from CowTerm: `FuzzyMatch.h` (62-line header-only fzf-style scorer), now returning matched
positions as well as a score so the palette can tint the characters the query hit. Its **peek**
pattern (navigating the list live-switches the background view, Enter commits, Esc restores) was
*not* taken — it means nothing for a list of commands, but maps directly onto
file-preview-on-highlight and is still worth having there. `SessionView`'s recursive split-pane
tree maps onto editor groups.

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

**Done** — `CommandRegistry` in `ECodeCore` and `Keymap` in `ECodeUI`, split
there because a keymap has to speak `Graphics::KeyEvent` and the registry is
pure model. Bindings hold command *ids* rather than callables, so a binding for
a command that does not exist is a dead entry rather than a dangling reference,
and the same table can be read from a config file before the registry is
populated. `when` clauses are the one piece not built; see §7.4 for what stands
in for them.

Steal one detail: `charactersIgnoringModifiers` is the correct field for matching shortcuts (so
Cmd+C is "c" on any layout) while `characters` is the correct field for text insertion.

That is right for letters and digits and wrong for everything else, which took a
test to find out — §7.4 has the correction and why the two halves of it
contradict each other on any non-QWERTY layout.

---

## 6. Where things stand

Everything through M4 is done and verified on screen. Commit hashes are in the
log; this is the shape of it.

**Done in eacp** (571 tests): scissor rects, macOS scroll-wheel delivery with
precise/momentum plumbing, backing-scale notification, texture sub-region
upload, `constexpr` colours, `ShaderProgram::prepare`'s blend mode, clipboard
`getText`/`hasText`, the missing key codes, `Files::writeFileAtomically` and
`File::modificationTime`, the y-down fix to `Graphics::Rect`, **menu-item
enablement**, **cursor shapes**, **the Windows menu bar**, and the whole
`eacp-text` module — rasterizer/atlas split,
per-glyph bearings, R8 mask + RGBA colour atlases, growth over eviction,
incremental upload, and `Text::GlyphRenderer`.

Menu enablement is the newest and the smallest: `MenuItem` gains a
`std::function<bool()> isEnabled`, and the runtime target that already forwarded
`trigger:` now also answers `validateMenuItem:`. An `NSMenu` autoenables its
items by default, so AppKit asks that question every time a menu opens — which
means the predicate is read live and an app never rebuilds its bar to change
what is greyed. Without it every item is permanently enabled and nothing fails:
a menu full of unavailable commands looks entirely normal and clicking one does
nothing. `Tests/Graphics/MenuTests.cpp` covers the model, `MenuTests-macOS.mm`
the plumbing, and `Apps/Graphics/MenuBarApp` is the live example. Confirmed red
without the change: the five macOS tests fail, and the one that fails *cleanly*
is the `respondsToSelector:` check — unlike the `scrollWheel:` case, `NSObject`
does not implement `validateMenuItem:` itself, so inheritance does not satisfy
it for free.

**Gap 7 — cursor shapes** closed on macOS, and the shape of the API is the
interesting part. A cursor fixed *per view* would have been useless here: a
GPU-drawn UI is one view with a whole widget tree painted into it, so ECode's
entire window has exactly one `Graphics::View` and many regions. So
`View::setMouseCursor` is settable at any time, from inside a `mouseMoved`
handler, and setting the same shape twice is free — the caller is a hover
handler and should not have to remember what it last asked for.

The macOS half needs both `cursorUpdate:` and an immediate `[cursor set]`.
Without the first, a shape set from a move survives only until the pointer
crosses a boundary and AppKit resets it, which reads as the cursor flickering
back at random. Without the second, the shape does not change until the *next*
cursorUpdate:, so the pointer sits on the splitter still showing an arrow. The
tracking area also had to ask for `NSTrackingCursorUpdate`, and it is easy to
add the method and forget the flag — the method then never fires and nothing
else about the view looks wrong.

Windows stores the shape and does not apply it, deliberately and with the reason
written down: Windows re-asks on every `WM_SETCURSOR`, so doing it properly means
handling that in the host window rather than in `View-Windows.cpp`, and there is
no Windows machine in the loop to see a flickering cursor on. `getMouseCursor()`
still answers, so the portable half is testable everywhere.

`Tests/Graphics/CursorTests.cpp` covers the state, `CursorTests-macOS.mm` the
plumbing — including an end-to-end check of the enum-to-`NSCursor` mapping via
`NSCursor.currentCursor`, and that no two shapes collapse onto the same cursor.
`Apps/Graphics/CursorShapes` is the live example: five bands in one view, the
shape following the pointer across them. Confirmed red without the change.

**The Windows menu bar**, which had never existed: `setApplicationMenuBar` was
an empty function body there, so ECode built its whole bar and handed it to
something that discarded it. macOS was the only backend that ever did anything.

The API now takes a window, and that is what made it implementable. macOS has an
*application* menu bar shown for whichever window is active, so the argument is
ignored there; Windows has no such thing — a menu belongs to an HWND and is drawn
in that window's frame — so it has to be told which one.

**The interesting part is how much of it is testable.** Win32 identifies menu
items by integer command id, so the implementation splits into a table (ids,
labels, accelerator text, id → action) and a thin shell of `CreateMenu` /
`AppendMenuW` / `SetMenu` calls. The table is portable, lives in
`MenuCommands.h`, and is tested on macOS by 16 tests; only the shell is
unverifiable from here. That split is `App-Windows-FilePicker.h`'s trick, which
this repo already used for the same reason — an untestable modal with testable
logic around it.

The property those tests really pin is one no single function owns: the builder
walks the tree assigning ids as it appends, and `flattenCommands` walks it again
assigning ids as it collects. Neither knows about the other — they agree only
because they agree on the *order*. Separators, submenu headers and
responder-selector items all have to be skipped identically by both, and an
off-by-one there means the menu silently runs the wrong command.

Two other things worth recording:

- **Greying happens on `WM_INITMENUPOPUP`**, which is where Win32 asks the
  question `validateMenuItem:` answers on macOS. `EnableMenuItem` with
  `MF_BYCOMMAND` searches from the root, so every item updates without tracking
  which popup is opening — and the app still installs its bar once.
- **`standardEditMenu` cannot be the same menu on both platforms.** The macOS
  one routes Cut/Copy/Paste down the responder chain with selectors, which
  Windows has no equivalent for. The Windows version carries labels and
  accelerators but no actions, and an app wanting working entries builds them
  from its own commands — which is what ECode does.

**Not verified on Windows.** No machine here can compile it — `Menu-Windows.cpp`
and the `WndProc` routing are `if (WIN32)` and the macOS and iOS builds never
touch them. So it went through an adversarial read instead, which found no
compile errors but seven real behaviour bugs. Six are fixed; the ones worth
recording:

- **`SetMenu` steals the client area.** The window is sized before it has a
  menu (`AdjustWindowRectExForDpi` with `bMenu = FALSE`, correctly, at creation
  time), and adding the bar takes its height straight out of the client rect —
  so the content came up a menu-bar shorter than it asked for, with the bottom
  clipped until the first resize. Measured and given back rather than
  calculated: the bar wraps to two rows on a narrow window and `SM_CYMENU` only
  ever describes one.
- **`HIWORD(wParam) == 0` is not "this came from a menu".** For a control
  notification that is the notification code, and `BN_CLICKED` is also 0. The
  documented test is `lParam == 0`, and eacp does host child HWNDs.
- **`&` in a title is a mnemonic prefix**, so "Find & Replace" drew as
  "Find _Replace". Escaped on the Win32 path only — macOS wants the raw string
  and the same `MenuBar` is built for both.
- **Two walks assigning the same ids is a bad shape**, and it had already gone
  wrong: the builder asked "separator?" first and `flattenCommands` asked
  "submenu?" first, so an item carrying both flags consumed no ids in one walk
  and gave ids to a whole subtree in the other — every id after it naming a
  different command. Both now switch on one `classifyMenuEntry`, so they agree
  structurally rather than by having been written to match. The comment claiming
  the tests covered that agreement was, before this, simply false.

**What the review also found is that a whole file was unnecessary**:
`Window::getHandle()` was already public and already returned the HWND, so the
friend-access seam invented for this was deleted along with the change to
`Window.h`.

Left undone, and the reason: **Alt+F does not open the File menu.** Win32
assigns no mnemonics — a title needs an explicit `&` — and separately
`takePendingCharacters` drains `WM_SYSCHAR` before `DefWindowProc` can match
one. That is pre-existing keyboard plumbing rather than menu work, and guessing
at it blind is how the other six bugs would have got in.

**Done in ECode** (568 tests): `Document` with an incremental line index,
`TextEdit`/`EditHistory` with step grouping, `Cursor`/`Editor`, `TextRenderer`
drawing only the visible slice with clipped gutter and text, `GlyphRenderer`
batching, tree-sitter highlighting with incremental reparse, the full typing
loop — keys, mouse, selection, undo, clipboard, blink, scroll-to-caret —
`TextFile`: the file lifecycle, saving included, `ECodeUI`: the widget
layer, with the chrome drawn by it rather than hardcoded — scroll containers,
a virtualised list, and a working file tree in the sidebar — and the command
layer: `CommandRegistry`, `Keymap`, `FuzzyMatch` and a `CommandPalette` that
reads both through a shared `TextField` — find/replace: a `Search` model,
a `FindBar`, match highlighting in the renderer and grouped undo for
replace-all — the menu bar: `MenuBuilder`, ⌘O and ⇧⌘O, and one dispatcher that
the keymap and the menus both arrive at — and the last two widgets: a right-click
`ContextMenu` and a draggable `Splitter` — `LineMap`, the logical-to-visual
row mapping, with soft wrap on ⌥Z as the consumer that proves it — damage
tracking: a `RowCache` of laid-out rows and a memoised highlight query, which
take an idle frame from 0.32 ms to 0.058 — and **more than one file open at
once**: a `Workspace` of `OpenFile`s, each with its own highlighter, undo
history and scroll offset, behind a tab strip that closes, shrinks, overflows
and scrolls, with ⌘N, ⌘W, ⌃Tab and ⇧⌘S, and untitled buffers that now have
somewhere to save to (§7.8) — and the cold open: a `SyntaxLanguage` shared by
every highlighter, a parse budgeted across frames so a large file is on screen
immediately, and the incremental reparse `applyEdit` was written for actually
running, which takes a keystroke on an 8,000-line file from 9.6 ms to 0.24 and a
twenty-file launch from 295 ms to 11.8 (§7.9) — and **multi-cursor**: a
`CursorSet` that owns the sorted, non-overlapping, never-empty invariant, every
edit and movement applied at N cursors as one thing to undo, N carets and N
selections drawn, with ⌥-click, ⌥⌘↑/↓, ⌘D, ⇧⌘L and Escape (§7.2).

**Proven elsewhere**: CowTerm ported onto `eacp-text` (−904/+208), rendering
CJK and colour emoji correctly. That was the test of whether the extraction was
real rather than a rearrangement, and it exposed two genuine gaps —
`GlyphRenderer` missing entirely, and a clipboard API that duplicated Jamie's.

---

## 7. What to do next

Ordered by what unblocks the most, with the reasoning rather than just the list.

**Where the front line is now that 7.1, 7.2, 7.3, 7.4, 7.8 and 7.9 are done.**
Nothing here is blocked on anything else, so this is a choice rather than a
sequence:

- ~~**The cold open.**~~ — **done, and the plan had the wrong half of it. See
  §7.9.** The 40 ms was not the parse: it was 14 ms of compiling the highlight
  query plus 10 ms of parsing, and it is the *query* that §7.8 multiplied by the
  number of open files. The parse was already lazy. Measuring it also turned up a
  bug worth more than the cold open itself — every keystroke was reparsing the
  whole file.
- ~~**Multi-cursor (§7.2).**~~ — **done, and the predicted bill never came.**
  §7.2 had spent months saying the deferral was costing a refactor across the
  app; `Editor::cursor()` turned out to have seven callers outside the editor
  and not one of them had to change. See §7.2 for why, and for what the
  deferral *did* cost.
- **IME (§7.5).** Still the largest framework gap, and the marked-text range in
  the cursor model is the part that should not wait for the rest of it.
- **Windows text (§7.7).** `GlyphRasterizer-Windows.cpp` is a stub, so Windows
  draws no text at all. First thing between ECode and a second platform, and the
  only item here that cannot be verified on this machine.
- ~~**More than one file open at a time.**~~ — **done, see §7.8.**
- **Editor groups**, which is what §7.8 makes the next obvious thing rather than
  an idea: one `Workspace` holds one active file, and a split view is two of
  them side by side. `SessionView`'s recursive split-pane tree in CowTerm is the
  shape. What it collides with is named in §7.8: one `TextRenderer` and one
  `RowCache` for the app, which two visible editors would thrash.
- **A 100 MB file**, which nothing here has ever measured — §7.6 defers the rope
  until one hurts and §7.9 could not say when that is. The two candidates are
  `Document`'s flat string and its line index, and which bites first is a
  measurement rather than an argument. The cheapest real thing left to learn.
- **The first ⌃Tab onto a large tab**, the 8.6 ms §7.9 leaves behind. Nothing
  parses a file until it is looked at, so the cost lands on the switch; starting
  it at open in 2 ms slices off the same budget would move it somewhere nobody is
  waiting. Smaller than either item above, and the machinery already exists.

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

**Now verified on screen**, which no test covers and the writing session could
not capture. Driven with synthesized keystrokes against a scratch file: typing
lights the tab dot and prefixes the title; ⌘S clears both and the bytes land on
disk; an external rewrite of a *clean* buffer appears on its own within the
poll interval and re-highlights; an external rewrite of a *dirty* one turns the
dot orange, appends "changed on disk. ⌘S again to overwrite" to the title and
writes nothing; the second ⌘S then writes the buffer. All four behave as
designed.

One accident worth keeping. An early run showed a stray `®` at offset 0 and a
lit dirty dot — a stray Option+R reaching the focused window, since `keyDown`
inserts `event.characters` and Opt+R *is* `®` on macOS. Not a bug, and it
confirmed the dot from the other direction: it lights exactly when the buffer
genuinely differs, including when the edit came from somewhere unexpected.

### 7.2 Multi-cursor — done, and the bill was much smaller than billed

⌥-click adds or removes a caret, ⌥⌘↑ and ⌥⌘↓ grow a column, ⌘D adds the next
occurrence and ⇧⌘L takes all of them, Escape collapses back to one. Every
editing operation, every movement, copy and the renderer all work at N cursors.

**This section spent months predicting a refactor across the app, and it was
wrong about that** — which is worth recording as plainly as the estimate was.
It said the bill "grows with everything written against a single cursor in the
meantime: the widget layer, find and replace, the palette's commands". Measured
by grep before starting, `Editor::cursor()` had **seven** callers outside the
editor and its tests, and every one of them genuinely wants the *primary*: the
status bar's line and column, the offset ⌘F resumes from, the caret the view
scrolls to, the caret `TextFile::reload` preserves. Not one of them had to
change. The rule the old section laid down — *anything new that touches the
cursor goes through `Editor`, not into `Editor::cursor()`* — is why, and it is
the part of this that turned out to be load-bearing. The deferral was the right
call and the reason given for it was not the one that paid off.

What did change: the renderer's `const Cursor*` became a `const CursorSet*`,
exactly as this section predicted, and `EditorWidget::isInsideSelection` had to
ask about all of them rather than one. Two lines and a loop.

The one piece that already existed also held. Replace-all needed several edits
as one thing to undo, so `EditHistory::beginGroup`/`endGroup` and the RAII
`UndoGroup` were in and tested — and a keystroke at N cursors is the same
shape.

Decisions worth recording, because each went against the obvious version:

- **The set is a type, not a vector.** `CursorSet` holds three invariants — at
  least one cursor, in document order, never overlapping — and `transform()` is
  the only way to mutate it, so the repair runs on the way out rather than at
  each of a dozen call sites that could forget it. All three failures are
  silent: two cursors in one place type every character twice, two out of order
  make the edit at one shift the other by a stale amount, and an empty set is a
  window with no caret. Nothing about any of them looks like a crash.
- **Edits run upwards with a running total, not downwards.** This section
  originally called for the highest offset first, which needs no shift for the
  cursors not yet reached — but then needs every cursor already done shifted by
  each later edit, which is the same arithmetic with an extra loop to get wrong.
  Upwards is one accumulator and no second pass.
- **A cursor is shifted whether or not it edits.** The bug that shape invites,
  and the one the tests were aimed at: forward-delete with a caret at the end of
  the file deletes nothing there, and a cursor left at its unshifted offset is
  pointing past the end of a document the cursors below it just shortened.
- **The undo group is conditional.** `beginGroup` ends whatever step is open, so
  grouping every keystroke would stop a typed word undoing as a word. With N
  cursors there is no such run to preserve — one keystroke is already N edits
  that have to be one step. Both directions are wrong in a way only ⌘Z reveals,
  and both are pinned.
- **Undo goes back to one cursor.** Restoring the set an edit was made with
  means recording it on the step, which is a change to `EditHistory`. The cheap
  substitute — a cursor per edit in the undone step — is right for a
  multi-cursor keystroke and wrong for the other kind of grouped edit: undoing a
  replace-all would leave a cursor on every occurrence in the file.
- **⌘D decides whole-word from the text, not from a flag.** VSCode matches whole
  words when the selection came from expanding a caret and substrings when it
  did not, which is the behaviour that keeps ⌘D on `id` from catching the `id`
  inside `hybrid`. A remembered flag would have to be cleared by everything else
  that changes a selection, and the one that got missed would silently change
  what the next press matched. Asking the two word motions whether the selection
  *is* a word answers the same question with no state.
- **The current-line band is drawn per line, not per cursor.** At 3.5% white a
  second pass over the same rectangle is a visible step in brightness. Cursors
  arrive in document order, so remembering the last line filled is the whole of
  the deduplication — and it is a fold no per-cursor test can land on.
- **Every caret looks the same, primary included.** Marking the primary would
  say which one a following ⌘F searches from, and cost the much more useful
  reading that all of them are equally live.
- **⌥-click toggles.** Adding only would leave a click that landed one character
  off with no way back except Escape and starting over.

**Verified four ways.** `Tests/MultiCursorTests.cpp` covers the model — 39
tests, chosen for inputs where a plausible wrong implementation gives a
*different* answer rather than no answer. `Tests/MultiCursorRenderTests.cpp`
drives a real `EditorWidget` in a real `WidgetHost` with synthesized ⌥-clicks
and keys and reads the pixels back. Twenty-seven mutations were tried and all
twenty-seven went red — one only after a test was rebuilt around the state it
needed, which is recorded in §9. Then it was looked at, off-screen and dumped to
files: a column of carets down three lines, every occurrence of a token selected
at once, two carets on one line with the band at exactly a single caret's
brightness, and `const ` typed into three lines from one keystroke.

The menus were driven live, since that is the half `renderToImage` cannot see:
Add Cursor Above/Below, Add Selection To Next Find Match and Select All
Occurrences all run, and Collapse To One Cursor comes up greyed with one cursor
and live with two — the predicate chain end to end. ⌘D and ⇧⌘L are read back
through AX as real key equivalents; the two arrow chords deliberately are not,
for the reason ⌃Tab is not (§7.4).

**Not done, deliberately:** an undo that restores the cursor set, as above.
Multi-cursor paste that distributes N copied lines to N cursors — VSCode does
it, and it needs the clipboard to carry how many selections produced the text.
Column (box) selection by ⌥-dragging, which is a different gesture on the same
model. And ⌥-double-click to add a *word* selection: the widget takes ⌥-click
before the click-count cases, so ⌥-double-click adds two carets rather than
selecting a word.

Soft wrap was the first change to test the narrow-surface rule, and it is the
reason `Editor` owns the line map rather than the view: moving by visual rows is
cursor movement, so it stayed behind `moveUp`/`moveDown` instead of the widget
computing an offset and calling `placeCaret`. The surface did not widen, and
that is what made this change small.

### 7.3 The widget layer — foundation done, concrete widgets next

`ECodeUI` exists and the chrome is drawn by it: `drawChrome()`'s hardcoded
rectangles are gone, and the tab strip draws the real filename with its
unsaved dot rather than a bare rectangle. eacp deliberately provides none of
this — `GPUWidgets` is path tessellation, not widgets, and `Graphics::View` is
one `NSView` per widget, which a 5,000-row file tree cannot use.

**What shipped:** `Widget` (tree, absolute bounds, paint/prepare walks,
hit-testing, visibility), `WidgetHost` (mouse capture, wheel routing, focus and
tab traversal), `PaintContext` + `ClipScope`, and the first widgets — `Panel`,
`TabBar`, `StatusBar`, `EditorWidget`. `TextRenderer::draw` now takes the
context instead of a raw pass, so an editor nested in a scrolling container
will clip correctly rather than drawing over its parent.

Three decisions worth recording, because each went against the obvious version:

- **Bounds are absolute, not parent-relative.** A parent splits *its own*
  rect with `Rect::removeFrom*` and hands the pieces down. That is what the GPU
  wants — a scissor rect is absolute and there is exactly one — and it makes
  hit-testing a plain `contains()` rather than a walk back up the tree
  accumulating offsets. The cost is that moving a widget relays out its
  subtree, which is nothing at the scale of IDE chrome.
- **The clip and the glyph batch are owned by the same object, because they
  are coupled and the coupling is silent.** `GlyphRenderer` batches between
  `begin()` and `flush()`, while the scissor is pass state read when a draw is
  *issued* — so glyphs queued under one clip and flushed under the next are
  clipped by the next one. `PaintContext` flushes on every clip change. The
  same object also rebinds the sprite pipeline lazily, since a glyph flush
  leaves the glyph pipeline bound and the next `fillRect` would otherwise be
  drawn through a shader that samples an R8 mask.
- **`Highlighter::update` moved onto the interface.** It was on
  `SyntaxHighlighter` alone, so a view had to know the concrete type to tell it
  which lines were about to be drawn — and that call is what keeps scrolling
  proportional to the viewport rather than to the file.

**One thing implemented but not reachable:** `WidgetHost` does Tab traversal
and it is tested, but nothing in the app calls it. Tab inside the editor
inserts spaces and consumes the key, which is what VSCode does — traversal
there is bound to chords like focus-explorer instead, and those arrive with the
keymap in 7.4. Clicking the tree does focus it, so the arrow keys drive the
tree today; that path is real.

**Since then:** `ScrollView`, `ScrollBar` and `ListView`. Scrolling turned out
to need no scrolling code path at all — the content is laid out at full height,
positioned above its parent by the scroll offset, and the `ClipScope` every
widget already gets in `paintTree` is what cuts it back. The list virtualises
off `PaintContext::clip()`, which after intersection *is* the visible band in
the list's own coordinates, so no separate notion of a viewport is kept.

**Still to build:** splitter and context menu. The overlay is done — the
command palette is one, and `ListView::setFocusable` came out of it: a list
inside something that owns the keyboard itself must not be a focus stop, or
clicking a row moves focus off its owner and the next keystroke goes nowhere.
The editor still scrolls itself rather than living in a `ScrollView`; worth
reconciling, but its own scrolling works and is tested, so it is not urgent.

**Cleared first, because the widget layer is built on it:** eacp's coordinate
space is y-down — `isFlipped` backing views, so `View::setBounds` and
`MouseEvent::pos` measure from the top, and the sprite, glyph and scissor paths
all put y = 0 at the top. `Graphics::Rect`'s splitters were the lone holdout,
still y-up from JUCE, which meant `removeFromTop` returned the *bottom* slice.
ECode had been drawing its tab bar along the bottom edge and its status bar
along the top since the chrome was written. Fixed upstream rather than worked
around, since the two conventions were contradicting each other inside one
framework rather than expressing a choice. `Rect` had no tests at all; it has
15 now, plus 3 that render slices off-screen and assert which end of the image
they land on — see §9.

An audit of the rest of eacp found everything else already agrees: the macOS,
iOS and Windows view backings, `Context` on both platforms, `Path` on Windows
and in `GPUWidgets`, `Image::at`, `renderToImage`, scissor rects, both GPU
shaders, `GlyphSlot::offset`, texture regions, camera frames, window
positioning and mouse position. The places that touch a natively y-up API
convert explicitly and say so — the off-screen `CGBitmapContext` flip, the
rasterizer's CoreGraphics conversion, the `CGWarpMouseCursorPosition` call.

**One inference, not an observation.** macOS `Layer::setPosition`/`setBounds`
is the one spot where the answer comes from AppKit rather than from code here:
`geometryFlipped` is never set, so nothing in the tree decides it. Two checks
say y-down — the snapshot path renders an asymmetric layer path against the top
edge, and `Layer.mm` is shared with iOS where a `(0,0)` anchor is
unambiguously top-left — and there is now a test pinning it. But it has not
been confirmed on a real screen, and the session that found it could not
capture one.

Riding on that: `Apps/Graphics/ComplexGUI/TaskBoard.cpp` positions its labels
as though layers were y-up (`titleLayer` at `bounds.h - 22`, `descLayer` at
`bounds.h - 40`), which under the y-down reading puts both near the bottom of
the card with the title *below* the description. Not fixed — recovering the
intended spacing needs the text layers' heights and a look at the result. Its
header band is a separate case that the `Rect` fix moved from the bottom to the
top, where it was always meant to be.

Two things still to get right, because both are painful later:
- ~~**Variable line height.**~~ — the mapping half is **done**; see below.
- ~~**Damage tracking.**~~ — **done**, and it came out a different shape than
  this section asked for. See below.

**Done: the logical-to-visual line mapping, with soft wrap as its consumer.**
`LineMap` in `ECodeCore` answers which text a row holds and which row an offset
is on; `TextRenderer` draws rows, `Editor` moves the cursor by rows, and
`EditorWidget` scrolls by them. ⌥Z toggles wrapping and the View menu carries a
checkmark for it.

The mapping was the part that could not be retrofitted, and it is worth being
precise about what is and is not done: **row height is still uniform.** It now
lives behind one function (`TextRenderer::rowTop`) instead of being multiplied
out at eight call sites, which is what makes a per-row height a change to a
class that already has one entry per screen strip. The debt this section named
was the assumption, not the arithmetic.

Decisions worth recording, because each went against the obvious version:

- **`Editor` owns the map, though a wrap width is a property of a view.**
  Vertical movement is its first caller and the cursor is the editor's, so the
  alternative is either a view reaching into the cursor — which §7.2 is explicit
  about not doing — or a subscriber on `onEdit` that a caller can forget to
  attach, in a class where forgetting means drawing the text as it was before
  the keystroke. The price is that two views of one file would share a wrap
  width, which is the point at which this becomes a view model.
- **Wrapping is measured in display columns, not pixels.** A tab is one byte and
  four columns, so bytes are wrong; pixels would drag font metrics into
  `ECodeCore`. Columns are exactly right for a monospace editor and are the seam
  a proportional one replaces. The same walk now serves `columnToX`, which had
  its own copy of the tab-stop arithmetic.
- **Only per-line row *counts* are stored; break positions are recomputed.**
  One `size_t` per line, the same shape and the same cost as `Document`'s line
  index, repaired the same incremental way — and a one-line cache, because the
  draw loop asks for consecutive rows of one line and recomputing per row would
  make a wrapped paragraph quadratic in its length. Storing every break would
  have been ~10 MB on a wrapped 100 MB file, on top of the line index.
- **`LineMap::rebuildCount()` exists for the tests, and it earns its keep.** An
  incremental update that quietly fell back to a full rebuild agrees with a full
  rebuild on every input — so the oracle §9 recommends cannot, on its own, tell
  the fast path from the slow one. The counter is what makes the oracle test
  fail when the optimisation is deleted, which it was confirmed to do.
- **A caret at a wrap point is genuinely ambiguous, and it is settled twice.**
  The offset ending one row is the offset starting the next, and nothing here
  models affinity. End backs over the blank the wrap left behind, and a held
  column that would land on a continuation row's far edge backs up one
  character. Both keep the caret on the row it was aimed at. What is left is the
  case with no blank to back over — a break inside a long token — where clicking
  past the end of a row still puts the caret at the start of the row below.
- **The gutter numbers lines, so a continuation row carries no number.** That
  one falls straight out of a gutter loop that still thinks in lines, and it is
  the single most obvious way to make wrapping look wrong.
- **A toggle command needs a third predicate.** `Command::isChecked` is the one
  `std::function` here deliberately left null by default, because null is "not a
  toggle" and no value of the predicate says that — false would put an empty
  checkbox beside every ordinary command. eacp's `MenuChecked` had already drawn
  the same distinction the same way; this only routes it through.

Not done, and deliberately: **continuation rows are not indented.** VSCode's
default aligns a wrapped row with its line's indentation, which matters much
more for code than for prose. It is a `LineMap` change — a per-line indent
added to the row's left edge and subtracted from its width — and it wants the
renderer to know about it too, so it is a change worth making on its own.

**Verified three ways, and the third one is the reason.** `LineMapTests` pin the
model against an oracle; `WrapRenderTests` render the assembled thing off-screen
and read the pixels back, which is what catches a map that is right and a
renderer that ignores it. Then the picture was dumped to a file and looked at,
which is where the tab stops, the mid-word break on a long token and the shape
of a selection spanning a wrap point were actually confirmed. Four mutation
checks stand behind the model tests and three behind the render tests; two of
the original tests passed against deleted code and had to be rebuilt — see §9.

The menu was driven live, since that is the half `renderToImage` cannot see:
the item runs the command and the checkmark follows it, confirmed through the
accessibility API on both sides of a toggle. **⌥Z itself is not confirmed by a
keystroke** — ECode would not come to the front to receive one — only that the
menu carries the chord as a native key equivalent, which is what macOS matches
before a window sees a key. See §9; the reading took considerably longer to get
right than the feature did.

**Done: damage tracking — but measured first, and the measurement moved the
target.** This section called it "the next thing to bite". It was not: a full
repaint of a 2500×1350 window showing 73 rows of highlighted C++ cost **0.32 ms**
of CPU in a `-O2` build. At the app's on-demand cadence that is nothing, and even
at 120 Hz it is 4% of the frame. The debt was real as a statement about the
design and wrong about the urgency, and it is worth saying which.

Then the shape of the fix turned out to be settled by the platform rather than by
us. **The drawing cannot be skipped.** §4 said to follow Alacritty, which sends
damage rectangles to EGL at present time; Metal has no such call, and the
drawable `nextDrawable` returns comes from a rotating pool, so its previous
contents are a frame two or three old. Drawing only the changed rows would leave
the rest of the window showing that older frame. What a frame actually spends its
time on is not the drawing anyway — it is *deriving* what to draw. So that is
what is cached, and every frame still draws every visible row.

Two pieces, and the smaller one was the bigger win:

- **The highlight query is memoised.** `SyntaxHighlighter::update` re-ran the
  tree-sitter query, repainted a `TokenKind` per byte and rebuilt the span map on
  **every frame**, for the same lines of the same unchanged text. It now keeps
  what it computed and the range it computed it for.
- **`RowCache` in `ECodeRender`** holds each visible row's glyphs — destination,
  atlas source, colour — so the UTF-8 decode, the atlas lookup per character and
  the span search happen once per row rather than once per row per frame.
  `TextRenderer::prepare` skips rasterizing a row it already holds, and `draw`
  replays it.

| frame | before | after |
|---|---|---|
| idle (nothing changed) | 0.32 ms | **0.058 ms** |
| scrolling, one row per frame | 0.32 ms | **0.069 ms** |
| highlight queries per 401 frames | 401 | **3** |
| row layouts per 401 frames | ~29,300 | **272** |

The first frame is unchanged at ~40 ms for an 8,000-line file, essentially all of
it tree-sitter's initial parse. That is now by far the largest number in the
renderer and the only one worth looking at next.

**Both halves of that last paragraph were wrong, and §7.9 has the numbers.** It
is ~24 ms of real work for 8,000 lines, not 40; and rather than "essentially all
of it" the parse is 10 ms of it, against 14 ms spent compiling the highlight
query, which has nothing to do with the file at all. Written down here as it
stood because it is the same mistake §7.3 opens by admitting: a number was
attributed rather than measured.

Decisions worth recording, because each went against the obvious version:

- **Cached glyphs are row-local, not absolute.** Positions measured from the
  row's own origin and the baseline mean a scroll is an origin change rather than
  an invalidation — the text did not move relative to itself. Absolute positions
  would throw a screenful away every time the view moved by a pixel, which is
  exactly the case worth keeping.
- **A window of rows, not a map of them.** An entry per row index would grow to
  one per row ever scrolled past — hundreds of megabytes on a large file — so the
  cache is narrowed each frame to the rows about to be drawn, and `store` refuses
  a row outside that window rather than resizing to reach it. Refuses without
  taking it, so the caller can still draw what it built.
- **One stamp for the whole cache, not a per-row dirty set.** Any change to the
  text, the colours, the wrap width, the tab width or the atlas generation drops
  everything. A per-row set would keep the rows above an edit, which sounds
  better until you notice what typing already costs: a reparse and a re-query,
  next to which laying out a screenful of rows is small. The frames worth saving
  are the ones where *nothing* changed, and those are all of them except the
  keystroke itself.
- **A document revision is unique across documents, not per document.** The
  obvious counter starts at zero in every `Document`, so opening a file would
  look to the cache exactly like the file it replaced at the same revision —
  same number, different text, and the old rows kept. One counter for the process
  and a new document drawing from it makes that state unrepresentable.
- **`Highlighter::version()` is on the interface, because the document's revision
  cannot see a reparse.** A parse that finishes changes what a line already
  reported comes back as without changing a byte. The renderer caches colours, so
  it needs to be told; a highlighter that never changes its mind can leave the
  default alone.
- **The query is widened past the window.** Scrolling asks for a window one line
  further down each frame, so an exact memo misses every single time — the reason
  scrolling was still 0.27 ms after the row cache landed. A 96-line margin either
  side costs a longer query on the frames that run one and buys 96 rows of
  scrolling that run none. That makes `update`'s documented range a floor rather
  than an exact promise, and two existing tests were asserting the promise: both
  were rewritten around a document longer than the margin, which is the honest
  form of the claim they were making (bounded by the window, not by the file).
- **The backing scale moved into `TextRenderer`'s constructor.** The prepass and
  the layout have to agree about which rows are cached, and only one of them is
  handed a `PaintContext`. The scale belongs to the atlas the renderer draws
  through in any case: a renderer sizing glyphs by one scale while its atlas was
  rasterized at another draws the wrong size.
- **The highlighter now runs before the glyph prepass, not after.** With colours
  cached, the old order would let the prepass decide a row needs nothing while
  the draw that follows finds it stale and lays it out again — after the atlas
  upload, so a glyph the new colouring needs first would be sampled from texels
  not yet on the GPU. One line moved, and the reason is written where it moved
  from.

**Not done, deliberately:** per-row invalidation on an edit, as above. And the
cache belongs to the `TextRenderer`, of which the app has one; the day a second
editor shares it, the two will invalidate each other on every switch. That is
correct, just not free, and it is a note for whoever adds tabs.

**Twelve tests, and the counters are what make them tests at all.** A renderer
that re-derives every row draws exactly the same picture as one that reuses them,
so pixels alone can say nothing — `RowCache::layouts()` and
`SyntaxHighlighter::queries()` exist for the tests, in the shape
`LineMap::rebuildCount()` established. Sixteen mutations were tried and thirteen
went red; the three that did not are each recorded in §9, and two of them are
statements about the design rather than about the tests.

Then it was looked at: the assembled tree with the real highlighter, dumped
off-screen. Typing between the two slashes of a `//` breaks the comment and the
whole line loses its comment colour in the same frame — which is the recolouring
path end to end, and the one thing no counter can confirm.

### 7.4 IDE chrome, on top of the widget layer

**Done:** the file tree, editor tabs and status bar contents. The tree is a
`FileTreeModel` — a directory listing flattened into rows, so row 4,000 is
answerable without walking 4,000 nodes — drawn through the virtualised
`ListView`. Only expanded directories are ever read, so an unexpanded `.git` or
`build` costs nothing, and expansion is remembered by path so a directory that
disappears and returns comes back open.

**Also done: the command layer and the palette.** The registry did come with
the palette rather than after it, which was the right call — ⌘⇧P works because
the palette enumerates `CommandRegistry` and prints what `Keymap::chordFor`
says runs each entry, and neither holds a list anyone maintains by hand.
`Main.cpp`'s if-chain of Cmd chords is gone: `registerCommands()` names twelve
commands and `bindKeys()` is a table of ten bindings.

CowTerm's `FuzzyMatch.h` was lifted as planned, with one addition — it returns
*where* it matched, so the palette tints the characters the query hit rather
than the whole row. Its `Palette` peek pattern was not taken: peek live-swaps
the background view, which is worth having for file preview and means nothing
for a list of commands.

Four decisions worth recording:

- **A chord is identified by what the key produced, not by which key it was.**
  The plan said match on `charactersIgnoringModifiers`, and eacp's own
  `KeyCode` header says the opposite for punctuation — a character is wrong
  there, since `Cmd+Shift+/` arrives as "?" and no binding written `/` matches
  it. Both are right, and they collide on any layout where a key at a
  punctuation *position* types a letter, which is most of the non-QWERTY ones.
  Asking what came out settles it: a letter or digit identifies itself, and
  anything else defers to the code. A test written for the Dvorak case is what
  forced this — it went red against a first version that checked the code
  first, and the failure it describes is silent, since a binding that never
  matches simply does nothing.
- **Shift is folded out of the character.** macOS delivers `Cmd+Shift+P` as
  "P", so a chord that kept it would have two spellings and only one would
  match. It lives in the modifiers alone.
- **A shortcut that has been rebound is not printed.** `chordFor` checks that
  the binding it found is still the one that wins, because the alternative is
  the palette telling someone to press a key that now does something else.
- **The palette is modal while open, and that is a special case rather than a
  mechanism.** Everything except a command chord reaches it before the keymap
  does, so a binding without a modifier would be typed into the query rather
  than fired. This is the job a VSCode `when` clause does; contexts belong with
  the config file, and one overlay did not justify inventing them early.

**Verified off-screen, not on a real screen** — and the distinction matters
here, because §7.1's file-lifecycle work was driven with synthesized keystrokes
against a live window and this was not. `Tests/PaletteRenderTests.cpp` renders
the palette through the real widget host and reads the pixels back, which is
what answers the two questions logic tests cannot: the backdrop dims rather
than covers, and the matched characters come out tinted. What that leaves
unconfirmed is the part only a person can judge — whether ⌘⇧P *feels* right,
whether the box sits at a sensible height, whether the dimming is too strong.
Worth ten seconds in the running app.

**Also done: find and replace.** ⌘F, ⌥⌘F, ⌘G and ⌘⇧G, case and whole-word
toggles, a live "3 of 17", every hit tinted with the current one picked out,
replace and replace-all. The search model is `Search` in `ECodeCore` — literal
text, no regex, and `findMatches` is the seam an engine would slot into.

Six decisions worth recording:

- **The search lives in `EditorWidget`, not in the find bar.** Everything it
  needs is already there: the document to search, the scroll offset that brings
  a hit into view, the renderer that draws the hits. The bar is a query and some
  buttons, and reports both. The alternative — a bar that owns the search — has
  it reaching for a document it does not have.
- **A hit is re-found by offset, not by index.** An edit earlier in the file
  changes how many occurrences precede the current one, so an index quietly
  starts naming a different place, and the symptom is the counter reading the
  same "3 of 17" while the view jumps somewhere unrelated. Pinned by a test that
  inserts an occurrence *before* the current one.
- **Find-next takes an offset rather than stepping a pointer.** Both directions
  are "the first hit after / before here", driven by the caret — so moving the
  caret and pressing ⌘G looks from where the person is rather than resuming a
  walk they have left. Wrapping stops being a case and becomes the same
  question asked past the end.
- **Replace-all needed a new undo primitive.** `EditHistory` only ever merged
  insertions that continued where the last one ended, which is right for typing
  and refuses exactly what replace-all is: replacements, running backwards
  through the file. So each occurrence landed on the stack separately and one
  ⌘Z left the file half-replaced. `EditHistory::beginGroup`/`endGroup` and an
  RAII `UndoGroup` fix it, and **multi-cursor wanted the same primitive** — N
  edits, one thing to undo. It uses a conditional version of it rather than
  `UndoGroup` itself, for the reason §7.2 records.
- **`Widget::isTextInput()` is the first piece of a `when` clause.** ⌘A, ⌘C and
  ⌘V with a find field focused belong to the field; everywhere else they belong
  to the document. Pasting a search term into the file being searched is a
  mistake that *edits the file*, which is why this one distinction was worth a
  virtual before contexts exist. The field claims those four chords and passes
  every other one on.
- **`TextField` never consumes Return, Escape or Tab.** They mean different
  things per owner — Return runs the command in the palette and finds the next
  hit in the find bar — so the field returns false and whoever owns it decides.
  That is what lets one field serve both.

**The palette now uses it too**, which was left for its own change and turned
out to be the one that mattered. The palette used to *be* the focus target, and
a widget that owns the keyboard has to handle every key — so it had grown its
own caret, its own UTF-8 backspace and its own idea of what counts as typing.
The last of those was wrong: it named Up, Down, Home and End and let everything
else fall through to "this is text", so **Left and Right put a private-use
codepoint into the query**, which then matched nothing with no visible cause.
A test written against the old code confirmed it before the change.

Three things follow from focusing the field rather than the palette:

- The palette is no longer a focus stop (`acceptsFocus()` is false) and exposes
  `keyboardTarget()`, matching the find bar. Keys still reach it as the field's
  parent, which is where Return, Escape and the arrows are handled.
- ⌘A, ⌘C, ⌘V and ⌘X in the palette now mean the query rather than the document,
  for free, via `Widget::isTextInput()`. Pasting into the palette previously
  pasted into the file.
- **Home and End changed meaning**, and deliberately: they used to jump the list
  to its first and last row, and now the field takes them and they move the text
  caret. That is what VSCode does and what anyone typing into a box expects; the
  way to reach a distant command in a fuzzy palette is to type, not to travel.
  The palette's Home/End branch is now unreachable dead code, so removing it is
  invisible to any test — which is itself the honest reason for deleting it.

**Verified by rendering the assembled tree off-screen**, not by driving a live
window: `Tests/FindRenderTests.cpp` builds a real `EditorWidget` and `FindBar`
in a real `WidgetHost`, wires them the way `Main.cpp` does, and drives them with
synthesized keys and clicks through `renderToImage`. That covers the toggles
changing what is lit, replace-all rewriting what is drawn, Escape clearing the
highlight, and an edit to the document refreshing the hits. It needs no focus
and runs in CI — see §9 for why the live-window version was the wrong instinct.

**Also done: the menu bar, and opening things.** File, Edit, Find and View, plus
the standard application menu — which is also where ⌘Q comes from, since macOS
provides it only through a menu bar and ECode had none. ⌘O opens a file and ⇧⌘O
opens a project folder, both through `Apps::chooseFile`/`chooseDirectory`, which
already existed upstream. `MenuBuilder` turns a list of command ids into the
bar: titles come from the registry, shortcuts from `Keymap::chordFor`, and
availability from the command's own `isEnabled`, so a menu holds no strings
anyone maintains by hand.

Four decisions worth recording:

- **A menu shortcut is not a keystroke, and that changes who gets it.** macOS
  matches key equivalents against the menu bar *before* the window is sent a key
  down. So the moment Paste is in the Edit menu, a ⌘V that used to arrive at a
  focused find field as an event arrives as a command instead — and every piece
  of logic that decided what ⌘V meant was written against a key event and no
  longer runs. Left alone this pastes into the file being searched, which §7.4
  already names as the mistake worth a virtual to prevent. `Widget::runCommand`
  is `isTextInput` asked about a command id, `WidgetHost::runCommandOnFocus`
  applies the same precedence, and both the keymap and the menu now converge on
  one dispatcher rather than on the registry.
- **The precedence moved out of `Main.cpp` so it could be tested.** It is six
  lines, and it was six lines no test could reach — `App` is not a library. On
  `WidgetHost` it is covered from both sides: the field claims the four editing
  ids even with nothing selected, and passes everything else on.
- **`runCommandOnFocus` deliberately does not bubble, though `keyDown` does.** A
  find bar holding a query box is not itself a text input, and continuing the
  walk past the box would let the bar answer for commands meant for the
  document.
- **Opening a *file* does not move the sidebar; opening a *folder* does.** The
  root followed the open file's parent because that was the only thing there was
  before a folder command. Keeping that would throw away the project someone is
  working in as a side effect of ⌘O.

**Verified in the running app**, which is where this one belongs: the menus are
structure and platform behaviour rather than pixels, so `renderToImage` has
nothing to say about them. Read back through the accessibility API rather than
judged from a screenshot — every title, every shortcut and modifier mask, and
the enabled flag on each item. Revert File, Undo, Redo, Cut and Copy all come up
greyed on a freshly opened clean file and Paste does not, which is the whole
chain end to end: command predicate → `MenuItem::isEnabled` → `validateMenuItem:`.

**One thing the automation taught by accident**, and it is the §9 lesson again
from the other direction: enumerating the menus via System Events *opened* them,
which ran Open Folder — the tree jumped to the project root and focus moved to
it. That looked like a bug for a while and was the feature working. Driving the
live window also meant fighting other applications for the screen, and two
captures came back showing the wrong app entirely. The menus were worth checking
live; almost nothing else is.

**Also done: the in-window context menu.** Right-click in the editor gives
Cut/Copy/Paste, Select All, Undo/Redo and Find, greyed from the same predicates
the menu bar reads. `Graphics::Menu` is the native menu bar and nothing else —
no platform eacp supports has a `popup(at:)` — so this is drawn by us, and it
took the shape the palette established: a child of the root covering the window,
because covering it is what catches the click that dismisses.

Right-click needed no framework work, which was the surprise. `rightMouseDown:`
is already registered on the macOS backing view and routed to the same handler
with `button = Right` — but *nothing in ECode had ever read `MouseEvent::button`*,
so a right-click had until now been an ordinary click that moved the caret.

Five decisions worth recording:

- **A right-click inside the selection leaves it alone.** Collapsing it first
  would mean the Copy directly below the pointer copied nothing. Outside it, the
  caret moves first, so the menu refers to where the person pointed.
- **The release chooses, not the press.** That is what makes press-drag-release
  work, which is how a menu opened by holding the button behaves — and releasing
  outside cancels, which is how anyone backs out of one opened by mistake.
- **A menu with nothing to offer does not open.** Unknown ids are dropped and
  separators collapse, so a menu can resolve to nothing; a popup appearing empty
  under the pointer reads as a glitch rather than as an answer.
- **It opens with nothing highlighted.** The pointer is at the box's corner, not
  on a row, and pre-selecting the first item would mean a stray Return ran
  something nobody pointed at.
- **The choice is dispatched, not run.** Same reason the menu bar's items are: a
  focused text box may claim it. Both menus and the keymap now arrive at
  `dispatchCommand`.

This is also the first thing in the app that **tracks the pointer** — the theme
said "no hover colour: nothing tracks the pointer yet", and `mouseMoved` was
never forwarded from the GPU view because nothing wanted it. Both are now true
only of everything else.

And it makes the palette's modal special case a **pattern rather than a case**:
two overlays now want every non-chord key. `modalOverlay()` names them in one
place. Still not worth inventing `when` contexts for two — worth recording that
a third would be the moment.

**Also done: the splitter, and the cursor that makes it legible.** The sidebar
resizes by dragging the seam, clamped so neither pane can be squeezed out of
existence.

Three decisions worth recording:

- **The splitter holds a position, not the panes.** A splitter owning its two
  children would have to own their layout too, and every arrangement here already
  has a parent that knows how its own rect divides — the window layout takes the
  sidebar off the left with `removeFromLeft` and passes the rest on. So it reports
  where the divider is and the parent lays out around it.
- **Drawn thin, grabbed thick.** One point of line inside eight points of grab
  band. Matching them gives either a target nobody can hit or a bar through the
  middle of the chrome, and the band is centred on the line so it is equally
  reachable from both panes.
- **The drag keeps its grab offset.** Snapping the divider's centre to the
  pointer means a visible twitch of up to half the band on every single drag.

**A widget reports a cursor rather than setting one**, because setting one needs
the `Graphics::View` that owns the GPU surface and there is exactly one of those
for the whole window. `Widget::cursor()` says what it wants,
`WidgetHost::cursorAt` asks whichever widget is under the pointer, and the
application applies the answer. The captured widget answers first, which is what
holds the resize shape while the divider is dragged past its own band — a pointer
reverting to an arrow mid-drag reads as the drag having been dropped. The editor
now asks for an I-beam, which is the other half of gap 7's original point.

**Every widget §7.4 planned now exists.** What is left here is not a widget: the
minimap, tooltips, hover states beyond the three that now have them, and
animation.
The two structural debts §7.3 named are both closed — the line mapping is in and
damage tracking with it — so what remains of variable line height is the
arithmetic behind one function rather than an assumption spread through the
renderer.

On overlays: the palette is the first, and it confirms the shape those want: a child of the root
laid out over the whole window, because `PaintContext` has no notion of a layer
escaping its parent's clip and covering the window is also what makes a click
outside dismiss it. The find bar is deliberately *not* that shape — it is sized
to its own box, because a widget laid out over the editor's full width swallows
every click meant for the text under it.

### 7.5 IME — the largest remaining framework gap

Still absent from eacp: no `NSTextInputClient`, no `interpretKeyEvents:`, no
`WM_IME_*`. CJK, dead keys and the emoji picker are all unavailable, and it
cannot be layered on from app code.

The implementation can wait; the **marked-text range in the cursor model should
not**. Composition means the document holds provisional text that is styled
differently and is not yet a real edit. Retrofitting that through `Editor`,
`EditHistory` and the renderer is exactly the kind of change this plan has been
trying to avoid — and multi-cursor has now landed on top of all three, so the
warning is about a larger surface than it was.

Though §7.2 is also the evidence for the other reading: the surface that
mattered there stayed small because it went through `Editor` rather than around
it, and marked text can be held to the same rule. It is a range on the
`CursorSet`, not a second thing views reach into.

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
  stub returning `isValid() == false`. The porting notes are in its header, and
  this is the first thing standing between ECode and Windows — ahead of the menu
  bar, which now exists.
- **Gamma-correct blending.** Planned in M2 and not done. This is the difference
  between "looks native" and "looks slightly off", worst on light-on-dark. It
  needs the per-cell background colour plumbed into the glyph shader, which is
  the part that is hard to retrofit — see §4.
- **Shaping and ligatures.** `GlyphAtlas` maps one codepoint to one glyph.
  Fira Code's `=>` needs CoreText/DirectWrite line shaping behind the existing
  seam, plus a run cache — Ghostty measured shaping at 96% of frame time before
  adding one.
- **`command` is the Windows key on Windows.** `Keyboard::isCommandPressed`
  reports `VK_LWIN`/`VK_RWIN`, so every ECode binding — all of which are
  `cmd+…` — needs Win+S rather than Ctrl+S there. The menu bar prints "Ctrl+S",
  because that is the convention and Win32 accelerator text is decorative
  either way, so the two currently disagree. The fix is to make `command` mean
  the platform's primary accelerator modifier (Ctrl on Windows), which is what
  every cross-platform toolkit does — but it changes keyboard semantics
  framework-wide, touching `GlobalHotKey` and `TextInput`, so it is a decision
  rather than a patch.
- **LSP.** `Processes::runAsync` returning `Async<T>` is the right foundation;
  diagnostics, completion and go-to-definition after the chrome exists.
- **macOS injects items into menus it recognises by name.** The Edit menu comes
  back with AutoFill, Start Dictation and Emoji & Symbols appended; the View
  menu with Show Tab Bar, Show All Tabs and Enter Full Screen, all three greyed.
  Left alone deliberately. The two text-input ones are suppressible with
  `NSDisabledDictationMenuItem` / `NSDisabledCharacterPaletteMenuItem`, but that
  needs ECode to carry its own `Info.plist` instead of eacp's shared template,
  and both items become *correct* the moment IME lands (§7.5) — so suppressing
  them now buys tidiness and costs a thing to remember to undo. AutoFill has no
  documented key at all. Window tabbing would need `allowsAutomaticWindowTabbing`
  exposed from eacp, and "Enter Full Screen" being disabled is a window-options
  gap that predates the menus.

### 7.8 More than one file open — done

⌘N, ⌘O and the file tree each open into their own tab; ⌃Tab and ⌃⇧Tab cycle,
⌘W closes, the × on a tab closes it and so does a middle click, and `ECode
a.cpp b.cpp c.cpp` opens all three. The strip shares its width between the tabs
down to a floor, and past that floor it overflows and scrolls to keep the active
one in view.

The model is `Workspace` in `ECodeCore`: a list of `OpenFile`, an active index,
and nothing about widgets. An `OpenFile` is a `TextFile`, a `Highlighter` and a
scroll offset.

Decisions worth recording, because each went against the obvious version:

- **The set is never empty.** Closing the last tab leaves an empty untitled
  file rather than nothing. Everything downstream — the title, the status bar,
  the renderer, every editing command — is written against "the active file",
  and an absent one is a null check at each of them plus a window with no
  caret, which reads as broken rather than as empty. It also made untitled
  buffers reachable for the first time, which is why `TextFile::saveAs` and ⌘N
  arrived with this rather than later: a buffer that cannot be saved is not a
  place to type.
- **One highlighter per open file, not one per workspace.** A shared one would
  reparse from scratch on every switch — the cold open above, paid on a ⌃Tab —
  and in between would hand the renderer a tree describing text that is not on
  screen. ~~The cost lands at launch instead: `ECode *.cpp` parses every file up
  front.~~ **Half right, and the wrong half.** It does not: nothing calls
  `update` on a file nobody is looking at, so an unopened tab is never parsed —
  the parse lands on the first ⌃Tab onto it, 8.6 ms, once. What `ECode *.cpp`
  *did* pay per file was the 14 ms query compile in the constructor, and §7.9
  fixes that by sharing it.
- **The highlighter factory is a constructor argument, not a settable member.**
  The workspace builds its first file inside its own constructor, so a factory
  installed afterwards leaves exactly that one file — the one a launch with no
  readable path lands in — permanently uncoloured while every file opened after
  it is fine. Found by a render test that draws the *same* text through two
  files and demands the two frames be identical.
- **The scroll offset lives on the `OpenFile`, not on the widget.** The
  alternative is stashing it on the way out of a tab and restoring it on the way
  in, and a step that has to be remembered at every call site that switches is
  one that will eventually be forgotten — the symptom being the view jumping on
  a tab switch with nothing on screen to explain it. Same shape as §7.3's "the
  clip and the glyph batch are owned by the same object, because they are
  coupled and the coupling is silent".
- **Paths are compared after resolving.** `FilePath::operator==` is a string
  comparison, so `dir/sub/../a.txt` and `dir/a.txt` would be two tabs over one
  file — two undo histories and two dirty flags over one set of bytes, with
  whichever saved last winning silently.
- **A dirty tab refuses to close**, exactly as §7.1's save refuses to clobber:
  the title carries the question and a second ⌘W answers it. What is new is that
  the question has to go *stale* — type one character and it is a different
  question — and that is a comparison of `Editor::stateId` rather than a clear
  on an event, because an edit arrives by three routes (a keystroke, a menu
  paste, an undo) and only the first runs the editor's own key handling.
- **`Highlighter::applyEdit` and `reset` moved onto the interface**, the same
  move §7.3 made for `update` and for the same reason: the workspace wires each
  file's editor to that file's highlighter and has no business knowing which
  implementation it holds.
- **`TextFile::pollDisk` is the file's policy now, not the window's**, so every
  open file is watched rather than only the visible one. A tab switched to an
  hour after a `git checkout` should show what is on disk, and finding out at
  the moment of the switch is too late to warn about a conflict.
- **Widgets are told the pointer has left them.** `mouseMoved` only ever reaches
  the widget under the pointer, so anything narrower than the window that lights
  up would stay lit for good — only the host knows what was hovered a moment
  ago. `Widget::mouseExited` plus `WidgetHost::hovered` is the third hover state
  in the app and the first that needed the widget layer changed for it.
- **A tab is selected on the press and closed on the release.** Selection has to
  follow the press or the strip feels a frame behind the hand; closing follows
  the release for the reason §7.4 records for the context menu, and it is what
  lets a press on the wrong × be backed out of by dragging off it.

**Not done, deliberately:** dragging tabs to reorder, a tab context menu, and a
"3 unsaved files" prompt on quit — `Workspace::hasUnsavedChanges` exists and
nothing asks it yet. And the note §7.3 left for whoever adds tabs is still owed:
the `RowCache` belongs to the app's one `TextRenderer`, so a switch drops every
cached row. Correct today because only one editor is ever visible; two side by
side would thrash it, which is what makes editor groups the next thing here.

**Looked at, and it changed the code.** The strip dumped off-screen and opened
showed what no assertion had thought to ask: at minimum width the filename ran
*under* the close button and was cut mid-character, because a tab's own clip
stops text at the tab's edge, which is past the ×. `UIText::elide` and a wider
floor fixed it, and both are pinned by tests now — including one that compares
the close button's box against the same tab with a short title, so any
difference is the title having arrived somewhere it should not.

**Then run, and it crashed on the second thing tried** — ⌘N, inside
`Document::columnAt`, drawing the status bar. `Document()` was `= default`, so
the line index was empty and `lineCount()` returned zero, contradicting the
invariant written three lines below it in the same header. It had been wrong for
as long as the file has existed and had never been reachable: the app opened a
file into its one buffer before drawing a frame. Untitled buffers made it
reachable. 508 tests were green.

That same run found a second thing, smaller and older: **the dirty dot did not
appear after a menu Paste.** Commands that change the document through the
registry — paste, undo, cut, select-all — never reach the editor's key handling,
which is what normally pushes a change into the chrome, so the tab looked clean
over text that was not until the next keystroke. Every command already converges
on `dispatchCommand`, so that is where the refresh went.

**Verified live where it belongs.** `renderToImage` answered the strip; the menu
bar and the workspace behind it were driven in the running app through the
accessibility API, as §7.4 established: launched over three files, ⌃Tab cycling
alpha → beta → gamma → alpha, Next/Previous Editor greyed with one tab open and
live with two, a menu Paste lighting the dot, ⌘W refusing and saying so in the
title, another edit making that question stale, and a second ⌘W taking it.

### 7.9 The cold open — done, and the plan was wrong about what it was

This section carried "opening an 8,000-line file spends ~40 ms parsing it before
the first frame, essentially all of it tree-sitter's initial parse" for months,
and §7.8 added that `ECode *.cpp` therefore "pays the parse once per file at
launch". Measured, both sentences turn out to be wrong, and in a way that made
the stated fix aim at the smaller half.

**What a cold open is actually made of**, at `-O2`, off-screen so no window
server is in the way (§9), for a generated 8,323-line C++ file:

| | before | after |
|---|---|---|
| `SyntaxHighlighter` constructor | 13.6 ms | **0.00 ms** after the first |
| first `update` — parse + query | 10.0 ms | 2.0 ms, then 4 more frames |
| worst frame during the parse | 10.0 ms | **2.02 ms** |
| **launch over 20 files × 8,000 lines** | **295 ms** | **11.8 ms** |
| typing one character | 9.6 ms | **0.24 ms** |
| first ⌃Tab onto an unparsed tab | 8.6 ms | 8.6 ms, in 2 ms frames |

Three findings, in ascending order of how wrong the plan was.

**One: the 40 ms was not the parse.** It was 13.6 ms of `ts_query_new` plus
10.0 ms of parsing plus ~5 ms of `renderToImage`'s own off-screen target, and the
plan had rounded the lot up and attributed it to tree-sitter's parse. Compiling
the query is the single most expensive call in a cold open, and it analyses each
of C++'s 77 patterns against the whole grammar — it is a function of the
*grammar*, not of the file, and it costs the same for a 500-line file as for a
30,000-line one. The glyph prepass and the draw are 0.36 ms and 0.44 ms; the
renderer was never the problem.

**Two: §7.8 multiplied the query compile, not the parse.** A file nobody is
looking at is never parsed — `update` is only ever called for the visible editor,
so an unopened tab holds a highlighter with no tree, and the parse lands on the
first ⌃Tab onto it. What every tab *did* pay, eagerly, in its constructor, was
those 13.6 ms. Twenty files was 295 ms of launch, 280 of it compiling one
identical query twenty times.

So the first fix is not the one this plan named: **share the compiled query.**
`SyntaxLanguage` holds the grammar, the compiled `TSQuery` and the capture-index
table, built on first use and shared by every highlighter thereafter. Everything
derived from a *document* — the parser, its tree, the query cursor — stays with
the highlighter. Sharing is safe because the query is immutable once built: the
one call that mutates it, `ts_query_disable_pattern` for the predicated patterns
(see §7.4), happens in the constructor before anyone else can see it, and
`ts_query_cursor_exec` takes it as `const`.

**Three, and it is worth more than the cold open: every keystroke was reparsing
the whole file.** `reparse` discards its tree when the document's length no
longer matches the length it parsed — a safety net for a document swapped out
behind its back — and `applyEdit` never updated that length after telling the
tree about an edit. So the net fired on every edit that moved the length, which
is very nearly every edit, and the incremental reparse that `applyEdit` exists
for had never once run. The only edits reaching the fast path were the
same-length ones, which are exactly the ones the length check cannot catch.

One line fixes it, and the interesting part is why it survived: a from-scratch
parse and an incremental one arrive at the same tree, so the oracle test that
compares an incremental reparse against a fresh parse agrees either way. §9
already had the general form written down — *an oracle proves the answer, never
the path* — and this is the second time in this project that the missing thing
was a counter. `SyntaxHighlighter::fullParses()` is it.

**Then the first frame, which is what the section originally asked for.** The
parse now runs under a wall-clock budget: `ts_parser_parse_with_options` with a
progress callback that cancels once 2 ms are gone, and tree-sitter resumes where
it left off on the next call. `hasPendingWork()` says the parse is unfinished,
`EditorWidget` asks for another frame, and the file is on screen and scrollable
in the first one. Probed before designing around it, since the documentation
describes resumption in a sentence: a budgeted parse of 8,000 lines came back
byte-identical to an uninterrupted one — `ts_node_string` compared, 646,186
characters both ways — and cost 9.79 ms against 10.59, so slicing it is free.

Decisions worth recording, because each went against the obvious version:

- **The budget is off by default and the *app* asks for it.** The obvious version
  makes it the default so nobody can forget, which is what §7.8's
  highlighter-factory lesson would suggest. It is wrong here, and the tests said
  so before the reasoning did: a budget generous enough to be invisible at `-O2`
  interrupts a 500-line parse in a debug build, so four existing tests went red
  in Debug and passed in Release. A default that makes `update()`'s answer depend
  on how the binary was compiled is worse than one that has to be asked for.
  Whoever owns the frame is also the only one who knows there is a frame to give
  back.
- **The old tree is not replaced until a new one arrives.** A cancelled parse
  returns null, and assigning that would throw away both the tree a resumed parse
  needs as its starting point and the last good colouring. Keeping it means a
  reparse holds its previous colours while it runs, rather than the file flashing
  to plain on an edit.
- **A pending parse runs no query.** The tree does not describe this text yet, so
  querying it would spend the frame's most expensive call on spans about to be
  replaced. Nothing is forgotten either, which is the same point from the other
  side: what is held is still the best answer available.
- **An edit mid-parse restarts it rather than resuming.** The parser holds a
  position in text that no longer exists, and resuming from it builds a tree
  describing neither version — not stale, *wrong*. `ts_parser_reset` is the
  documented way to abandon it, and `parseRestarts()` counts them.
- **`version()` moves when the parse lands, not per slice.** Per slice would drop
  the §7.3 row cache on every frame of the parse; never would leave the file drawn
  plain for as long as it stayed open.
- **A parse begun is counted where it begins, not where it runs.** A budgeted
  parse comes back through `reparse` once per frame, and all of those are one
  parse — the distinction between `fullParses` and "calls" is what makes the
  counter mean anything.

**Verified three ways, and the counters are what make two of them tests at all.**
`Tests/ColdOpenTests.cpp` covers the model: the query compiled once for twenty
highlighters, typing not moving `fullParses`, an unreported change still moving
it, a budgeted parse agreeing span-for-span with a whole one, a one-microsecond
budget still terminating, the version moving only on completion.
`Tests/ColdOpenRenderTests.cpp` covers the frame, through a real `EditorWidget`
in a real `WidgetHost` rendered off-screen: the first frame draws the text with
the parse still pending, it asks for another frame, the colours arrive, and once
they have it stops asking. Eight mutations were tried and all eight went red.

Then it was looked at — two frames dumped to files and opened, which is what §9
settled on. Frame 1 of an 8,000-line file is the text, the gutter, the line
numbers and the caret, all correct and entirely plain; frame 5 is the same
picture coloured, with nothing moved. The app was also launched on that file and
on twenty copies of it, and came up and stayed up; a screen capture came back
black, which is the same wall §9 already ends on.

**Not done, deliberately:** the remaining 8.6 ms is one parse of one file, spread
over five frames, and there is nothing left to take out of it without threads or
a smaller grammar. Two things are now the next numbers here rather than this one:
the **first ⌃Tab onto a large tab** still pays that parse, which could be started
in the background at open instead of at first sight; and a **100 MB file**, which
neither this nor §7.6 has measured. `Document::fromText` is 0.41 ms for 556 KB,
so the line index scales linearly and the string is the thing to watch.

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
- **`Graphics::Rect` changed meaning.** Its splitters are now y-down, matching
  the rest of eacp. Nothing in eacp, ECode or CowTerm compensated for the old
  behaviour, so the fix corrected four call sites rather than breaking any —
  but the forks (`JamieEACP`, `eacpTest`, `eacp-cleanup`) still carry the y-up
  version, and merging across that boundary will silently invert layouts.
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
- **Arithmetic cannot tell you which way is up.** `Graphics::Rect`'s splitters
  were y-up in a y-down framework, so `removeFromTop` returned the bottom
  slice and ECode drew its tab bar along the bottom edge for months. A unit
  test on `Rect` would not have caught it — it can only confirm the maths
  agrees with whatever convention the author had in mind, and the author had
  the wrong one. It took rendering the slices off-screen and asking which end
  of the *image* they landed on. Anything that is a convention rather than a
  computation needs a test at the level where the convention is observable.
- **And check it fails in the direction that costs you something.** The first
  dirty-flag test picked a sequence where the naive implementation happened to
  answer correctly, so it passed against the thing it was written to rule out.
  A wrong answer has two directions and usually only one of them hurts: here,
  falsely *clean* skips the save and loses the work, while falsely dirty just
  writes a file twice. Aim the test at the expensive one.
- **A test of the common case cannot see what only the rare case separates.**
  `ClipScope` intersects a child's clip with its parent's. The first test for
  it drew a tab title too long for its tab and asserted the strip beyond stayed
  bare — and it passed with the intersection replaced by a plain assignment,
  because a tab lies *inside* the tab bar, where narrowing and replacing are the
  same operation. Nearly every widget fits inside its parent, so nearly every
  arrangement is blind to the difference. It took a child deliberately laid out
  four times the size of its parent, asserted on all four sides. When a test
  covers a fold in the behaviour, build the case that lands on the fold.
- **A test can be unable to fail on your machine.** The clamp that keeps a
  negative row index from becoming an enormous unsigned one is guarding
  undefined behaviour — and on arm64 the UB happens to do the right thing, so
  the test written for it passed with the clamp deleted. Confirmed with a
  two-line program rather than assumed: `(size_t)(-10.f)` saturates to 0 here.
  The clamp stays, because the guarantee is not the platform's to give; but the
  test was rewritten to pin what it can actually observe, and to point at the
  sibling clamp that *is* load-bearing. A green suite is not evidence until you
  know which of its tests could have gone red.
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
- **An 8-bit drawable makes "slightly" free, so never assert on "slightly".**
  The palette's backdrop is meant to *dim* what is behind it, and the test for
  it asked whether the pixel came back darker than the panel underneath. It
  passed with the backdrop deleted outright: `0.102f` written into an 8-bit
  target reads back as `0.10196`, so a plain `<` holds on rounding alone. The
  fix is to demand a margin the effect comfortably clears and quantisation
  cannot — the backdrop is 45% black, so a fifth is not close to either. The
  general form: when a test compares a rendered value against the value that
  was written, the round trip is not lossless, and a strict inequality is
  satisfied by the loss.
- **A whole-row highlight swallows an ink count.** Counting pixels that differ
  from a region's background says nothing about the region if something already
  covers all of it — the palette's selected row is tinted edge to edge, so
  "more ink with the shortcut than without" came out equal. Two renders
  compared against *each other* over the column in question answered it, plus
  the complementary check that the rest of the row is byte-identical, so the
  difference is a shortcut appearing rather than the row shifting.
- **A sort of equal elements cannot be caught being unstable.** libc++ leaves
  an all-equal range where it found it, so a stability test built from
  identically-scoring commands passes against `std::sort` at any length. It
  takes two score classes interleaved, so the ties genuinely have to be carried
  past each other.
- **Two correct halves can compose into a wrong whole, and only a test that
  draws both at once can see it.** Search found the right ranges and the
  renderer had two hit colours, both tested. In the app the current hit came out
  the *selection's* blue: finding a hit selects it, so the two always coincide
  there, and the selection was being painted over the highlight. Every unit test
  passed, and so did the render tests — because none of them set a cursor and a
  match list in the same frame. The arrangement that was broken was the only
  arrangement the app is ever in. When two features are correct separately, the
  test worth writing is the one that puts them in the same picture.
- **Render the assembled tree off-screen; do not drive the live window.** The
  bug above was first spotted by launching the app and sending it keystrokes,
  which meant stealing the screen, fighting other apps for focus, and a
  screenshot that cannot run in CI. The same finding came out of a
  `renderToImage` test over a real `WidgetHost` in a few lines — synthesized
  keys and clicks into the actual widgets, pixels read back. `View::renderToImage`
  is what this framework has instead of a robot, and it is strictly better:
  no focus, no window, deterministic, and it stays as a regression test
  afterwards. Reserve the running app for judging how something *feels*.
- **A test written against a convention can be wrong about the convention.**
  The first key-equivalent test asserted `Chord::parse("cmd+")` was invalid and
  therefore converted to nothing. It went red, and the code was right: `"+"` is
  both the separator a binding is written with and a key in its own right, and
  `parse` documents the trailing one as the key. ⌘+ is a real shortcut in most
  applications. The test now pins that it *does* convert. Worth recording
  because the instinct on a red test is to fix the code.
- **The automation can be the thing that changed the state.** Reading the menus
  through System Events opens them, and opening them ran a command — the file
  tree jumped to a different root and focus moved, which read as a bug in the
  new code for a while. Driving a live window also means competing for the
  screen: three captures came back showing an entirely different application,
  and in the end ECode simply would not come to the front at all. Both are the
  same lesson §9 already ends on, met from the other side. **`renderToImage` is
  the answer to "let me look at it", not just to "let me assert on it"** — dump
  the snapshot to a file and open it. No focus, no stolen screen, and the same
  picture.
- **A region assertion is only as good as its region.** The separator test asked
  whether the band holding the rule was brighter than an empty strip, and it
  passed with the rule deleted: the band spanned the box's full width, so the
  *border* supplied the peak. Inset past the border it fails properly. The
  general form — when a test samples an area, check nothing else bright lives in
  that area, or it will answer about the wrong thing.
- **A sample point can miss by a pixel and take three tests with it.** The
  splitter's line is one point wide centred in an eight-point band, so it lands
  at `x - 0.5` and the lit column is the one to the *left* of the divider.
  Three render tests sampling the divider's own column all failed against
  correct code. A peak across the band answers whichever side of the half-pixel
  it falls on. Sub-pixel placement is normal; a test that names one pixel is
  asserting on rounding.
- **Looking is still worth it after the tests are green.** The context menu had
  38 passing tests, two of them mutation-checked, and rendering one to a file
  showed two things immediately that none of them asked about: the highlight bar
  painted over the box's border, breaking the outline along the row the eye was
  on, and a disabled row printed its shortcut *brighter* than its own greyed
  title, because the shortcut colour is lighter than the disabled one. Both are
  now pinned by tests that fail without the fix. Tests answer the questions you
  thought to ask.

- **An oracle cannot see a fallback.** The incremental line map is compared
  against a full rebuild after every edit, which is the right shape — and it
  passes with the whole optimisation replaced by `rebuild()`, because a rebuild
  agrees with a rebuild. Confirmed by doing exactly that. What makes the test
  real is a counter of full rebuilds, asserted not to move. Any optimisation
  whose fallback is the thing it optimises needs that second assertion, and the
  general form is: an oracle proves the answer, never the path.
- **Pick the input where the two implementations disagree.** Two word-wrap tests
  passed with word breaking deleted outright, because at the widths chosen the
  greedy *character* break happened to land on a space anyway. Wrapping "the
  quick brown fox" at ten columns cannot tell character wrap from word wrap;
  at thirteen it can. When a test compares two algorithms, the width, the
  length and the spacing are not decoration — they are the test.
- **A guard can be reachable only by a route you have to construct.** The
  correction that keeps a held column from falling through to the row below
  survived deletion at first, because reaching it needs the column to be held
  *across* an intervening row: a caret already on a full continuation row can
  never be at its end, since that offset belongs to the row beneath. It takes
  three rows and two presses of Up. When a mutation does not go red, the useful
  question is not "is the test weak" but "what state does this code actually
  need", and the answer is worth writing into the test.
- **An observer that does not disturb the state may not read it either.** The
  new View-menu checkmark looked broken for a long time: toggle word wrap, read
  the item's `AXMenuItemMarkChar`, get nothing. It had been correct the whole
  time. A checkmark is set inside `validateMenuItem:`, which AppKit calls when a
  menu is about to be *drawn* — so reading the attribute reports whatever the
  last draw set, and the attribute read does not itself cause one. Addressing
  the item by name (`menu item "X" of menu 1 of …`) never opens the menu and so
  always reads stale; enumerating all of `menu items` does open it, which is why
  the one reading that worked was the one written as a loop. §9 already says the
  automation can be the thing that changed the state; the other half is that it
  can also be the thing that failed to. Two `fprintf`s — one in the command, one
  in the predicate — answered in a single run what an afternoon of reading the
  attribute could not: `checked=0`, then the command, then `checked=1`.
- **And ECode still would not come to the front.** ⌥Z sent through System Events
  went to Terminal, which is the same wall §9 already ends on. The menu carries
  the chord as a native key equivalent — read back through AX as `Z` with the
  Option-without-Command mask — and macOS matches those before the window sees a
  key at all, so the path is as verified as this machine allows. It has not been
  pressed by a person.
- **A measurement can be right about the total and wrong about every part of
  it.** The cold open was carried as "~40 ms, essentially all of it tree-sitter's
  initial parse". The total was in the right neighbourhood and the attribution was
  not: 14 ms of it was compiling the highlight query, which depends on the grammar
  and not on the file, and the parse was 10 ms. Everything the plan proposed —
  draw unhighlighted, parse behind it — aimed at the 10. §9 already said to
  measure before believing a debt; the other half is that a *number* is not a
  measurement until it is broken down, because a fix is aimed at a part and never
  at a total.
- **The oracle lesson has a second victim, and it cost more than the first.** An
  incremental reparse and a from-scratch one arrive at the same tree, so the test
  that compares them agrees either way — and it agreed for as long as `applyEdit`
  forgot to keep `reparse`'s length check in step and *every keystroke reparsed
  the whole file*. 9.6 ms a character on an 8,000-line file, in a suite of 508
  green tests, behind a test written specifically to cover that path. The rule
  from `LineMap::rebuildCount` is now twice-earned: any optimisation whose
  fallback is the thing it optimises needs a counter, and the counter is the test.
- **The build type is a hidden input to a wall-clock default.** A 2 ms parse
  budget is invisible at `-O2` and interrupts a 500-line parse in a debug build,
  so making it the default turned four existing tests into ones that passed or
  failed depending on how the binary was compiled. It showed up immediately, which
  is the only reason it is a lesson rather than a bug: a default whose behaviour
  depends on the compiler's optimiser has no business being a default.
- **Read the library's promise, then check it.** The whole deferred-parse design
  rests on one sentence of tree-sitter's documentation — that a parse cancelled by
  the progress callback resumes on the next call. Confirmed before anything was
  built on it, by parsing the same 8,000 lines twice and comparing the trees as
  strings: 646,186 characters, identical, and the sliced version was no slower.
  Cheap, and the alternative is discovering it through a subtly wrong syntax tree.
- **A loop that tests before it acts can measure the wrong frame.** Two tests here
  were written as `while (hasPendingWork()) render();` — and nothing is pending
  until a frame has asked for it, so the loop never ran, and what came after it
  measured the very first frame instead of a settled one. One failed loudly; the
  other would have passed while asserting nothing. `do`/`while` is the shape, and
  the tell is a loop whose condition depends on something the body causes.
- **A snapshot at a scale the harness does not share is a lie.** The wrapped
  view dumped through `renderToImage(2.f)` came back with the right half of
  every row missing, which looked exactly like a clipping bug in the new code.
  It was the test view passing `1.f` to `PaintContext` while rendering at 2× —
  geometry scaled, scissor did not. The app passes its real backing scale. Dump
  at the scale the context was built with, or fix the context.

- **Run the app.** The red-text bug — an R8 mask through a tint-multiplying
  shader — passed every test that existed and was obvious in one screenshot.
  Still true, and the point is narrower than it looks: run it to see whether
  something is *right*, then write the off-screen test that proves it stays so.
- **And the second thing you try in it is where the crash is.** 508 tests green,
  the tab strip rendered off-screen and inspected, the model covered by 23
  mutations — and ⌘N in the running app segfaulted immediately, because
  `Document`'s default constructor never built its line index and `lineCount()`
  therefore reported zero, contradicting a comment three lines below it. The
  code had been wrong since the file was written and *unreachable*: the app
  opened a file into its one buffer before drawing anything. The general form —
  a new feature does not only add code paths, it makes old ones reachable, and
  those have never been executed by anything. Ask what your change lets happen
  for the first time.
- **An invariant a class states in prose is a test you have not written.**
  `Document`'s header said "a genuinely empty document still has a single line to
  put the caret on" while `= default` left it with none. The sentence was true of
  `fromText` and false of the constructor beside it, and no test asked either,
  because every existing test built documents from text.
- **Three tabs cannot tell a decrement from a clamp.** Closing the first of
  three with the last one active lands on the right file whether the active index
  is stepped down or merely clamped — the two arithmetic errors cancel. It takes
  four tabs and the third active. Same family as the word-wrap widths above: when
  a test compares two implementations, the *size* of the case is the test.
- **A guard whose point is a coordinate needs a case with that coordinate.**
  Dropping the tab strip's "is the point even inside the strip" check left every
  test green, because the strip in those tests started at x = 0 — so there was
  nowhere to the left for a scrolled-off tab's rectangle to be. In the window the
  strip starts at the sidebar's edge and that rectangle reaches back under the
  file tree. The test had to move the strip.
- **A refusal that is returned but not remembered passes a test of the return
  value.** `TextFile::save` reported `changedOnDisk` correctly while forgetting
  it had, and the existing test only read the result. Nothing was wrong until the
  window stopped being able to hold that state for the one open file — and then
  the failure is silent and expensive: the second ⌘S takes the ordinary path,
  refuses again, and the work can never be written at all.

- **A revert that keeps the file's old timestamp leaves the mutant in the
  binary.** The mutation script backed each file up with `shutil.copy`, which
  preserves mtime, and restored it with `shutil.move` — so after the revert the
  source was older than the object built from the mutant, ninja reported nothing
  to do, and the mutant stayed linked in. Every mutation after the first was
  therefore tested against an accumulation of the earlier ones, and the run
  afterwards was a suite failing for reasons that were not in the source. The
  revert has to *write* the file. This is the "verify the mutation applied"
  lesson from the other end: verify the revert applied too, and check the suite
  is green before the run and green after it.
- **A red detector that cannot see red.** The same script asked ctest for its
  verdict through `| tail -3`, which cuts off the "(Failed)" line and the
  percentage — so all thirteen mutations came back green and looked like
  thirteen worthless tests. The exit code is the answer. And a pipeline's exit
  code is the *last* command's, so piping ctest into anything throws it away.
- **Two independent guards for one invariant, and neither mutation goes red.**
  Deleting the revision comparison from the highlighter's memo leaves the tests
  green, because a reported edit already forgets the query through the reparse;
  deleting the forget leaves them green, because the revision comparison already
  catches it. That is information about the design and not about the tests: it
  says the invariant is held twice over. Worth keeping both, and worth writing
  down which one the test is actually exercising, or the next person reads a
  green suite as coverage of both.
- **An optimisation with no CPU-side observable can only be measured.** Skipping
  the glyph prepass for a cached row saves hash lookups. Nothing counts them —
  the atlas has no counter, and a stub glyph source sees nothing either, since a
  warm lookup never reaches it. So the mutation that disables the skip stays
  green, and the only evidence it does anything is the clock: 0.076 ms against
  0.058. What *is* tested is its precondition — that a cached row's glyphs are
  still in the atlas — which is the atlas generation in the cache's stamp. When a
  skip cannot be observed, test the thing that makes it safe.
- **Timing a live window measures what the window server allowed, not what the
  code costs.** Instrumenting the running app gave 0.5 ms a frame at 1200×800 and
  then *less* at 2500×1350, which is not how bigger windows work. An occluded
  window gets no drawable, the pass becomes a no-op and the paint measures
  nothing. The off-screen numbers are the quotable ones, for the same reason §9
  already prefers `renderToImage` for looking: no window server in the way.
- **Measure before believing a debt.** This plan carried damage tracking as "the
  next thing to bite" for months. It was 0.32 ms a frame. The measurement took
  half an hour, and it changed both the urgency and — via what it revealed about
  the drawable pool — the whole shape of the fix. A performance debt is a claim
  about numbers, and the numbers were never taken.
- **A test comparing two frames cannot fail if both are blank.** The idle-frame
  test asserts the reused frame is pixel-identical to the frame that built it,
  and it passed with the cache storing empty rows — two blank frames are equal.
  It took an ink count alongside the equality: the frame that skipped the work
  still has to have drawn something. Same family as the backdrop that dimmed by
  a rounding error.

- **Two implementations can agree on every sequence you would naturally write.**
  "Add a cursor below the bottommost" and "below the primary" are different
  rules, and swapping one for the other left all 566 tests green — because every
  add makes its own new cursor the primary, so in any run of presses that only
  goes downwards the two are the same cursor. Separating them needs a ⌥-click,
  which is the one thing that makes the primary an *upper* cursor. This is the
  held-column lesson from §7.3 generalised: when a mutation stays green, the
  question is what state the code actually needs, and the answer belongs in the
  test. Both directional tests now build that state on purpose and say why.
- **A channel that separates two colours in principle may not separate them in a
  pixel.** The caret is (0.55, 0.78, 0.98) and the text is (0.85, 0.87, 0.91), so
  "is this pixel blue" looked like it would find a caret — and it found one on
  every line, because a 70%-covered antialiased glyph edge reaches the same
  blue. Blue *minus red* is 0.43 against 0.06 and separates them with room to
  spare. The general form: when two things are distinguished by hue rather than
  by brightness, test the hue. Antialiasing turns every colour into a ramp
  towards the background, and a single-channel threshold reads a point on that
  ramp.
- **§9's own lessons come back.** The multi-cursor frames were dumped at 2× while
  the harness passed 1.f to `PaintContext`, and the right half of every row went
  missing — exactly the entry three bullets up, reproduced within an hour of
  reading it. It looked like a clipping bug in the new renderer code for about a
  minute. Writing a lesson down does not stop it; recognising it faster is the
  whole return.
- **A "grey it out" predicate can only be read while the menu is being drawn.**
  Checking that Collapse To One Cursor greys with one cursor and lives with two
  read `false`, `false`, `true` through three states it should have read
  `false`, `true`, `true` — one step behind, every time. `validateMenuItem:` runs
  when AppKit is about to draw the menu, and `click menu item "X"` does not draw
  it, so the attribute reports the state at the previous draw. Enumerating
  `menu items` does draw it, and the same three reads then came back correct.
  §9 already recorded this for the View menu's checkmark; it applies to
  `enabled` for the same reason, and the tell is a sequence of readings that are
  each right for the *previous* step.
