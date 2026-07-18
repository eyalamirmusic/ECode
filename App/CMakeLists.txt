add_executable(ECode Main.cpp)

target_link_libraries(ECode PRIVATE eacp-sprites)

set(BUNDLE_ID "com.eacp.ecode")

set_target_properties(ECode PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "ECode"
        MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_ID}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_ID})

eacp_set_gui_subsystem(ECode)
set_default_target_setting(ECode)
