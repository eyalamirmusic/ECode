# The editor model: no GPU, no platform, no rendering. Kept that way so it stays
# testable without a window, and so the buffer can be swapped for a rope later
# without the renderer noticing.
add_library(ECodeCore STATIC
        Commands.cpp
        Cursor.cpp
        Document.cpp
        Editor.cpp
        FileTree.cpp
        Search.cpp
        TextEdit.cpp
        TextFile.cpp)

target_include_directories(ECodeCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(ECodeCore PUBLIC eacp-core)

set_default_target_setting(ECodeCore)
