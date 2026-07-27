# The widget toolkit that lives inside a single GPUView: layout, hit-testing,
# capture, focus and hover, plus the handful of general controls built on them.
# Draws through ECodeRender's PaintContext, which is where the clip stack and the
# glyph batch live.
#
# ChromeTheme is here rather than with the workbench because ScrollView,
# TextField and Splitter each hold a reference to one — a palette living above
# the widgets that read it would point the dependency backwards. Splitting the
# struct in two would fragment the settings-file schema that Settings and Themes
# read as a unit, and buy nothing: the embeddable editor includes no theme header
# at all.
add_library(ECodeWidgets STATIC
        Chord.cpp
        ListView.cpp
        ScrollView.cpp
        Splitter.cpp
        TextField.cpp
        UIText.cpp
        Widget.cpp
        WidgetHost.cpp)

target_include_directories(ECodeWidgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(ECodeWidgets PUBLIC ECodeRender)

add_library(ECode::Widgets ALIAS ECodeWidgets)

set_default_target_setting(ECodeWidgets)
