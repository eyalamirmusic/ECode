# The embeddable editor: one file, drawn on the GPU, and nothing around it.
#
# This is the target another project links. It deliberately depends on neither
# ECodeSyntax nor ECodeWorkbench, so a consumer that wants to show a source file
# gets no tree-sitter, no command registry, no tab strip and no sidebar. A host
# that does want highlighting links ECodeSyntax itself and hands the result to
# CodeEditorView::setHighlighter — which is the same seam Workspace's factory
# uses, and the reason ECodeCore knows only the Highlighter interface.
add_library(ECodeEditor STATIC
        CodeEditorView.cpp
        EditorWidget.cpp)

target_include_directories(ECodeEditor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(ECodeEditor PUBLIC ECodeWidgets eacp-gpu)

add_library(ECode::Editor ALIAS ECodeEditor)

set_default_target_setting(ECodeEditor)
