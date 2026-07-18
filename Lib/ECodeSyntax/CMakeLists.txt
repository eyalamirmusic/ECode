# tree-sitter highlighting. Isolated in its own library so ECodeRender never
# sees a tree-sitter header: it consumes ECodeCore's Highlighter interface and
# knows nothing about how the spans were derived.
add_library(ECodeSyntax STATIC
        SyntaxHighlighter.cpp
        ${ECODE_HIGHLIGHT_QUERY_CPP})

target_include_directories(ECodeSyntax PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(ECodeSyntax
        PUBLIC ECodeCore
        PRIVATE tree-sitter ts-grammar-cpp)

set_default_target_setting(ECodeSyntax)
