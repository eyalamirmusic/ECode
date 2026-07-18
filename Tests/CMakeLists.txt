# ECodeCore is deliberately free of GPU and platform dependencies, so its tests
# need no window, no device and no event loop — they are plain logic.
find_package(NanoTest REQUIRED)

nano_add_executable(ECodeCoreTests
        SOURCES
            DocumentTests.cpp
            StyleTests.cpp
        TARGETS ECodeCore)

# Highlighting runs a real grammar and a real query, but still needs no GPU: the
# spans are computed entirely on the CPU.
nano_add_executable(ECodeSyntaxTests
        SOURCES SyntaxHighlighterTests.cpp
        TARGETS ECodeSyntax)

# The renderer/highlighter integration draws off-screen, so unlike the others
# these need a GPU device and an event loop. NO_MAIN + TestMain runs them inside
# Apps::run, the same shape eacp's GPU tests use.
nano_add_executable(ECodeRenderTests
        NO_MAIN
        SOURCES
            RenderIntegrationTests.cpp
            TestMain.cpp
        TARGETS ECodeRender ECodeSyntax)
