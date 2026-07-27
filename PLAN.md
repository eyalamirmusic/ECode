# ECode — a GPU-drawn, VSCode-style code editor on eacp

**What this is:** the live design document. It describes the editor as it stands
and the work that is still open. It is not a changelog — the reasoning behind a
finished decision lives in the header of the class that holds it, and the history
lives in the git log.

**Status.** A working editor, macOS only. Opens files — several at once, in
several panes — highlights them with tree-sitter, scrolls both down and across,
is typed in with multi-cursor selection, undo, clipboard and mouse, finds and
replaces, soft-wraps, saves atomically, and notices external changes. The chrome
is a widget tree drawn entirely on the GPU: sidebar file tree, per-pane tab
strips, status bar, command palette, find bar, context menu, draggable
splitters, native menu bar. Configured from `ECode/settings.json` in the
platform's application-data directory — colours, font and keybindings, the last
of them including VSCode's two-key sequences — which it opens in itself and
re-reads on save, and whose theme can be picked from the palette, previewed row
by row, and written back. 714 tests.

Built against [eacp](https://github.com/eyalamirmusic/eacp) `main` via CPM. Much
of the framework work ECode needed landed upstream; §3 is what has not.

---

## 1. Decisions that hold

- **VSCode-like, not Vim-like.** Modeless editing, mouse-first, standard chords,
  multi-cursor, command palette, sidebar tree, tabs, panel, status bar.
- **No webview.** Every pixel is drawn by us on the GPU.
- **macOS first**, Windows later — but the glyph-raster seam stays abstract.
- **Shared text stack**: the glyph atlas and rasterizer live in eacp's
  `eacp-text`, consumed by ECode and CowTerm both.
- **Anything a second app could want goes upstream**, with unit tests and a live
  example, and the iOS build run before it is called done.

### The central architectural call

**One `GPU::GPUView` for the entire window, with our own widget tree inside it.**
Not one `Graphics::View` per widget, for three reasons that have not changed:

1. Every `Graphics::View` is backed by a real `NSView`. A file tree with 5,000
   rows would be 5,000 of them.
2. `Graphics::View` chrome is composited by CoreAnimation while text is drawn by
   Metal — two pipelines, and cross-boundary z-order becomes CALayer ordering
   rather than GPU z-order.
3. `GPUView::paint()` is `final`, so a GPU view cannot also use the
   `Graphics::Context` API. The two drawing models are disjoint by construction.

`Graphics::View` is still used for the window's root and for genuinely native
things (menu bar, tray).

Consequences worth keeping in mind: there is exactly **one mouse cursor** for the
whole window, so widgets *report* a shape and the application applies it; and
there is exactly **one scissor rect** with no stack, so `PaintContext::ClipScope`
intersects a child's clip with its parent's rather than replacing it.

---

## 2. Module map

```
Lib/ECodeCore/     no GPU, no platform, fully unit-testable
  Document          text + incremental line index
  TextEdit/Editor   transactions, undo/redo, cursors, movement
  Cursor            CursorSet — sorted, non-overlapping, never empty
  LineMap           logical lines ↔ visual rows (soft wrap)
  Search            find/replace model
  TextFile          path, dirty flag, disk stamp, save/reload
  Workspace         the files open in one pane, and which is active
  EditorGroups      how many panes there are, and which one a path belongs to
  FileTree          directory model
  Commands          the command registry
  Style             the Highlighter interface — no tree-sitter in here
  Utf8, FuzzyMatch  header-only helpers
  ScrollOffset      where a view sits over its content, negative up and left

Lib/ECodeSyntax/   tree-sitter behind Style.h's interface
  SyntaxLanguage    grammar + compiled query, shared by every highlighter
  SyntaxHighlighter one parser and one tree per document

Lib/ECodeRender/   the glyph pipeline
  TextRenderer      lays out and draws the visible rows
  RowCache          the rows on screen, kept until something changes them
  PaintContext      sprites + glyph batch + the clip and atlas stacks
  FontSettings      family and size, and the atlas they ask for
  TextTheme         a document's colours: gutter, caret, one per TokenKind
  ColorJson         Color ⇄ "#rrggbb", and the hook that teaches Miro it

Lib/ECodeUI/       the widget tree inside the single GPUView
  Widget/WidgetHost layout, hit-testing, capture, focus, hover
  EditorGroupView   one pane: tab strip + editor + its own TextRenderer
  EditorWidget      the text view and its input handling
  Chrome            Panel, TabBar, StatusBar
  Theme             ChromeTheme — the colours around a document
  Themes            the built-in palettes, by name
  Settings          the file: what it says, what it resolves to, and — in
                    ThemeChoice — which theme a re-read of it leaves in force
  FileTreeView, ScrollView, ListView, TextField, FindBar,
  CommandPalette, ContextMenu, Splitter, MenuBuilder, Keymap

App/Main.cpp       the shell: GPU resources, the window layout, the commands
```

**Ownership rules that are load-bearing:**

- **One path means one document, window-wide.** `Workspace` keeps one tab per
  path; `EditorGroups::open` routes a path to whichever pane already holds it.
  Two of anything over one file is two undo histories and two dirty flags, with
  whichever saved last winning silently.
- **Per-file state lives on the file, not the view.** Scroll offset, syntax tree
  and undo history are on the `OpenFile`, so switching tabs is one assignment and
  there is no stash-and-restore step to forget.
- **Per-pane state lives on the pane.** Each `EditorGroupView` owns its own
  `TextRenderer` and therefore its own `RowCache`, which is a window of laid-out
  rows keyed by row index — two panes sharing one would invalidate each other
  every frame and draw the identical picture doing it.
- **An atlas is shared by everything drawing at its size.** A glyph is a glyph
  wherever it is drawn and uploading it once is the whole point — but a
  `GlyphAtlas` is one face at one size, so a settable editor font means two of
  them, the document's and the chrome's. See §4.
- **A widget reads its colours through a reference to the theme, never a copy.**
  That is what makes changing the theme an assignment: every widget is drawing
  the new palette on the next frame with nothing told and nothing walked. There
  are exactly two shapes that cannot — a `Panel` *is* a colour, and a `TextField`
  is *handed* its colours because the same field sits on the palette's background
  in one place and the find bar's in another — and both are the reason
  `Widget::themeChanged` exists. A stale copy has no CPU-side observable: it
  draws perfectly, in the theme that was loaded at startup.
- **Anything that touches the cursor goes through `Editor`, not through
  `Editor::cursor()`.** That rule is why multi-cursor cost two lines outside the
  editor: `cursor()` had seven callers and every one genuinely wanted the primary.

---

## 3. eacp — what is still missing

Framework gaps, ordered by how hard they block. Each ships with unit tests under
`Tests/<Module>`, a live example under `Apps/<Module>/<Name>`, and an iOS build
(`if (APPLE)` is true on iOS; the guard is `if (APPLE AND NOT IOS)`).

| # | Gap | Why it blocks | Shape of fix |
|---|-----|---------------|--------------|
| 3 | **No IME / composition.** No `NSTextInputClient`, no `interpretKeyEvents:`, no `WM_IME_*`. | CJK input, dead keys (`⌥e` → é) and the emoji picker are all unavailable, and none of it can be layered on from app code. | `NSTextInputClient` on the macOS backing view: marked-text range, composition callbacks, candidate-window rect. Real Objective-C++ work, and the largest item here. |
| 9 | **No UTF-8 support in `Strings`.** No codepoint iteration, no grapheme clusters, no width tables, no case folding. | Search's case-insensitive match folds ASCII only, so "Ä" does not match "ä". Soft wrap counts a CJK character as one column rather than two, so a wrapped line of it breaks late. | `ecode::Utf8` already carries `next`, `previousBoundary` and `nextBoundary`, which is the shape the eacp version wants. Width tables and folding are new. |
| 10 | **No file watching, no directory enumeration.** | The file tree and external-change detection. | FSEvents on macOS. The seams it replaces are `TextFile::hasChangedOnDisk` (polled once a second per open file), `SettingsWatcher::poll` (one more stat on the same timer) and `FileTreeModel::refresh` (`std::filesystem` behind `eacp::toStdPath`). |

Beyond the numbered gaps:

- **`GlyphRasterizer-Windows.cpp` is a stub** returning `isValid() == false`, so
  Windows draws no text at all. Porting notes are in its header. This is the
  first thing between ECode and a second platform, and the only item here that
  cannot be verified on this machine.
- **`command` is the Windows key on Windows.** `Keyboard::isCommandPressed`
  reports `VK_LWIN`/`VK_RWIN`, so every ECode binding — all `cmd+…` — would need
  Win+S rather than Ctrl+S there, while the menu bar prints "Ctrl+S" because that
  is the convention and Win32 accelerator text is decorative. The fix is to make
  `command` mean the platform's primary accelerator modifier, which changes
  keyboard semantics framework-wide (`GlobalHotKey`, `TextInput`) — a decision
  rather than a patch.
- **A menu item cannot print a shortcut it does not claim.**
  `Graphics::MenuItem` takes a `KeyEquivalent`, which is both the text shown and
  the chord the platform matches. A two-key sequence has no key equivalent — a
  bar claiming ⌘K would eat the prefix before the window saw it — so ⌘K ⌘T is
  bound, works, and prints nothing beside Color Theme. What is missing is a
  display-only string, which every platform's menus can show and neither
  platform's API infers.
- **Gamma-correct blending.** Coverage alpha is blended in whatever space the
  drawable is in. This is the difference between "looks native" and "looks
  slightly off", worst on light-on-dark, which is the default theme. See §4.
- **Shaping and ligatures.** `GlyphAtlas` maps one codepoint to one glyph. Fira
  Code's `=>` needs CoreText/DirectWrite line shaping behind the existing
  `GlyphSource` seam, plus a run cache — Ghostty measured shaping at 96% of frame
  time before adding one.
- **A family cannot be checked.** `CTFontCreateWithName` substitutes for a name
  it does not know, so `GlyphRasterizer::isValid()` is true for a misspelt family
  and the app draws in a face nobody asked for. Comparing
  `CTFontCopyFamilyName` against the request would report it; enumerating the
  installed monospace families is the same seam and is what a font picker needs.
  Until then `FontSettings::family` is set in code and believed.
- **Atlas eviction.** The atlas doubles up to a cap keeping every placement, and
  only at the cap does it clear and tick `generation()`. That is what Ghostty
  does and it is fine; LRU with stable handles can wait for a profile that asks.
- **The Windows menu bar is unverified.** No machine here compiles it. It went
  through an adversarial read that found seven behaviour bugs, six fixed. The
  seventh is left: **Alt+F does not open the File menu** — Win32 assigns no
  mnemonics without an explicit `&`, and separately `takePendingCharacters`
  drains `WM_SYSCHAR` before `DefWindowProc` can match one. That is pre-existing
  keyboard plumbing rather than menu work.

---

## 4. Text rendering — the parts not yet built

`eacp-text` is in place: `GlyphRasterizer` (CoreText, the only platform file),
`ShelfPacker`, `GlyphAtlas`, `GlyphRenderer`. Slots carry bearings and advance,
so layout is per-glyph rather than a fixed cell grid; the mask atlas is `R8Unorm`
with colour emoji on a second RGBA8 page; the rasterizer sits behind a
`GlyphSource` interface so the atlas is driven by a stub in tests.

**Two atlases, one per size.** A `GlyphAtlas` is one face at one size, so the
settable editor font and the fixed chrome font are two of them: ⌘+ enlarges the
code and leaves the tab strip, the tree and the status bar where they are. The
cost is one rule, and it is the same rule the clip stack already lives by —
`GlyphRenderer` batches until a flush and the flush names the texture, so a batch
queued against one atlas and flushed against another draws whatever texels sit at
those coordinates in the other. `PaintContext` owns which atlas is current and
flushes on every change; `AtlasScope` is how `TextRenderer` switches to its own
for the length of a document draw. `FontRenderTests` pins it by drawing the same
text at the same size through one atlas and through two and demanding the same
pixels.

Settled and worth not relitigating, read from the Ghostty and Alacritty source
trees rather than from blog posts:

- **No subpixel positioning.** Neither terminal does it. On integer cell origins
  every occurrence of a glyph shares one subpixel phase, so consistency costs one
  atlas entry instead of four. Zed pays for four because GPUI renders
  proportional UI text; a code grid does not have that problem.
- **Grayscale AA everywhere, and solve gamma instead of subpixel AA.** Subpixel
  AA cannot coexist with a transparent window background, and macOS dropped it in
  Mojave. Alacritty did subpixel AA and ignored gamma; Ghostty is grayscale into
  an `r8unorm` atlas and solved gamma. Take Ghostty's side.
- **Damage is CPU-side only, and that is forced.** Alacritty's compositor damage
  goes through EGL, which takes damage rectangles at *present* time. Metal has no
  equivalent: a `CAMetalDrawable` is presented whole and comes from a rotating
  pool, so loading its previous contents gives a frame two or three old. "Redraw
  only the damaged rows" is not available; skipping the *deriving* is, and that
  is what `RowCache` does.

**What gamma correction will need, and the reason to design for it now.**
Ghostty's `linear-corrected` mode renders into an sRGB target so blending is
linear, then remaps the coverage alpha so the result still looks like the
familiar sRGB blend: `a' = (blend_l - bg_l) / (fg_l - bg_l)`. That remap needs the
**per-cell background colour inside the text shader**. Ghostty gets it because
`cell_text_vertex` reads a flat `bg_colors[row * cols + col]` buffer, and the same
data path is what enables minimum-contrast later. It is genuinely hard to
retrofit, and the current renderer does not have it.

**Backgrounds want a fullscreen pass, not instanced quads.** Ghostty moved cell
backgrounds to a flat `[4]u8` array read by a fullscreen-triangle shader, which
cut GPU memory ~20% and shrank its text instance from 56 to 32 bytes. Its floor
is three draw calls per frame: clear, cell backgrounds, one instanced call for
all glyph quads. That is a better target than the current run-length rects.

---

## 5. What is left in ECode

Nothing here blocks anything else, so this is a menu rather than a sequence.

### 5.1 The larger ones

**One document in two panes.** This is what a split is usually *for*, and the
current invariant — one path, one pane — is what stands in for it. It is not a
groups problem: two views of one document need their own cursors, their own line
map, their own wrap width and their own scroll offset, and all four live on
`Editor` today, which is one per `TextFile`. Wrapping already shows the seam from
the other side: `wordWrap` is a property of the widget while the `LineMap` it
configures belongs to the editor, so two widgets at different wrap widths would
thrash one map even now. The shape is a `DocumentView` owning those four with
`Editor` keeping the text, the history and the edits — a real refactor of the
multi-cursor work rather than an addition to it.

**IME, from this side.** The framework half is §3's gap 3. The half that belongs
here is the **marked-text range in the cursor model**, and it is the piece that
should not wait: composition means the document holds provisional text that is
styled differently and is not yet a real edit, and retrofitting that through
`Editor`, `EditHistory` and the renderer is the kind of change this plan exists
to avoid. Multi-cursor has now landed on all three, so the surface is larger than
it was — but multi-cursor is also the evidence for the other reading: it stayed
small because it went through `Editor` rather than around it, and marked text can
be held to the same rule. It is a range on the `CursorSet`, not a second thing
views reach into.

**The rope.** Deferred on a measurement, not a guess: on a generated 100 MB file
a keystroke in the middle costs 1.02 ms — the string's memmove and the line
index's shift in roughly a 3:1 ratio — and a full `LineMap::rebuild` (⌥Z, or a
resize) costs 102 ms. Nothing at 10 MB. Real, but not what stands between ECode
and a large file.

