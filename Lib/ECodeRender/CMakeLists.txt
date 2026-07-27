# GPU text drawing: turns a Document plus a viewport into quads, and carries the
# clip stack the GPU does not have.
add_library(ECodeRender STATIC
        ColorJson.cpp
        FontSettings.cpp
        PaintContext.cpp
        RowCache.cpp
        TextRenderer.cpp
        TextTheme.cpp)

target_include_directories(ECodeRender PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(ECodeRender PUBLIC ECodeCore eacp-sprites eacp-text)

set_default_target_setting(ECodeRender)
