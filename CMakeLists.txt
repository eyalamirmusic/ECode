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

# Syntax highlighting is the one module with a heavyweight dependency, and it is
# the one a consumer may genuinely not want: tree-sitter and the C++ grammar are
# fetched and compiled at configure time, which is most of a cold configure. An
# embedded code view draws plain text without it, so the fetch is guarded rather
# than paid unconditionally.
#
# On by default all the same, because a consumer that wants colour should get
# ECode::Syntax by linking it rather than by discovering a flag.
option(ECODE_BUILD_SYNTAX "Build ECodeSyntax, the tree-sitter highlighter" ON)

if (ECODE_BUILD_SYNTAX)
    find_package(TreeSitter REQUIRED)
endif ()

# eacp appends its CMake helper directory to CMAKE_MODULE_PATH only within its
# own directory scope, so re-add it here: Apps/ calls find_package(ResEmbed) via
# eacp_set_gui_subsystem and needs the Find modules.
list(APPEND CMAKE_MODULE_PATH "${eacp_SOURCE_DIR}/CMake")

# And pushed up when ECode is somebody's subproject, because a directory's
# CMAKE_MODULE_PATH does not reach the caller. A consumer that links
# ECode::Editor is building a GUI app and will call eacp_set_gui_subsystem on
# its own target, which needs eacp's Find modules — without this it fails to
# configure on a missing ResEmbed, naming a package it never asked for.
if (NOT PROJECT_IS_TOP_LEVEL)
    set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" PARENT_SCOPE)
endif ()

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

# The two things a consumer does not want, defaulted off the moment ECode is not
# the root project: the app bundles and the tests. A CPMAddPackage of this repo
# should produce libraries and nothing else — an add_subdirectory that quietly
# builds somebody else's .app is the reason "just link the library" ends up
# meaning "vendor the sources".
option(ECODE_BUILD_APPS "Build the applications in Apps/" ${PROJECT_IS_TOP_LEVEL})
option(ECODE_ENABLE_TESTS "Build ECode's tests" ${PROJECT_IS_TOP_LEVEL})

if (ECODE_BUILD_APPS AND NOT ECODE_BUILD_SYNTAX)
    message(FATAL_ERROR
            "ECODE_BUILD_APPS needs ECODE_BUILD_SYNTAX: both applications colour "
            "their text with tree-sitter. Turning the grammar off is for consumers "
            "embedding ECode::Editor, which draws plain text without it.")
endif ()

if (ECODE_BUILD_APPS)
    add_subdirectory(Apps)
endif ()

if (ECODE_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif ()

# Headers, so an installed tree can be compiled against.
#
# There is deliberately no install(EXPORT) to go with this, and the reason is
# upstream rather than here: eacp has no install rules or export sets at all, so
# an ECode target that names eacp-core in its link interface cannot be exported —
# CMake refuses at configure time. Until eacp exports its targets, the supported
# route is source consumption, which is how ECode consumes eacp itself:
#
#   CPMAddPackage("gh:eyalamirmusic/ECode@0.1.0")
#   target_link_libraries(MyApp PRIVATE ECode::Editor)
#
# The ECode:: aliases each module declares are what make that line work whether
# ECode was fetched, added as a subdirectory, or is one day installed properly.
install(DIRECTORY Lib/
        DESTINATION include
        FILES_MATCHING PATTERN "*.h")