Two things to know before building it. **`LineMap::rowStarts` is the same shape as
`Document`'s line index** — a flat vector of absolute positions, repaired
incrementally, shifted in full past every edit — so whatever replaces one should
replace both, or the rope buys back a millisecond that soft wrap immediately
spends. And **the deferral is safe because the mutation API is narrow**:
`replace(start, end, text)` plus `line(i)`, so the storage can change without the
renderer or the highlighter noticing.

**Config and theming.** The file is `ECode/settings.json` under the platform's
per-user application data — Application Support, Roaming AppData, the XDG data
home — holding font, theme name, two partial colour blocks and a partial
keybindings block, read through Miro reflection. A file left at the old
`~/.config/ecode.json` is moved there once, by `migrateSettings`. Why each of
those is shaped the way it is lives in `Settings.h`, `ColorJson.h`, `Themes.cpp`
and `Keymap.h`.

**A write is now allowed, and only one shape of it.** `Preferences: Color Theme`
opens the palette over `themeNames()` with each row previewing itself — the
window re-themes as the highlight moves, Enter keeps it, Escape puts back what
was there — and Enter writes `"theme"` to the file so it outlives the window.

The rule that makes writing safe is `settingsWith`: it edits the *parsed
document* and prints it back, so every key it does not name survives, including
the two colour blocks and anything a later version writes. Re-serialising
`Settings` would delete all of them, and nothing would say so — reflection knows
only its own fields. Nothing else writes: previews do not, and the zoom still
does not, which is now a decision about the zoom rather than about the file.

