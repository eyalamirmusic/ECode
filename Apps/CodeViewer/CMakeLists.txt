# A single code file in a window, built the way a consumer builds one.
#
# Two libraries, named through the aliases a downstream project would use.
# ECode::Syntax is here rather than pulled in by ECode::Editor, and that is the
# whole shape of the highlighting seam: the editor knows the Highlighter
# interface, an app that wants colour links a grammar and hands one over. An
# embedder that does not want tree-sitter simply leaves this line at one library.
#
# What is *not* here is the point. No ECode::Workbench, so no tabs, no sidebar,
# no palette, no keymap, no settings file — none of it can turn up by accident,
# because none of it is linked. The build-time guard against ECodeEditor growing
# a dependency on the workbench lives in Tests/CMakeLists.txt, where
# ECodeEditorTests and ECodeEditorRenderTests link ECodeEditor alone.
add_executable(CodeViewer Main.cpp)

target_link_libraries(CodeViewer PRIVATE ECode::Editor ECode::Syntax)

set(BUNDLE_ID "com.eacp.ecode.codeviewer")

set_target_properties(CodeViewer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "CodeViewer"
        MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_ID}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_ID})

eacp_set_gui_subsystem(CodeViewer)
set_default_target_setting(CodeViewer)
