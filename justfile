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
run: build
    open {{build_dir}}/App/ECode.app

clean:
    rm -rf {{build_dir}}

rebuild: clean build