`ThemeChoice` is what holds the two apart while a picker is open: a preview
outlives every re-read of the file, and the file takes it back the moment its
own `"theme"` moves — which is what a commit makes happen a moment later.

What is left:
- **The window's title bar is not themed.** It follows the system appearance, so
  a light theme opens under a dark title bar. That is a window-options gap in
  eacp rather than anything here.
- **Choosing the *family*** waits on eacp — CoreText substitutes silently for a
  name it does not know and there is no way to ask which face it picked, so a
  picker today could only offer a list and hope. §3 is where that gap belongs.
  Until then a misspelt family in the file is accepted and drawn in something
  else, which is the one thing the load path checks and cannot report.
- **Nothing in the app shows what the keys are bound to, or edits them.** The
  palette and the menus print the chord beside each command, which is the whole
  of the read side, and the file is the whole of the write side. VSCode's
  keybindings editor is a table with a recorder in it; the pieces here are a
  `ListView`, a `TextField` and `Chord::fromEvent`, which already turns a key
  event into exactly the string the file wants.
- **A bad binding is reported to the log, which nobody is reading.** An
  unparseable chord, a command that was never registered, and now a chord bound
  over the start of a longer one are all silent in the app itself — the shortcut
  simply does not work. The status bar is the obvious surface, and it is the
  same gap the settings file has for a bad colour, so it is one answer rather
  than two. It is also the surface a pending chord already uses, which is the
  argument for putting the rest there.
