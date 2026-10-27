#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Part of mirrortest -- bidirectional C++ test/code generator.
#
# Prepares an Ubuntu 24.04 WSL2 distro to build and use mirrortest from VS Code
# on Windows (Remote - WSL). Idempotent: safe to re-run. It installs packages
# with apt and, with --clone, clones the repository; it edits no dotfile.
#
#   tools/setup-mirrortest.sh [--with-libclang] [--with-qt] [--with-clang]
#                             [--clone[=DIR]] [--no-vscode] [--no-verify]
#
# What it installs:
#   always          build-essential (GCC 13, C++17), CMake (>= 3.20), Ninja,
#                   git (CMake fetches the dependencies with it), gdb
#   --with-libclang libclang 20 and its resource headers, for `--frontend libclang`
#   --with-qt       Qt 6 with its Test module and moc, to compile the tests that
#                   --framework qtest writes and to run the Qt round-trip tests
#   --with-clang    Clang 20 and its sanitizer runtime, to build with Clang and
#                   with ASan/UBSan as CI does
#
# Then it checks the toolchain, installs the VS Code extensions into WSL when
# `code` is on the PATH, and builds and tests the project as the README says.
# Configuring downloads the dependencies from GitHub (README, "Offline").

set -Eeuo pipefail
trap 'printf "ERROR: command failed (line %s): %s\n" "$LINENO" "$BASH_COMMAND" >&2' ERR

REPO_URL="https://github.com/juanlumc1988/mirrortest.git"
MIN_CMAKE="3.20"
# The same major as the other projects' scripts; CI tests libclang 14 to 22.
LLVM_VERSION="20"

APT_PACKAGES=(build-essential cmake ninja-build git ca-certificates gdb)
VSCODE_EXTENSIONS=(ms-vscode.cpptools ms-vscode.cmake-tools)

SUDO=()
ROOT=""
WARNINGS=()
DO_CLONE=0
CLONE_DIR=""
WITH_LIBCLANG=0
WITH_QT=0
WITH_CLANG=0
DO_VSCODE=1
DO_VERIFY=1

log()  { printf '\n==> %s\n' "$*"; }
warn() { printf 'WARNING: %s\n' "$*" >&2; WARNINGS+=("$*"); }
die()  { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<EOF
Usage: $0 [--with-libclang] [--with-qt] [--with-clang] [--clone[=DIR]] [--no-vscode] [--no-verify]

  --with-libclang  Install libclang ${LLVM_VERSION} (the optional --frontend libclang) and build with it.
  --with-qt        Install Qt 6 (Test module and moc) for Qt Test.
  --with-clang     Install Clang ${LLVM_VERSION} and its sanitizer runtime (clang++-${LLVM_VERSION}).
  --clone[=DIR]    Clone the repository into DIR (default: ~/mirrortest) when
                   the script is run standalone, outside a checkout.
  --no-vscode      Do not install VS Code extensions.
  --no-verify      Do not build and run the tests at the end.
  -h, --help       Show this help.
EOF
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --with-libclang) WITH_LIBCLANG=1 ;;
            --with-qt)       WITH_QT=1 ;;
            --with-clang)    WITH_CLANG=1 ;;
            --clone)         DO_CLONE=1 ;;
            --clone=*)       DO_CLONE=1; CLONE_DIR=${1#--clone=} ;;
            --no-vscode)     DO_VSCODE=0 ;;
            --no-verify)     DO_VERIFY=0 ;;
            -h|--help)       usage; exit 0 ;;
            *)               usage >&2; die "unknown option: $1" ;;
        esac
        shift
    done
    CLONE_DIR=${CLONE_DIR:-$HOME/mirrortest}
    CLONE_DIR=${CLONE_DIR/#\~\//$HOME/}   # --clone=~/x arrives unexpanded
}

# version_ge A B: true when A >= B.
version_ge() { printf '%s\n%s\n' "$2" "$1" | sort -V -C; }

# first_line CMD...: first line of a command's output. sed rather than head,
# so the producer is never killed by SIGPIPE under `pipefail`.
first_line() { "$@" 2>&1 | sed -n 1p; }

