# GPU text drawing: turns a Document plus a viewport into quads.
add_library(ECodeRender STATIC
        TextRenderer.cpp
        GlyphBatch.cpp)

target_include_directories(ECodeRender PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(ECodeRender PUBLIC ECodeCore eacp-sprites eacp-text)

set_default_target_setting(ECodeRender)
