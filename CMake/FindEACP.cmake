include(CPM)

# The CPM package name is lowercase `eacp` so the local-checkout override is
# spelled -DCPM_eacp_SOURCE=/path/to/eacp (CPM derives the variable from NAME
# verbatim, and CMake variables are case-sensitive). Same spelling CowTerm uses.
#
# WebView is off: ECode draws every pixel itself, so the WKWebView/WebView2
# surface and the Vite/npm codegen path are dead weight in this build.
CPMAddPackage(
        NAME eacp
        GITHUB_REPOSITORY eyalamirmusic/eacp
        GIT_TAG main
        OPTIONS
            "EACP_ENABLE_EXAMPLES OFF"
            "EACP_ENABLE_TESTS OFF"
            "EACP_BUILD_WEBVIEW OFF")
