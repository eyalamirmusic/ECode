# CLAUDE.md

Guidance for Claude Code (claude.ai/code) when working in this repository.

## Git Rules

Never commit or push without explicit permission from the user in the current
conversation. This applies to the eacp checkout too, not just this repo.

## Project Overview

ECode is a **VSCode-style code editor drawn entirely on the GPU** — no webview,
no native widget toolkit. Modeless editing, mouse-first, standard chords,
multi-cursor, command palette, sidebar tree, tabs, panel, status bar. It is
**not** a vim-style editor.

It is also a **library**. `ECode::Editor` is a single-file code view — read-only
or editable, no tabs, no sidebar, no palette, no command registry — that another
project embeds by constructing one `CodeEditorView`. Keeping that target small is
a live constraint, not an aspiration: see "The library split" below.

It is built on [eacp](https://github.com/eyalamirmusic/eacp), which is fetched
with CPM. Much of the work is currently *in* eacp rather than here: the editor
needs framework capabilities that did not exist yet, and those belong upstream.
`PLAN.md` is the live design document — read it before starting anything
substantial, and keep it current when a decision changes.

## Build

```bash
just build          # configure + build
just run            # build + launch
just clean
```

Or directly, which is what the justfile does:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DEACP_UNITY_BUILD=OFF \
      -DCPM_eacp_SOURCE=$HOME/Code/eacp
cmake --build build --target ECode
```

Two flags that matter:

- **`-DEACP_UNITY_BUILD=OFF`** — always. Unity builds merge translation units,
  so `compile_commands.json` stops being per-file and LSP results go wrong.
- **`-DCPM_eacp_SOURCE=$HOME/Code/eacp`** — builds against a local eacp checkout
  instead of fetching from GitHub. ECode and eacp are co-developed, so this is
  the normal way to work. CPM derives the variable name from the `NAME` in
  `CMake/FindEACP.cmake` verbatim, and CMake variables are case-sensitive:
  it is `CPM_eacp_SOURCE`, lowercase, not `CPM_EACP_SOURCE`.

Use `$HOME`, never `~` — CMake does not expand tilde, and it is suppressed
inside quotes, so the path silently resolves to nothing and fails later.

## Verifying changes

**Run the app.** A change that compiles and passes tests can still draw nothing.
Launch it and look.

**Build for iOS before calling framework work done.** `APPLE` is true on iOS, so
`if (APPLE)` in a `CMakeLists.txt` pulls macOS-only sources into the iOS build —
the guard is `if (APPLE AND NOT IOS)`. This has broken the build once already.
From the eacp checkout:

```bash
cmake -G Xcode -B build-ios -DCMAKE_SYSTEM_NAME=iOS \
      -DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_ARCHITECTURES=arm64 \
      -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO -DEACP_CI_BUILD=ON
cmake --build build-ios --config Debug -- -sdk iphonesimulator
```

`EACP_CI_BUILD=ON` turns unity builds on, which changes what compiles together —
a file can be fine alone and break in a unity blob.

## Work that lands in eacp

Anything a second app could want belongs upstream, not here. So far that has
meant scissor rects, scroll-wheel delivery, backing-scale notification, texture
sub-region upload and the whole `eacp-text` module.

**Every eacp change ships with unit tests and a live example.** Tests go under
`Tests/<Module>`, examples under `Apps/<Module>/<Name>`.

**Check that a new test fails without the change.** This is not a formality — a
test here passed with the feature deleted, because `respondsToSelector:` was
satisfied by `NSView`'s own inherited implementation. Comment out the change,
watch the test go red, put it back.

GPU state with no CPU-side observable (a scissor rect returns nothing; there is
no texture read-back) is tested by rendering off-screen with
`View::renderToImage(scale)` and asserting on the pixels.

## Architecture

**One `GPU::GPUView` for the whole window, with our own widget tree inside it.**
Not one `Graphics::View` per widget:

1. Every `Graphics::View` is backed by a real `NSView`. A file tree with 5,000
   rows would be 5,000 of them.
2. `Graphics::View` chrome is composited by CoreAnimation while text is drawn by
   Metal — two pipelines, and z-order across the boundary becomes CALayer
   ordering rather than GPU z-order.
3. `GPUView::paint()` is `final`, so a GPU view cannot also use the
   `Graphics::Context` API. The two drawing models are disjoint by construction.

`Graphics::View` is still used for the window's root and for genuinely native
things (menu bar, tray).

### The library split

Six targets, each linking the one above it:

```
ECodeCore       the model — no GPU, no platform
ECodeSyntax     tree-sitter behind Style.h's interface (optional)
ECodeRender     the glyph pipeline
ECodeWidgets    the widget toolkit inside one GPUView
ECodeEditor     the embeddable editor — what another project links
ECodeWorkbench  tabs, sidebar, palette, find, settings — the IDE around it
```

**`ECodeEditorTests` and `ECodeEditorRenderTests` are what keep this honest.**
Both link `ECodeEditor` alone — no workbench, no grammar — so an accidental
dependency stops them building. That matters because the cost of one is invisible
any other way: an embedder pays it in link size and build time, and the running
app looks identical.

So: **anything needing a command registry, a theme the editor does not read, or a
grammar belongs in ECodeWorkbench.** `EditorWidget` deliberately includes no
theme header. `ECodeSyntax` hangs off `ECodeCore` alone and nothing below links
it — highlighting reaches an embedded view through `setHighlighter`, the same
injection seam `Workspace`'s factory uses.

There are two applications, both under `Apps/`:

```
Apps/ECode/       the editor — links ECodeWorkbench
Apps/CodeViewer/  one file in a window — links ECode::Editor + ECode::Syntax
```

**Run `just run-viewer` after touching anything in ECodeEditor or below.**
CodeViewer drives the whole embedding API by hand from a native menu bar, which
no test does — a view that draws correctly into `renderToImage` can still come up
blank on a Retina display or ignore a menu command. It uses eacp's own
`Graphics::MenuBar`, never ECode's `MenuBuilder`: that belongs to the workbench,
and reaching for it there would be the first step of the split coming undone.
Numbers to preserve: 6.9 MB and zero workbench symbols, against ECode's 9.2 MB.

Consumers get the library by source, via CPM, the way ECode gets eacp:

```cmake
CPMAddPackage("gh:eyalamirmusic/ECode@0.1.0")
target_link_libraries(MyApp PRIVATE ECode::Editor)
```

`ECODE_BUILD_APPS` and `ECODE_ENABLE_TESTS` default to `PROJECT_IS_TOP_LEVEL`;
`ECODE_BUILD_SYNTAX=OFF` skips the tree-sitter fetch (105 s of cold configure down
to 7 s). There is no `install(EXPORT)` — eacp has no export sets, so CMake
refuses. `PLAN.md` §3 tracks that.

### eacp pieces this builds on

- `eacp-text` — `GlyphRasterizer` (CoreText, the only platform file),
  `ShelfPacker`, `GlyphAtlas`. Slots carry bearings and advance, so layout is
  per-glyph rather than a fixed cell grid.
- `eacp-sprites` — `SpriteRenderer`, textured quads. **One draw call per quad**;
  a real editor grid needs an instanced path, written against
  `ShaderProgram::instanceInput` + `RenderPass::drawInstanced`.
- `eacp-gpu` — `GPUView`, `RenderPass` (`setScissorRect` for every scrolling
  region), `Texture::update(region, …)`, and a C++ shader EDSL that generates
  MSL and HLSL. No `.metal` files.
- `eacp-graphics` — `Window`, `View`, input events, `Rect`'s JUCE-style
  `removeFromLeft/Top/…` splitters, which suit IDE chrome well.

### Things eacp does not have

Not yet written, and an editor eventually needs all of them: IME/composition
(so no CJK, dead keys or emoji picker), clipboard *paste* (copy only),
punctuation and Home/End/PageUp keycodes, cursor shapes (no I-beam), UTF-8
helpers, file watching, directory enumeration. `PLAN.md` tracks these.

## Code Style

Match eacp. Enforced by `.clang-format` (copied from it verbatim):
Allman braces, 85 columns, 4-space indent, `int* ptr`, constructor initializers
broken before the comma.

- Modern C++ and RAII throughout.
- `auto` for variables wherever possible; **not** for function return types.
- Prefer named functions over comments that explain *what* code does.
- Comments earn their place by explaining *why* — a non-obvious constraint, a
  platform quirk, a rejected alternative. eacp's own headers are written this
  way. Do not narrate the code.
- Give `std::function` members a non-null default (a no-op lambda, or one
  returning an empty value) so call sites invoke them without null checks.
