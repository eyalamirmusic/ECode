# ComponentPlan — moving ECode onto eacp's `UI::Component`

**What this is:** the live plan for replacing `ECodeWidgets` with eacp's
component tier. It is a companion to `PLAN.md`, which predates that tier and does
not mention it: §2's module map and §3's "what eacp is still missing" both
describe a world where ECode owns its own widget toolkit, and both get rewritten
as this lands. Until then, read this one for the widget layer and that one for
everything else.

**Status.** Nothing migrated. The eacp-side work is on branch
`ecode-component-adoption`, cut from `main` at `2545c94`. No decision has been
made to move `ECode::Editor` — §3 is the measurement that decides it.

---

## 1. Why

`ecode::Widget` and `eacp::UI::Component` are the same design. Both are
lightweight elements in one `GPUView`; both give the JUCE argument for it in
their own header comment — a real interface is hundreds to thousands of elements
and one native view apiece pays AppKit for every one. ECode wrote its version
because eacp had none. eacp has one now, and it is the more capable of the two:
relative bounds, z-order, `hitTest` overrides, mouse enter/exit, focus traversal,
drag-and-drop targets, compositing layers, and vector `PathShape`s that rasterize
through a compute kernel into a shared coverage atlas.

Keeping both means maintaining a second widget system that contains no idea the
first one lacks. The parts of `ECodeWidgets` that *are* unique — a virtualised
list, a scrollbar that speaks in content coordinates, a splitter — are unique
because nobody has written them upstream yet, not because they are about editing
text. That is the definition of work that belongs in eacp.

## 2. What maps, and what does not

| `ECodeWidgets` | eacp `UI` | verdict |
|---|---|---|
| `Widget` (206 l) | `Component` | superset; drop ours |
| `WidgetHost` (259 l) | `ComponentHost` | same capture, hover, focus, Tab; drop ours |
| `PaintContext` (`ECodeRender`) | `UI::Graphics` | state stack, gradients, rounded rects, `fillPath`, `drawLayer`, justified `drawText`, font stack; drop ours |
| `UIText` (119 l) | `Graphics::measureText`, `drawText(area, justification)`, `Component::measureText` | drop ours except `elide()` |
| `TextField` (380 l) | `UI::TextEditor` | near-equal; ours adds the command-precedence hooks of §5 |
| `ScrollView` + `ScrollBar` (343 l) | `ScrollPanel` | ScrollPanel is thinner — see §4.3 |
| `ListView` (261 l) | — | §4.2 |
| `Splitter` (243 l) | — | §4.4 |
| `Chord` (434 l), `ChromeTheme` | — | stays here — §5 |

About 1,600 of `ECodeWidgets`' ~3,000 lines are things `Component` already does.

The migration surface on this side: 13 `Widget` subclasses across 10 headers, 12
`paint(PaintContext&)` bodies, 6 `layout()` bodies, 9 `prepare()` bodies, and 17
test files that build an atlas and a `PaintContext` by hand.

## 3. The gate: what `ECode::Editor` would carry

**Nothing else in this document matters until this number is known.**

`ECodeEditor` links `ECodeWidgets → ECodeRender` today. On the component tier it
links `eacp-ui`, which links `eacp-gpu eacp-gpuwidgets eacp-text`. That is not
the whole cost. `Component.cpp` calls `ComponentHost::setFocusedComponent`,
`measureText` and `getFont` (lines 167, 269, 276), so linking `Component` pulls
`ComponentHost.o`, which holds a `CoverageAtlas`, `ShapeBatch`, `MeshBatch`,
`LayerRenderer` and `GradientRamps` as members. A static archive drops
unreferenced *members*, not unreferenced code inside a member that is referenced,
so an embedded read-only code view would carry the compute-kernel path rasterizer
and the layer compositor — neither of which it draws a single pixel with.

`CLAUDE.md` names 6.9 MB for CodeViewer against ECode's 9.2 MB as numbers to
preserve, and `ECodeEditorTests` / `ECodeEditorRenderTests` exist to make an
accidental dependency fail the build. This would be a deliberate one, so the
tests would not catch it and only the size would say.