- **A command bound to a sequence prints no shortcut in the menu.** Sequences
  are built — a binding is a `ChordSequence`, `ChordMatcher` holds where the
  keyboard is inside one, and ⌘K ⌘T opens the theme picker — but a sequence can
  never be a native key equivalent, because macOS matches the bar before the
  window is sent a key and would eat the prefix. So the item shows nothing while
  the palette prints "⌘K ⌘T", and the fix is a display-only shortcut string on
  `Graphics::MenuItem`, which is eacp's to add.

  Two decisions in it worth not relitigating. **A prefix beats an exact binding
  on the same chords**, because waiting is the only state from which either can
  still happen — the short one loses, and `reportKeybindingProblems` is what
  says so. And **there is no timeout**: any key that does not continue the
  sequence ends it, so a pending prefix costs the one keystroke that ends it and
  can never leave the editor untypeable, whereas a wall-clock rule would put the
  behaviour of the keyboard behind a number no test can read without waiting for
  it. What stands in for the timeout is the status bar, which says what is being
  waited for and, afterwards, which chord missed.

**LSP.** `Processes::runAsync` returning `Async<T>` is the right foundation;
diagnostics, completion and go-to-definition after the chrome settles.

### 5.2 Performance, in the order the numbers say

- **`Files::readFile` used to be the largest number left on a big file** — 4.9×
  the file in RSS, all of it the read. Fixed upstream (eacp `12817bb`, "stop
  reading a file at four times its own size"): it stats the size, resizes once
  and reads into that, with the short-read case handled because the stream stays
  in text mode and Windows turns CRLF into one character. Not re-measured here.
- **The first ⌃Tab onto a large tab**: 8.6 ms, spread over 2 ms frames. Nothing
  parses a file until it is looked at, so the whole cost lands on the switch.
  Starting it at open, in slices off the same budget, would move it somewhere
  nobody is waiting. The machinery already exists.
- **The 4 MiB highlight threshold** is what makes a large file usable at all:
  above it nothing is parsed, the file draws plain and the status bar says
  "Plain (file too large)". Below it, tree-sitter's reparse is linear at ~1.7 ms
  per MB and crosses a 60 Hz frame at around 10 MB. Raising the threshold means
  making tree-sitter cheaper — threads, or a smaller grammar.

### 5.3 Editing

- **Undo does not restore the cursor set.** It collapses to one cursor, because
  recording the set on each step is a change to `EditHistory`. The cheap
  substitute — a cursor per edit in the undone step — is right for a multi-cursor
  keystroke and wrong for a replace-all, which would leave a cursor on every
  occurrence in the file.
- **Multi-cursor paste does not distribute.** VSCode gives N copied lines to N
  cursors; that needs the clipboard to carry how many selections produced the
  text.
- **No column (box) selection** by ⌥-dragging. A different gesture on the same
  model.
- **⌥-double-click adds two carets** rather than a word selection: the widget
  takes ⌥-click before the click-count cases.
- **The horizontal range is sized from the widest line in *bytes*.** Scrolling
  across works — the wheel drives it, the caret is followed off the right edge,
  the gutter stays put while the text slides under it, and wrapping pins it back
  to the left because a wrapped row never reaches past the edge. What is
  approximate is which line the range is measured on: `Document` records the
  longest in bytes, and while that line is then measured properly, tabs and all,
  a *shorter* line with more tabs in it can reach further and have its last
  columns be unreachable. Fixing it means counting expanded columns, which puts
  a tab width — a rendering decision — inside the one class that deliberately
  has none. `LineMap` already has one and is the better home if it is wanted.
  The reason it is a memo at all is that recomputing eagerly measured 4.7 ms of
  a 6.7 ms keystroke on a 100 MB file, so `clampScrollColumn` asks only of a
  view that has actually been scrolled across.
- **Continuation rows are not indented.** VSCode aligns a wrapped row with its
  line's indentation, which matters much more for code than for prose. It is a
  `LineMap` change — a per-line indent added to the row's left edge and
  subtracted from its width — and the renderer has to know about it too.
- **Row height is uniform.** The assumption now lives behind one function,
  `TextRenderer::rowTop`, instead of being multiplied out at eight call sites, so
  a variable height is a change to a class that already has one entry per screen
  strip.

### 5.4 Chrome

- **Widgets not yet built**: minimap, tooltip, animation/easing, and a `Button`.
  The find bar's six controls are a hotspot table inside it rather than widgets;
  now that hover states exist (`Widget::mouseExited` + `WidgetHost::hovered`),
  that has stopped being the right call.
- **No `when` clauses in the keymap.** Two overlays — the palette and the context
  menu — are handled by hand in `EditorView::modalOverlay`, and a focused text
  box claiming ⌘A/⌘C/⌘V/⌘X is handled by `Widget::isTextInput` and
  `Widget::runCommand`. Still not worth inventing contexts for two; a third is
  the moment.
- **Tabs**: no dragging to reorder, no tab context menu, and no "N unsaved files"
  prompt on quit — `EditorGroups::hasUnsavedChanges` exists and nothing asks it.
- **Panes are a row.** There is no splitting one horizontally.
  `SessionView`'s recursive split-pane tree in CowTerm is the shape if that is
  wanted, and a row is a strict subset of it — the weights and the seam-drag
  arithmetic are what a tree would keep.
- **No file preview on highlight in the palette.** The *peek* half is built —
  a `PaletteItem` carries a `preview` alongside its `run`, and `show(items, …)`
  takes what a dismissal has to undo — and the theme picker is the first thing
  using it. What a file list would add is the cost: a preview that opens a
  document is a parse and a row cache rather than an assignment, so it wants the
  budgeted highlighter the ⌃Tab item in §5.2 wants.
- **There is no dialog widget**, so two questions are carried by the window
  title and answered by pressing the same chord again: a save that would clobber
  an external write (⌘S) and a close with unsaved edits (⌘W). Both go stale on
  the next edit, compared by `Editor::stateId` rather than cleared by an event.
- **macOS injects items into menus it recognises by name** — AutoFill, Start
  Dictation and Emoji & Symbols into Edit; Show Tab Bar, Show All Tabs and Enter
  Full Screen into View. Left alone deliberately: the two text-input ones are
  suppressible with `NSDisabledDictationMenuItem` /
  `NSDisabledCharacterPaletteMenuItem`, but that needs ECode to carry its own
  `Info.plist` instead of eacp's shared template, and both become *correct* the
  moment IME lands. AutoFill has no documented key. Window tabbing would need
  `allowsAutomaticWindowTabbing` exposed from eacp, and "Enter Full Screen" being
  disabled is a window-options gap that predates the menus.

---

## 6. Risks worth naming

- **eacp is self-declared alpha**: "APIs will change without notice between
  commits." ECode tracks `GIT_TAG main`, as do eacp's own four dependencies.
  Worth pinning to a SHA now that ECode depends on real behaviour rather than
  just compiling.
- **Two eacp branches have diverged.** `jamierpond/eacp jp/fancy-terminal` has
  commits we lack. The CowTerm PR is blocked on reconciling them — a coordination
  problem rather than a coding one. Duplicated work has already happened once
  (clipboard read).
- **eacp's README and CLAUDE.md predate the GPU stack** and describe it as Core
  Graphics only. Read the tree, not the docs.
- **`Graphics::Rect`'s splitters are y-down now**, matching the rest of eacp.
  Nothing in eacp, ECode or CowTerm compensated for the old behaviour, so the fix
  corrected four call sites rather than breaking any — but the forks
  (`JamieEACP`, `eacpTest`, `eacp-cleanup`) still carry the y-up version, and
  merging across that boundary will silently invert layouts.
- **`if (APPLE)` includes iOS.** It has broken the build once. The guard is
  `if (APPLE AND NOT IOS)`, and the CI invocation is in CLAUDE.md.
- **No Linux GUI path exists in eacp at all** — the whole `Graphics`/`GPU` tree is
  gated behind `(APPLE OR WIN32)`. Linux is not "later", it is a separate project.

---

## 7. What this project has learned about testing itself

Every rule here cost something to find out. They are the reason the suite is
worth trusting, and they keep coming back — several were learned twice.

**Making a test able to fail**

- **Verify a new test fails without the change.** One passed with the feature
  deleted, because `respondsToSelector:` is satisfied by `NSView`'s own inherited
  implementation.
- **Verify the mutation applied — and that the revert did too.** Two mutation
  checks silently no-op'd because clang-format had reflowed the text being
  replaced. A backup restored with `shutil.move` kept the old mtime, so ninja
  rebuilt nothing and the mutant stayed linked in for every later run.
- **The verdict is the exit code.** A run that asked ctest for its answer through
  `| tail -3` cut off the "(Failed)" line, so thirteen mutations came back green.
  A pipeline's exit code is the last command's.
- **A mutation that goes red by *crashing* has not been checked.** The tell is a
  red run with no FAIL line in it. Find out which assertion broke — and note
  that the usual cause is the test itself: a count checked and then indexed
  reads off the end as soon as the count is wrong, which is exactly when it
  matters. Two mutations of the palette's preview came back this way. Asserting
  on the whole sequence as one joined string cannot do it, and says more.
- **Check it fails in the direction that costs you something.** A wrong answer
  usually only hurts one way: falsely *clean* skips the save and loses the work,
  while falsely dirty writes a file twice.
- **A line no mutation can kill is a line nobody needs.** The settings template
  writes its two colour blocks by hand, so the keybindings block was written the
  same way — and backing that line out left every test green. Reflection had
  been emitting it all along, because unlike the colour blocks it *is* a field
  of `Settings` and an empty map prints as `{}`. A surviving mutation is
  normally a hole in the tests; sometimes it is the code that is redundant, and
  the two are only told apart by asking why the tests did not care.
- **A test can be unable to fail on your machine.** The clamp keeping a negative
  row index from becoming an enormous unsigned one guards undefined behaviour,
  and on arm64 the UB happens to do the right thing. A green suite is not
  evidence until you know which of its tests could have gone red.

**Choosing the case**

- **Pick the input where the two implementations disagree.** Wrapping "the quick
  brown fox" at ten columns cannot tell character wrap from word wrap; at
  thirteen it can. Three tabs cannot tell a decrement from a clamp — it takes
  four and the third active. Two implementations agreed on every sequence anyone
  would naturally write, because each `⌘D` makes its own new cursor the primary.
- **When a mutation survives, ask what state the code actually needs.** That is
  information about the design, not about the test, and the answer belongs *in*
  the test. A held column falling through to the row below needs three rows and
  two presses of Up. Renumbering after a closed pane needs three panes, a forward
  move, and a source that is neither the last nor the destination.
- **A test of the common case cannot see what only the rare case separates.**
  `ClipScope`'s intersection passed with a plain assignment, because a tab lies
  *inside* its tab bar, where narrowing and replacing are the same operation. It
  took a child laid out four times the size of its parent.
- **A guard whose point is a coordinate needs a case with that coordinate.**
  Dropping the tab strip's "is the point inside the strip" check left every test
  green, because the strip started at x = 0 in all of them.
- **A sort of equal elements cannot be caught being unstable.** libc++ leaves an
  all-equal range where it found it. It takes two score classes interleaved.
- **Some invariants are held twice over.** Deleting either the highlighter memo's
  revision comparison *or* its forget-on-edit leaves the suite green, because each
  catches what the other does. Worth keeping both, and worth writing down which
  one a test actually exercises.

**Asserting on pixels**

- **Arithmetic cannot tell you which way is up.** `Rect`'s splitters were y-up in
  a y-down framework, so `removeFromTop` returned the bottom slice and ECode drew
  its tab bar along the bottom edge for months. A unit test can only confirm the
  maths agrees with whatever convention the author had in mind. Anything that is
  a convention rather than a computation needs a test where the convention is
  observable.
- **An 8-bit drawable makes "slightly" free, so never assert on "slightly".**
  `0.102f` reads back as `0.10196`, so a plain `<` holds on rounding alone.
  Demand a margin the effect clears and quantisation cannot.
- **Test the hue when hue is the difference.** "Is this pixel blue" found a caret
  on every line, because a 70%-covered antialiased glyph edge reaches the same
  blue. Antialiasing turns every colour into a ramp towards the background, and a
  single-channel threshold reads a point on that ramp.
- **A region assertion is only as good as its region.** A band holding a rule
  passed with the rule deleted, because the band spanned the box's border. A
  pane's text area contains the current-line band, which fills the caret's row
  edge to edge whatever the document says. After the code, the second place to
  look is what else the widget draws for free. Counting a colour over the *whole
  image* is the same mistake with no region at all: the find bar's own button
  labels are drawn in `theme.findText` and read it live, so a test meant to ask
  whether the text field's private copy had been updated was answered by two
  widgets that were never in doubt, and survived the field never being told.
- **A named pixel is an assertion about rounding.** A one-point line centred in
  an eight-point band lands at `x - 0.5`, so the lit column is the one to the
  *left* of the divider. Take a peak across the band.
- **A whole-row highlight swallows an ink count.** Compare two renders against
  each other over the column in question, plus the complementary check that the
  rest of the row is byte-identical.
- **A test comparing two frames cannot fail if both are blank.** The idle-frame
  test passed with the cache storing empty rows. It took an ink count alongside
  the equality.
- **Dump at the scale the context was built with.** A snapshot through
  `renderToImage(2.f)` while the harness passed `1.f` to `PaintContext` came back
  with the right half of every row missing, and looked exactly like a clipping
  bug in the new code. Twice.
- **Check what the harness asked for before believing what it shows.** A blank
  first frame at a deep scroll offset read as the line index failing a quarter of
  a million lines in. `scrollY` is *negative* here; `firstVisibleRow` returning 0
  for every offset was the tell, and it was in the first diagnostic printed.

**Testing an absence**

- **An oracle proves the answer, never the path.** The incremental line map is
  compared against a full rebuild, and it passes with the whole optimisation
  replaced by `rebuild()`. Worse: an incremental reparse and a from-scratch one
  arrive at the same tree, so that oracle agreed for as long as *every keystroke
  reparsed the whole file* — 9.6 ms a character, inside a green suite, behind a
  test written specifically to cover that path. Any optimisation whose fallback
  is the thing it optimises needs a counter, and the counter is the test.
  `LineMap::rebuildCount`, `RowCache::layouts`, `SyntaxHighlighter::fullParses`.
- **An oracle built out of the thing under test agrees with it while both are
  wrong.** The layering test asked "is the un-overridden sidebar still the light
  theme's?" against `themeByName("light")` — and a `themeByName` that ignored its
  argument entirely handed the same dark palette to both sides, so it passed. The
  comparison that could fail was against the *defaults*, which is the one value in
  the test that the mutation could not also move. Both belong: one catches the
  name being ignored, the other catches the layering producing some third colour.
- **Reflection cannot be its own witness.** A field left out of `MIRO_REFLECT` is
  a field the settings file cannot set, and nothing says so — the key is ignored
  like any unknown one. Comparing two structs' JSON cannot see it, because a
  missing field is missing from both sides and the test passes exactly when it
  should fail. The oracle has to come from outside reflection: `memcmp` over the
  struct, with every channel perturbed to its own value first, so a pair of names
  swapped in the macro's list fails as loudly as a pair left out.
- **When an optimisation has no observable, ask what changes *afterwards*.**
  Releasing the syntax tree for an over-size file shows up in no colour — but a
  file that shrinks back under the limit is then parsed from scratch, and
  `fullParses()` goes to 2. Freeing something is invisible; needing to rebuild it
  is not.
- **Some skips can only be measured.** Skipping the glyph prepass for a cached row
  saves hash lookups that nothing counts. What *is* testable is its precondition —
  that a cached row's glyphs are still in the atlas, which is the atlas generation
  in the cache's stamp. When a skip cannot be observed, test what makes it safe.

**Composition, and the app**

- **Two correct halves can compose into a wrong whole.** Search found the right
  ranges and the renderer had two hit colours, both tested. In the app the current
  hit came out the *selection's* blue, because finding a hit selects it, so the
  two always coincide — and no test set a cursor and a match list in the same
  frame. The arrangement that was broken was the only arrangement the app is ever
  in.
- **A new feature makes old code paths reachable, and they have never run.** ⌘N
  segfaulted in `Document::columnAt` because the default constructor never built
  its line index — wrong since the file was written and unreachable until
  untitled buffers existed. Closing the last tab refilled the workspace without
  connecting a highlighter, so the buffer left behind would draw plain forever —
  found only by reading the class in order to add a second exit from it. Ask what
  your change lets happen for the first time, and when you add a route out of a
  class, read every existing one.
- **A hook that runs after *every* command is a claim about commands that do not
  exist yet.** `dispatchCommand` wakes the editor, and waking it brought the caret
  into view — right for the paste, undo and cut it was written for, and wrong the
  moment a command changed nothing about the document. ⌘+ zoomed the font and
  threw the file back to wherever the caret happened to be, defeating the
  top-line preservation the zoom had just done. The fix is a condition rather
  than a flag at each call site: follow the caret only when the text or the
  cursor has actually moved, so a command added later cannot forget to say so.
- **An invariant a class states in prose is a test you have not written.**
  `Document`'s header said "a genuinely empty document still has a single line to
  put the caret on" while `= default` left it with none.
- **A refusal that is returned but not remembered passes a test of the return
  value.** `TextFile::save` reported `changedOnDisk` correctly while forgetting it
  had; the existing test only read the result.
- **Run it.** The red-text bug — an R8 mask through a tint-multiplying shader —
  passed every test that existed and was obvious in one screenshot. Run it to see
  whether something is *right*, then write the off-screen test that proves it
  stays so.

**Measuring**

- **Measure before believing a debt.** Damage tracking was carried as "the next
  thing to bite" for months. It was 0.32 ms a frame.
- **A number is not a measurement until it is broken down.** The cold open was
  "~40 ms, essentially all of it tree-sitter's parse". It was 14 ms of compiling
  the highlight query — a function of the *grammar*, not the file — and 10 ms of
  parsing. Everything proposed aimed at the 10.
- **Vary the input along the axis you want to attribute.** Timing a keystroke at
  the *end* of a 100 MB file moves nothing and shifts nothing, so what remains is
  what an edit costs wherever it lands: 4.72 ms of 6.70, and now 0.000. The
  cheapest way to break a number down is an input where the part goes to zero.
- **Grep for who reads a number before optimising it.** Two thirds of a keystroke
  on a large file went into `Document::widestLine()`, which at the time nothing
  outside its own tests called. Nothing looked wrong and no test could have
  failed: it was computing the right answer, eagerly, for nobody. The horizontal
  scroll is its caller now, and asks only when the view has left the left edge —
  which is the shape the memo was left in to make possible.
- **A/B the parts that "obviously" help.** A `reserve` on the line index measured
  as noise — 29.6 ms against 30.0 — and would have over-allocated 25 MB on a file
  that turned out to be one long line. The whole 3.5× came from `memchr`.
- **A benchmark reading exactly zero is a bug in the harness until shown
  otherwise.** `setWrapColumns` early-returns on an unchanged width, so timing it
  three times at 80 columns timed one rebuild and two no-ops — and "best of N",
  the right reducer everywhere else, reports the no-op. Soft wrap on a 100 MB
  file came back as 0.000 ms. It is 102 ms.
- **Timing a live window measures what the window server allowed.** An occluded
  window gets no drawable, the pass becomes a no-op and the paint measures
  nothing — which is how a bigger window came out *faster*. Off-screen numbers
  are the quotable ones.
- **The build type is a hidden input to a wall-clock default.** A 2 ms parse
  budget is invisible at `-O2` and interrupts a 500-line parse in a debug build.
  A default whose behaviour depends on the optimiser has no business being one.
- **Read the library's promise, then check it.** The whole deferred-parse design
  rests on one sentence of tree-sitter's docs. Confirmed by parsing the same
  8,000 lines twice and comparing the trees as strings — 646,186 characters,
  identical — before anything was built on it.

**Driving the running app**

- **Render the assembled tree off-screen; do not drive the live window.**
  `View::renderToImage` is what this framework has instead of a robot, and it is
  strictly better: no focus, no stolen screen, deterministic, and it stays as a
  regression test. It is also the answer to "let me *look* at it" — dump the
  snapshot to a file and open it. Reserve the running app for judging how
  something feels.
- **The automation can be the thing that changed the state.** Reading menus
  through System Events opens them, and opening them runs commands.
- **And the thing that failed to.** A menu item's `enabled` and its checkmark are
  set inside `validateMenuItem:`, which AppKit calls when a menu is about to be
  *drawn* — so reading the attribute reports the last draw, and reading it does
  not cause one. Addressing an item by name never opens the menu; enumerating all
  of `menu items` does. The tell is a sequence of readings each right for the
  *previous* step.
- **AppKit also refuses the *click* against that stale state**, silently.
  Focusing a pane and then clicking a command the focus had just enabled did
  nothing, three times running. Enumerate, then click in a separate step. A
  command that appears to do nothing under automation is not evidence.
- **The settings file cannot be pointed somewhere else for a run.**
  `FilePath`'s directories come from the platform's own API —
  `NSHomeDirectory()`, `NSSearchPathForDirectoriesInDomains` — which read the
  user record and ignore `$HOME`. Verified, because a run under a scratch home
  read the real path and reported nothing, which looks identical to the feature
  not working. So there is no way to exercise the configured app without writing
  to the real Application Support, and that is the argument for every rule here
  taking *text* or *paths*: `configurationFromJson`, `settingsWith` and
  `migrateSettings` are each testable against a scratch directory, and only the
  wiring between them and the window is not.
- **ECode will not reliably come to the front**, and captures come back showing
  another application. What that does not say, and cost something to find out:
  the keystrokes still go somewhere. A driving script has to *check* that ECode
  is frontmost before every send and give up otherwise, or it types a command
  name into whatever application is. Chords that are single characters become native menu key
  equivalents, matched by macOS before the window sees a key at all — verified
  through AX as far as this machine allows, and not pressed by a person.

**And about this document**

- **A plan can hold both halves of a contradiction and never put them together.**
  "A split view is two Workspaces side by side" and "two tabs over one path would
  be two undo histories, whichever saved last winning silently" sat two paragraphs
  apart for months. Two workspaces open files by path and know nothing about each
  other, so the first sentence *is* the second failure. A design note written as a
  decision about one feature is also a constraint on every feature that reuses the
  same class.
