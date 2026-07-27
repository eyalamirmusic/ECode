build_dir := "build"
generator := "Ninja"
build_type := "Debug"

# Local eacp checkout. The ECode work needs framework changes (scissor rects,
# scroll wheel, texture sub-upload), so we build against a branch, not the
# published main. Override with: just eacp=/other/path build
eacp := justfile_directory() / ".." / "eacp"

default:
    @just --list

# EACP_UNITY_BUILD=OFF keeps compile_commands.json per-file so LSP tooling is accurate.
configure:
    cmake -S . -B {{build_dir}} -G {{generator}} \
        -DCMAKE_BUILD_TYPE={{build_type}} \
        -DEACP_UNITY_BUILD=OFF \
        -DCPM_eacp_SOURCE={{eacp}}

build: configure
    cmake --build {{build_dir}} --target ECode

[macos]
run *path: build
    open {{build_dir}}/Apps/ECode/ECode.app --args {{path}}

# The test app: one file in a window, built out of ECode::Editor not the workbench.
viewer: configure
    cmake --build {{build_dir}} --target CodeViewer

[macos]
run-viewer *path: viewer
    open {{build_dir}}/Apps/CodeViewer/CodeViewer.app --args {{path}}

test: configure
    cmake --build {{build_dir}}
    cd {{build_dir}} && ctest --output-on-failure

clean:
    rm -rf {{build_dir}}

rebuild: clean build
