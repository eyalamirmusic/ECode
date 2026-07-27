# In dependency order: each of these links the one above it.
#
#   ECodeCore       the model — no GPU, no platform
#   ECodeSyntax     tree-sitter behind Style.h's interface
#   ECodeRender     the glyph pipeline
#   ECodeWidgets    the widget toolkit inside one GPUView
#   ECodeEditor     the embeddable editor — what another project links
#   ECodeWorkbench  tabs, sidebar, palette, find, settings — the IDE around it
#
# ECodeSyntax hangs off ECodeCore alone and nothing below it links it, which is
# what keeps tree-sitter out of an embedded code view.
add_subdirectory(ECodeCore)

if (ECODE_BUILD_SYNTAX)
    add_subdirectory(ECodeSyntax)
endif ()

add_subdirectory(ECodeRender)
add_subdirectory(ECodeWidgets)
add_subdirectory(ECodeEditor)
add_subdirectory(ECodeWorkbench)
