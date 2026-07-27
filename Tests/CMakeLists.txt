# ECodeCore is deliberately free of GPU and platform dependencies, so its tests
# need no window, no device and no event loop — they are plain logic.
find_package(NanoTest REQUIRED)

nano_add_executable(ECodeCoreTests
        SOURCES
            CommandTests.cpp
            DocumentTests.cpp
            DocumentEditTests.cpp
            EditorGroupTests.cpp
            EditorTests.cpp
            FileTreeTests.cpp
            LineMapTests.cpp
            MultiCursorTests.cpp
            SearchTests.cpp
            StyleTests.cpp
            TextEditTests.cpp
            TextFileTests.cpp
            WorkspaceTests.cpp
            WrapMotionTests.cpp
        TARGETS ECodeCore)

# Layout, hit-testing, capture and focus are plain logic over rectangles, so the
# widget layer needs no device either — only what PaintContext's header pulls in.
nano_add_executable(ECodeWidgetsTests
        SOURCES
            ScrollTests.cpp
            TextFieldTests.cpp
            WidgetTests.cpp
        TARGETS ECodeWidgets)

# The embeddable editor.
#
# **This target and ECodeEditorRenderTests are where the library split is
# enforced.** Both link ECodeEditor alone — no workbench, no grammar — so if
# showing a file ever starts needing a command registry, a chrome theme or
# tree-sitter, they stop building. That is the only cheap way to notice: an
# accidental dependency costs an embedder link size and build time, and neither
# is visible in a picture of the running app. Apps/CodeViewer exercises the same
# API by hand but links ECode::Syntax for colour, so it cannot make this claim.
nano_add_executable(ECodeEditorTests
        SOURCES
            ReadOnlyTests.cpp
            SplitterTests.cpp
        TARGETS ECodeEditor)

# Highlighting runs a real grammar and a real query, but still needs no GPU: the
# spans are computed entirely on the CPU.
nano_add_executable(ECodeSyntaxTests
        SOURCES
            ColdOpenTests.cpp
            SyntaxHighlighterTests.cpp
        TARGETS ECodeSyntax)

nano_add_executable(ECodeWorkbenchTests
        SOURCES
            CommandPaletteTests.cpp
            ContextMenuTests.cpp
            FileTreeViewTests.cpp
            FindBarTests.cpp
            FontSettingsTests.cpp
            KeymapTests.cpp
            MenuBuilderTests.cpp
            SettingsTests.cpp
            TabBarTests.cpp
        TARGETS ECodeWorkbench)

# CodeEditorView owns its own GPU resources, so proving it draws means drawing
# it — and its own target rather than a place in ECodeRenderTests, which links
# the workbench. The whole claim is that an embedder does not need that, and a
# test that quietly linked it could not tell. See ECodeEditorTests above.
nano_add_executable(ECodeEditorRenderTests
        NO_MAIN
        SOURCES
            CodeEditorViewRenderTests.cpp
            TestMain.cpp
        TARGETS ECodeEditor)

# The renderer/highlighter integration draws off-screen, so unlike the others
# these need a GPU device and an event loop. NO_MAIN + TestMain runs them inside
# Apps::run, the same shape eacp's GPU tests use.
nano_add_executable(ECodeRenderTests
        NO_MAIN
        SOURCES
            ChromeRenderTests.cpp
            ColdOpenRenderTests.cpp
            ContextMenuRenderTests.cpp
            DamageTests.cpp
            EditorGroupRenderTests.cpp
            FindRenderTests.cpp
            FontRenderTests.cpp
            HorizontalScrollTests.cpp
            MultiCursorRenderTests.cpp
            PaletteRenderTests.cpp
            RenderIntegrationTests.cpp
            SplitterRenderTests.cpp
            TabRenderTests.cpp
            TestMain.cpp
            ThemeRenderTests.cpp
            WrapRenderTests.cpp
        TARGETS ECodeSyntax ECodeWorkbench)   # the workbench links ECodeRender publicly