**The spike:** build CodeViewer against a throwaway `eacp-ui`-based root — one
`ComponentHost`, one component that draws a rectangle — and compare the linked
binary against today's. Half a day. Debug archives are no guide here
(`libECodeWidgets.a` is 9.5 MB and `libeacp-ui.a` 13.7 MB, both unstripped
debug); the figure that decides it is a release binary.

**The decision rule:**

- Under ~1 MB of growth: migrate everything, including `ECode::Editor`.
- Over that: migrate `ECodeWorkbench` only, and keep a `Widget`-shaped editor —
  which is only possible if §6's adapter holds, since a `Component` cannot have
  a `Widget` child.
- If neither works: split `eacp-ui` upstream so `Component` + `ComponentHost` +
  `ShapeBatch` do not drag the path and layer renderers in. This is the honest
  outcome to hope for — the tier is one library today because nothing had reason
  to ask otherwise, not because the pieces are entangled.

## 4. Work that lands in eacp

Branch `ecode-component-adoption`, off `main`. eacp's own rules apply: **every
change ships with unit tests under `Tests/UI` and a live example under
`Apps/UI/<Name>`**, the test is checked to fail without the change, and the iOS
build is run before anything is called done. Validated from ECode by pointing CPM
at the checkout, which is already the normal build:

```bash
just build && just test && just run && just run-viewer   # -DCPM_eacp_SOURCE=$HOME/Code/eacp
```

`CPM_eacp_SOURCE` follows whatever branch that checkout has out, so there is
nothing to switch when moving between eacp branches — but also nothing to warn
you that you are building against one.

### 4.1 The glyph pre-pass — the only correctness item

`UI::Graphics` calls `text.flush(pass)` at every clip change
(`Graphics/Graphics.cpp:197`), and `TextRenderer::flush` calls `atlas->commit()`
before drawing (`Text/TextRenderer.cpp:212`). So the atlas uploads **mid-pass**,
after earlier text draws in that same pass have already bound it.

A glyph landing in free texels is harmless — earlier quads sample coordinates
nothing has touched. An atlas that **grows or resets** mid-frame is not, and a
code editor is exactly the application that reaches 4096 and resets: a screenful
of a large file in a mixed script, or a font size dragged while the atlas is
already full.

ECode does not have this hazard, and not by luck. `Widget::prepare(atlas,
visibleRect)` walks the tree before the pass opens and rasterizes every glyph the
frame will need — `visibleRect` being the same rectangle `paint()` will see as
its clip, so a virtualised list rasterizes the rows it will actually draw. Then
the atlas commits once, and the pass is opened against a texture nothing will
mutate.

`ComponentHost` needs that pre-pass, or an equivalent: deferring a reset to a
frame boundary and re-issuing the frame would also do it. Whichever way, this is
the same class of bug as the `PaintContext` ordering fix (`5ce5a0f`) and just as
invisible — the symptom is last frame's text appearing in this one, on the frame
where the atlas happened to fill.

**Test:** render into an atlas sized to overflow partway through a frame, then
assert the glyphs drawn before the overflow are still correct.
`View::renderToImage(scale)` is how, there being no texture read-back.

### 4.2 A virtualised list

`ListView` draws the visible rows of 5,000 through a `prepareRow` callback and
holds no child per row. `ScrollPanel` plus 5,000 `Component`s is an allocation
and a tree walk apiece, every frame. The sidebar file tree and the command
palette both need this, and neither is an editing feature.

Shape to aim for: a component that owns a row count and a row height, calls back
to paint and to prepare a row, and never instantiates one.

### 4.3 A scrollbar with a thumb

`ScrollPanel` has the wheel and a position indicator. `ScrollBar` has a
draggable thumb and speaks in *content* coordinates rather than thumb pixels —
which is the whole point of it, since getting that conversion right in one
direction and wrong in the other is how a scrollbar ends up almost working.

### 4.4 `Splitter`

240 lines: drawn thin, grabbed thick, reports a position rather than owning its
two panes — every arrangement already has a parent that knows how to split its
own rect. Nothing in it is editor-specific.

### 4.5 Content-driven sizing

`Component` has `resized()` and no way to ask a child how tall it wants to be.
`Widget::preferredHeight(width)` is what a scroll view sizing its range and a
list sizing itself to its rows both need.

