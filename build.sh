#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="$SCRIPT_DIR/build"
MODE="${1:-release}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

if [ "$MODE" = "clean" ]; then
    print_info "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    print_info "Clean complete"
    exit 0
fi

select_compiler() {
    if [ -n "${TUTRAST_CXX:-}" ] && [ -x "${TUTRAST_CXX}" ]; then
        printf '%s\n' "$TUTRAST_CXX"
        return 0
    fi

    if [ -x "$HOME/miniforge3/bin/x86_64-conda-linux-gnu-c++" ]; then
        printf '%s\n' "$HOME/miniforge3/bin/x86_64-conda-linux-gnu-c++"
        return 0
    fi

    if [ -x /usr/bin/g++ ]; then
        printf '%s\n' /usr/bin/g++
        return 0
    fi

    if command -v g++ >/dev/null 2>&1; then
        command -v g++
        return 0
    fi

    if [ -x /usr/bin/clang++ ]; then
        printf '%s\n' /usr/bin/clang++
        return 0
    fi

    if command -v clang++ >/dev/null 2>&1; then
        command -v clang++
        return 0
    fi

    return 1
}

collect_supported_flags() {
    local -n requested_ref="$1"
    local -n supported_ref="$2"
    supported_ref=()
    for flag in "${requested_ref[@]}"; do
        if compiler_supports_flag "$flag"; then
            supported_ref+=("$flag")
        fi
    done
}

compiler_supports_flag() {
    local flag="$1"
    local tmpdir src bin
    tmpdir="$(mktemp -d)"
    src="$tmpdir/test.cpp"
    bin="$tmpdir/test.bin"
    cat > "$src" <<'SRC'
int main() { return 0; }
SRC
    if "$CXX_BIN" "$flag" "$src" -o "$bin" >/dev/null 2>&1; then
        rm -rf "$tmpdir"
        return 0
    fi
    rm -rf "$tmpdir"
    return 1
}

manual_build() {
    local build_type="$1"
    mkdir -p "$BUILD_DIR"

    local -a common_flags
    local -a opt_flags
    local -a arch_flags
    local -a lto_flags
    local -a omp_flags
    local -a tutrast_sources

    common_flags=(-std=c++17 -Wall -Wextra -pipe)
    tutrast_sources=(
        "$SCRIPT_DIR/src/main.cpp"
        "$SCRIPT_DIR/src/cube_parser.cpp"
        "$SCRIPT_DIR/src/cli_options.cpp"
        "$SCRIPT_DIR/src/input_parser.cpp"
        "$SCRIPT_DIR/src/grid.cpp"
        "$SCRIPT_DIR/src/cluster.cpp"
        "$SCRIPT_DIR/src/transition_state.cpp"
        "$SCRIPT_DIR/src/tunnel.cpp"
        "$SCRIPT_DIR/src/pbc.cpp"
        "$SCRIPT_DIR/src/output_writer.cpp"
        "$SCRIPT_DIR/src/kmc.cpp"
        "$SCRIPT_DIR/src/kmc_state.cpp"
        "$SCRIPT_DIR/src/matrix_utils.cpp"
    )

    if [ "$build_type" = "Debug" ]; then
        opt_flags=(-O0 -g)
        print_info "Building in Debug mode..."
    else
        opt_flags=(-O3 -DNDEBUG)
        print_info "Building in high-performance Release mode..."
    fi

    if [ -n "${TUTRAST_ARCH_FLAGS:-}" ]; then
        arch_flags=(${TUTRAST_ARCH_FLAGS})
    else
        arch_flags=(-march=znver4 -mtune=znver4)
    fi

    local -a supported_arch_flags
    collect_supported_flags arch_flags supported_arch_flags
    if [ ${#supported_arch_flags[@]} -eq 0 ] && [ -z "${TUTRAST_ARCH_FLAGS:-}" ]; then
        local -a fallback_arch_flags=(-march=native -mtune=native)
        collect_supported_flags fallback_arch_flags supported_arch_flags
    fi
    if [ ${#supported_arch_flags[@]} -eq 0 ]; then
        print_warning "No requested architecture tuning flags are supported, building without explicit arch tuning."
    fi
    arch_flags=("${supported_arch_flags[@]}")

    if compiler_supports_flag -flto; then
        lto_flags=(-flto)
    else
        lto_flags=()
        print_warning "Compiler does not support -flto, continuing without LTO."
    fi

    if compiler_supports_flag -fopenmp; then
        omp_flags=(-fopenmp)
    else
        omp_flags=()
        print_warning "Compiler does not support OpenMP, grid generator will run single-threaded."
    fi

    print_info "Compiler: $CXX_BIN"
    if [ ${#arch_flags[@]} -gt 0 ]; then
        print_info "Architecture flags: ${arch_flags[*]}"
    else
        print_info "Architecture flags: (none)"
    fi
    [ ${#lto_flags[@]} -gt 0 ] && print_info "LTO enabled"
    [ ${#omp_flags[@]} -gt 0 ] && print_info "OpenMP enabled for mkgrid"

    print_info "Compiling tutrast..."
    "$CXX_BIN" \
        "${common_flags[@]}" "${opt_flags[@]}" "${arch_flags[@]}" "${lto_flags[@]}" \
        -I"$SCRIPT_DIR/include" \
        "${tutrast_sources[@]}" \
        -o "$BUILD_DIR/tutrast" \
        -lm

    print_info "Compiling generate_pes_grid_v10_cpp..."
    "$CXX_BIN" \
        "${common_flags[@]}" "${opt_flags[@]}" "${arch_flags[@]}" "${lto_flags[@]}" "${omp_flags[@]}" \
        -I"$SCRIPT_DIR/include" \
        "$SCRIPT_DIR/src/pes_grid_v10_cpp.cpp" \
        -o "$BUILD_DIR/generate_pes_grid_v10_cpp" \
        -lm
}

if ! CXX_BIN="$(select_compiler)"; then
    print_error "No C++ compiler found. Please install g++ or clang++ in user space."
    exit 1
fi

BUILD_TYPE="Release"
if [ "$MODE" = "debug" ]; then
    BUILD_TYPE="Debug"
fi

if command -v cmake >/dev/null 2>&1; then
    print_info "Using CMake-based build..."
    mkdir -p "$BUILD_DIR"
    if ! cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_CXX_COMPILER="$CXX_BIN"; then
        print_warning "CMake configuration with Ninja failed, retrying with default generator..."
        rm -rf "$BUILD_DIR"
        mkdir -p "$BUILD_DIR"
        cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_CXX_COMPILER="$CXX_BIN"
    fi
    cmake --build "$BUILD_DIR" -j "$(nproc 2>/dev/null || echo 4)"
else
    print_warning "CMake not found in PATH, using manual high-performance build fallback."
    manual_build "$BUILD_TYPE"
fi

if [ -f "$BUILD_DIR/tutrast" ] && [ -f "$BUILD_DIR/generate_pes_grid_v10_cpp" ]; then
    print_info "Build successful!"
    print_info "Executable location: $BUILD_DIR/tutrast"
    print_info "Grid generator: $BUILD_DIR/generate_pes_grid_v10_cpp"
else
    print_error "Build failed!"
    exit 1
fi
