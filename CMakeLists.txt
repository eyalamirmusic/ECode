cmake_minimum_required(VERSION 3.31)
project(ECode VERSION 0.1.0 LANGUAGES C CXX)

# The glyph atlas backend is Objective-C++ on Apple (CoreText).
if (APPLE)
    enable_language(OBJCXX)
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# eacp links its static libraries into a GUI executable; every static dep must be
# position-independent, matching eacp's own top-level build.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# MSVC compiles the standard library's try/catch without unwind semantics unless
# told otherwise (C4530), which is fatal in deps that build warnings-as-errors.
# Set before the CPM packages so their targets inherit it.
if (MSVC)
    add_compile_options(/EHsc)
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

include(CPM)
find_package(EACP REQUIRED)
find_package(TreeSitter REQUIRED)

# eacp appends its CMake helper directory to CMAKE_MODULE_PATH only within its
# own directory scope, so re-add it here: App/ calls find_package(ResEmbed) via
# eacp_set_gui_subsystem and needs the Find modules.
list(APPEND CMAKE_MODULE_PATH "${eacp_SOURCE_DIR}/CMake")

# eacp runs eacp_default_setup() only when it is the root project. ECode is root,
# so reproduce the part the app relies on: the macOS bundle Info.plist template
# that set_default_target_setting() stamps onto the app (NSHighResolutionCapable
# et al.), plus the deployment target. eacp_setup_apple is a macro keyed off
# CMAKE_CURRENT_SOURCE_DIR, so it cannot be reused verbatim from here; point the
# plist variable straight at eacp's template instead.
if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
    set(EACP_MACOS_PLIST "${eacp_SOURCE_DIR}/CMake/macOSBundleInfo.plist.in"
            CACHE INTERNAL "eacp macOS bundle Info.plist template")
endif ()

add_subdirectory(Lib)
add_subdirectory(App)

option(ECODE_ENABLE_TESTS "Build ECode's tests" ${PROJECT_IS_TOP_LEVEL})

if (ECODE_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif ()
