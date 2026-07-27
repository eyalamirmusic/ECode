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
nano_add_executable(ECodeUITests
        SOURCES
            CommandPaletteTests.cpp
            ContextMenuTests.cpp
            FileTreeViewTests.cpp
            FindBarTests.cpp
            FontSettingsTests.cpp
            KeymapTests.cpp
            MenuBuilderTests.cpp
            ScrollTests.cpp
            SettingsTests.cpp
            SplitterTests.cpp
            TabBarTests.cpp
            TextFieldTests.cpp
            WidgetTests.cpp
        TARGETS ECodeUI)

# Highlighting runs a real grammar and a real query, but still needs no GPU: the
# spans are computed entirely on the CPU.
nano_add_executable(ECodeSyntaxTests
        SOURCES
            ColdOpenTests.cpp
            SyntaxHighlighterTests.cpp
        TARGETS ECodeSyntax)

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
        TARGETS ECodeSyntax ECodeUI)   # ECodeUI links ECodeRender publicly