apt_install() { "${SUDO[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "$@"; }

check_platform() {
    log "Checking platform"

    if [[ $EUID -eq 0 ]]; then
        [[ -z ${SUDO_USER:-} ]] || die "run as your normal user, not through sudo (the script calls sudo itself)"
    else
        command -v sudo >/dev/null || die "not root and sudo not found"
        SUDO=(sudo)
    fi
    command -v apt-get >/dev/null || die "apt-get not found: this script targets Ubuntu"

    local id="" version=""
    if [[ -r /etc/os-release ]]; then
        # shellcheck source=/dev/null
        id=$(. /etc/os-release && echo "${ID:-}")
        # shellcheck source=/dev/null
        version=$(. /etc/os-release && echo "${VERSION_ID:-}")
    fi
    if [[ $id != ubuntu || $version != 24.04 ]]; then
        warn "written for Ubuntu 24.04, found '${id:-?} ${version:-?}'"
    fi

    if ! grep -qi microsoft /proc/version; then
        warn "this does not look like WSL; continuing anyway"
    elif ! uname -r | grep -qi wsl2; then
        warn "WSL1 detected: file access is slow and sanitizers do not run. From PowerShell: wsl --set-version <distro> 2"
    fi
    echo "  ok"
}

# ninja-build, gdb and the LLVM packages are in `universe`, which Ubuntu enables
# by default but a trimmed image may not.
ensure_universe() {
    if ! grep -rqsE '^Components:.*\buniverse\b|^deb .*\buniverse\b' /etc/apt/sources.list /etc/apt/sources.list.d/; then
        log "Enabling the 'universe' component"
        "${SUDO[@]}" apt-get update
        apt_install software-properties-common
        "${SUDO[@]}" add-apt-repository -y universe
    fi
}

install_packages() {
    local packages=("${APT_PACKAGES[@]}")
    if [[ $WITH_LIBCLANG == 1 ]]; then
        # -dev brings libclang1-N and the resource headers (libclang-common-N-dev);
        # they are named so that a failure to install them is plain to see.
        packages+=("libclang-${LLVM_VERSION}-dev" "libclang1-${LLVM_VERSION}" "libclang-common-${LLVM_VERSION}-dev")
    fi
    if [[ $WITH_CLANG == 1 ]]; then
        # Without the runtime, -fsanitize=address fails at link time.
        packages+=("clang-${LLVM_VERSION}" "libclang-rt-${LLVM_VERSION}-dev")
    fi
    if [[ $WITH_QT == 1 ]]; then
        packages+=(qt6-base-dev)
    fi

    log "Installing apt packages: ${packages[*]}"
    ensure_universe
    "${SUDO[@]}" apt-get update
    apt_install "${packages[@]}"
}

# probe CXX [FLAGS...]: build and run a trivial C++17 program, warnings fatal.
probe() {
    local cxx=$1 dir rc=0
    shift
    dir=$(mktemp -d)
    printf '%s\n' \
        '#include <optional>' \
        'int main() { std::optional<int> v; if constexpr (sizeof(int) >= 2) { v = 1; } return *v - 1; }' \
        >"$dir/p.cpp"
    "$cxx" -std=c++17 -Wall -Wextra -Werror "$@" "$dir/p.cpp" -o "$dir/p" >"$dir/log" 2>&1 \
        && "$dir/p" >>"$dir/log" 2>&1 || rc=$?
    if [[ $rc -ne 0 ]]; then
        sed 's/^/    /' "$dir/log" | sed -n 1,8p >&2
    fi
    rm -rf "$dir"
    return "$rc"
}

llvm_dir() { printf '/usr/lib/llvm-%s' "$LLVM_VERSION"; }

verify_toolchain() {
    log "Verifying toolchain"

    local v
    v=$(first_line cmake --version | sed -n 's/^[^0-9]*\([0-9][0-9.]*\).*$/\1/p')
    version_ge "${v:-0}" "$MIN_CMAKE" || die "CMake ${v:-not found}: the project needs $MIN_CMAKE or newer"
    command -v ninja >/dev/null || die "ninja not found"
    command -v git >/dev/null || die "git not found (CMake fetches the dependencies with it)"
    probe g++ || die "g++ cannot build a C++17 program"

    if [[ $WITH_CLANG == 1 ]]; then
        # Not fatal: nothing needs the sanitizers to build or test locally.
        local asan_hint="if it dies at startup under WSL2, try: sudo sysctl vm.mmap_rnd_bits=28"
        probe "clang++-${LLVM_VERSION}" \
            || warn "clang++-${LLVM_VERSION} cannot build a C++17 program"
        probe "clang++-${LLVM_VERSION}" -fsanitize=address,undefined \
            || warn "clang++-${LLVM_VERSION} with ASan/UBSan does not work ($asan_hint)"
    fi
    if [[ $WITH_LIBCLANG == 1 ]]; then
        [[ -f $(llvm_dir)/include/clang-c/Index.h ]] || die "libclang ${LLVM_VERSION} headers not found in $(llvm_dir)/include"
        [[ -e $(llvm_dir)/lib/libclang-${LLVM_VERSION}.so ]] || die "libclang ${LLVM_VERSION} not found in $(llvm_dir)/lib"
        [[ -d $(llvm_dir)/lib/clang ]] || die "the clang resource headers are missing (libclang-common-${LLVM_VERSION}-dev)"
    fi

    printf '  %-8s %s\n' \
        cmake "$(first_line cmake --version)" \
        ninja "$(first_line ninja --version)" \
        g++   "$(first_line g++ --version)" \
        gdb   "$(first_line gdb --version)"
    if [[ $WITH_CLANG == 1 ]]; then
        printf '  %-8s %s\n' clang "$(first_line "clang++-${LLVM_VERSION}" --version)"
    fi
}

is_repo_root() { [[ -f $1/CMakeLists.txt ]] && grep -q '^project(mirrortest' "$1/CMakeLists.txt"; }

resolve_repo() {
    local here
    here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]:-$0}")/.." 2>/dev/null && pwd) || here=""

    if [[ -n $here ]] && is_repo_root "$here"; then
        ROOT=$here
        if [[ $DO_CLONE == 1 ]]; then
            echo "  already inside a checkout, ignoring --clone"
        fi
    elif [[ $DO_CLONE == 1 ]]; then
        log "Locating checkout in $CLONE_DIR"
        if is_repo_root "$CLONE_DIR"; then
            echo "  reusing existing checkout"
        elif [[ -n $(ls -A "$CLONE_DIR" 2>/dev/null) ]]; then
            die "$CLONE_DIR exists and is not a mirrortest checkout"
        else
            git clone "$REPO_URL" "$CLONE_DIR"
        fi
        ROOT=$(cd -- "$CLONE_DIR" && pwd)
    fi

    case $ROOT in
        /mnt/*) warn "$ROOT is on the Windows filesystem: builds are slow and permissions and line endings misbehave. Keep the checkout under your Linux home (~/) instead." ;;
    esac
}

install_vscode_extensions() {
    log "Installing VS Code extensions inside WSL"

    if ! command -v code >/dev/null; then
        warn "'code' is not on the PATH (VS Code not installed on Windows, or PATH interop disabled in /etc/wsl.conf): install ${VSCODE_EXTENSIONS[*]} from the Extensions view once VS Code is connected to WSL"
        return 0
    fi

    local installed ext
    installed=$(code --list-extensions 2>/dev/null) || installed=""
    for ext in "${VSCODE_EXTENSIONS[@]}"; do
        if grep -qixF "$ext" <<<"$installed"; then
            echo "  $ext: already installed"
        elif ! code --install-extension "$ext"; then
            warn "could not install $ext"
        fi
    done
}

# Same commands as the README, so what is verified is what a user would run.
verify_build() {
    log "Building and testing $ROOT"
    local args=(-S "$ROOT" -B "$ROOT/build" -G Ninja -DCMAKE_BUILD_TYPE=Release)
    if [[ $WITH_LIBCLANG == 1 ]]; then
        args+=(-DMT_WITH_LIBCLANG=ON "-DMT_LIBCLANG_INCLUDE_DIR=$(llvm_dir)/include" "-DMT_LIBCLANG_LIBRARY=$(llvm_dir)/lib/libclang-${LLVM_VERSION}.so")
    fi
    cmake "${args[@]}"
    cmake --build "$ROOT/build" -j "$(nproc)"
    ctest --test-dir "$ROOT/build" --output-on-failure -j "$(nproc)"
    "$ROOT/build/mirrortest" --version
}

summary() {
    log "Done"
    if [[ ${#WARNINGS[@]} -gt 0 ]]; then
        printf '\n%d warning(s):\n' "${#WARNINGS[@]}"
        printf '  - %s\n' "${WARNINGS[@]}"
    fi

    local n=1
    printf '\nNext steps:\n'
    printf '  %d. Windows, once: install VS Code and the "WSL" extension (ms-vscode-remote.remote-wsl).\n' $((n++))
    if [[ -n $ROOT ]]; then
        printf '  %d. From this shell: cd %q && code .\n' $((n++)) "$ROOT"
        if [[ $DO_VERIFY == 1 ]]; then
            printf '  %d. The program is %q/build/mirrortest; to install it: cmake --install build --prefix ~/.local\n' $((n++)) "$ROOT"
        else
            printf '  %d. Build: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build\n' $((n++))
        fi
    else
        printf '  %d. Clone under your Linux home, not /mnt/c: git clone %s ~/mirrortest\n' $((n++)) "$REPO_URL"
        printf '     then: cd ~/mirrortest && code .\n'
    fi
    printf '  %d. In VS Code, let CMake Tools configure (Ninja, GCC kit), then Build / Test from the status bar.\n' $((n++))
    if [[ $WITH_LIBCLANG == 1 ]]; then
        printf '  Built with libclang %s: pass --frontend libclang to read headers with it.\n' "$LLVM_VERSION"
    fi
}

main() {
    parse_args "$@"
    check_platform
    install_packages
    verify_toolchain
    resolve_repo
    if [[ $DO_VSCODE == 1 ]]; then
        install_vscode_extensions
    fi
    if [[ $DO_VERIFY == 1 && -n $ROOT ]]; then
        verify_build
    fi
    summary
}

main "$@"
