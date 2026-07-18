# ECodeCore is deliberately free of GPU and platform dependencies, so its tests
# need no window, no device and no event loop — they are plain logic.
find_package(NanoTest REQUIRED)

nano_add_executable(ECodeCoreTests
        SOURCES
            DocumentTests.cpp
            StyleTests.cpp
        TARGETS ECodeCore)