## 5. Work that stays in ECode

- **`Chord`** — the two-key sequence state machine. Keybinding policy, not a
  framework concern, and `Component`'s key bubbling is the only framework
  support it needs.
- **`ChromeTheme`** — a settings-file schema that `Settings` and `Themes` read
  as a unit. `UI::Theme` is a different thing at a different scale.
- **`isTextInput()` / `runCommand()`** — the precedence that decides whether ⌘V
  belongs to the find field or the document. The keystroke half is covered by
  `Component`'s key bubbling; the menu-command half exists because macOS matches
  a key equivalent against the menu bar before the window sees a key at all, and
  that is workbench policy.
- **`UIText::elide`** — until something upstream wants it.

## 6. The migration itself

**It is all-or-nothing per tree.** `EditorWidget` is a child of the workbench's
tree, so there is no "chrome first, editor later": a `Component` cannot have a
`Widget` child. What can be staged is the *inside* of each widget, via an
adapter:

1. `ecode::Widget` becomes a subclass of `eacp::UI::Component`, forwarding the
   handful of virtuals ECode adds (`themeChanged`, `preferredHeight`,
   `isTextInput`, `runCommand`) and nothing else.
2. `PaintContext` is reimplemented as a facade over `UI::Graphics` rather than
   over the atlas and sprite renderer directly.
3. Those two commits move the tree, the host and all input at once, with every
   `paint()` body untouched.
4. Then the bodies migrate one widget at a time, and the facade shrinks to
   nothing.
5. Then `ECodeWidgets` is deleted.

**Absolute versus relative bounds is the real work.** `Widget` bounds are
absolute in host space, deliberately: the scissor rect is absolute, so
hit-testing is a plain `contains()` rather than a walk up the tree accumulating
offsets. `Component` is parent-relative with a `Graphics` that translates. That
is not a gap in eacp — it is a rewrite of every `layout()` into a `resized()`,
and of the render tests that assert on absolute rectangles. Mechanical, and the
bulk of the hours.

**The test harnesses churn too.** 17 test files construct a `GPUView`, an atlas
and a `PaintContext` by hand. They become a `ComponentHost` and a root component,
which is less code — but it is 17 files of it, and they are what currently proves
the chrome draws.

## 7. Order

1. **The spike of §3.** It decides scope, so nothing else starts first.
2. **§4.1 upstream** — the correctness item, before anything depends on the tier
   for text.
3. **§4.4, §4.3, §4.2, §4.5 upstream**, in that order: cheapest first, and the
   list last because it is the one with a real design question in it.
4. **The adapter** (§6, steps 1–2) in one ECode branch, with the whole suite
   green and both apps run before it merges.
5. **Paint bodies**, widget by widget.
6. **Delete `ECodeWidgets`**, rewrite `PLAN.md` §2 and §3, and update
   `CLAUDE.md`'s architecture section and the six-target list.

## 8. Risks worth naming

- **The tier is moving.** `UI::Component` gained `setPos` and `addChildren` in
  `2545c94`, and more is in flight. Migrating against an API still being shaped
  means re-fixing call sites; it is also the normal condition of this project,
  and the reason the branch exists rather than a pinned tag.
- **The shared checkout.** `~/Code/eacp` is worked in by more than one session at
  a time. `ecode-component-adoption` is a branch in that same checkout, so
  another session's commits land on it unless it switches back.
- **The split could quietly stop being enforced.** `ECodeEditorTests` catches a
  dependency added by accident. A migration adds one on purpose, and then only
  the binary size objects. Whatever §3 measures, measure it again at the end.
- **Two systems at once.** Between steps 4 and 5 the codebase has a `Component`
  tree painted through a `PaintContext` facade. That is a strictly worse place
  than either end, and the reason step 5 should not be left half-done.

## 9. Done

`ECodeWidgets` no longer exists. The chrome, the editor and the embeddable view
are all `UI::Component`s in a `ComponentHost`. 728 tests still pass, `just run`
and `just run-viewer` still draw a highlighted file, and CodeViewer's binary is
within the budget §3 set. eacp has a splitter, a scrollbar, a virtualised list
and a glyph pre-pass, each with a test and an example, and `PLAN.md` describes
the editor that actually exists.
