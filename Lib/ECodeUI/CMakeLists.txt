# The widget tree that lives inside the single GPUView: layout, hit-testing and
# focus. Draws through ECodeRender's PaintContext, which is where the clip stack
# and the glyph batch live.
add_library(ECodeUI STATIC
        Chrome.cpp
        CommandPalette.cpp
        EditorWidget.cpp
        FileTreeView.cpp
        FindBar.cpp
        Keymap.cpp
        ListView.cpp
        MenuBuilder.cpp
        ScrollView.cpp
        TextField.cpp
        UIText.cpp
        Widget.cpp
        WidgetHost.cpp)

target_include_directories(ECodeUI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(ECodeUI PUBLIC ECodeRender)

set_default_target_setting(ECodeUI)
