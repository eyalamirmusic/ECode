# Everything that makes the editor an IDE rather than a text view: tab strips,
# the sidebar tree, the command palette, the find bar, the context menu, the
# menu bar spec, the keymap and the settings file.
#
# Split from ECodeWidgets and ECodeEditor so that embedding a code view costs
# none of it. Nothing below this line is linked by a host that only wants to
# show a file.
add_library(ECodeWorkbench STATIC
        Chrome.cpp
        CommandPalette.cpp
        ContextMenu.cpp
        EditorGroupView.cpp
        FileTreeView.cpp
        FindBar.cpp
        Keymap.cpp
        MenuBuilder.cpp
        Settings.cpp
        Themes.cpp)

target_include_directories(ECodeWorkbench PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(ECodeWorkbench PUBLIC ECodeEditor)

add_library(ECode::Workbench ALIAS ECodeWorkbench)

set_default_target_setting(ECodeWorkbench)
