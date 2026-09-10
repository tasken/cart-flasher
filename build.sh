#!/bin/bash
set -euo pipefail

# Change to the script's directory
cd "$(dirname "$0")"

BUILD_LOG="build.log"

# Resolve the real invoking user's UID/GID before root-owned host commands:
# when this script is run as `sudo ./build.sh`, id reports root instead.
BUILD_UID="${SUDO_UID:-$(id -u)}"
BUILD_GID="${SUDO_GID:-$(id -g)}"

restore_ownership() {
    if [ "$(id -u)" -ne 0 ] || [ -z "${SUDO_UID:-}" ] || [ -z "${SUDO_GID:-}" ]; then
        return 0
    fi

    local status=0
    local path
    for path in \
        "$BUILD_LOG" \
        .git/config \
        .git/modules/arm9/vendor/flashcart_core/config \
        .git/modules/arm9/vendor/libncgc/config \
        arm9/vendor/flashcart_core \
        arm9/vendor/libncgc \
        arm9/generated \
        arm9/build \
        arm7/build \
        arm9/cart_flasher.elf \
        arm9/cart_flasher.map \
        arm7/cart_flasher.elf \
        arm7/cart_flasher.map \
        cart_flasher-dev.nds; do
        if [ -e "$path" ]; then
            chown -R "$BUILD_UID:$BUILD_GID" "$path" || status=$?
        fi
    done
    return "$status"
}

restore_ownership_on_exit() {
    local build_status=$?
    local ownership_status=0
    trap - EXIT
    if restore_ownership; then
        :
    else
        ownership_status=$?
    fi
    if [ "$build_status" -ne 0 ]; then
        exit "$build_status"
    fi
    exit "$ownership_status"
}

trap restore_ownership_on_exit EXIT

usage() {
    echo "Usage: $0 [build|clean]"
    echo "  build  Refresh BlocksDS, clean, and build (default)"
    echo "  clean  Remove build outputs without refreshing BlocksDS"
}

require_docker() {
    if ! command -v docker >/dev/null 2>&1; then
        echo "Error: docker is not installed." >&2
        exit 1
    fi
    if ! sudo docker compose version >/dev/null 2>&1; then
        echo "Error: docker compose is not available." >&2
        exit 1
    fi
}

start_build_log() {
    rm -f "$BUILD_LOG"
    : > "$BUILD_LOG"
    # Restore this root-created file before tee opens it. The exit trap repeats
    # the repair for submodule configuration and failed builds.
    restore_ownership
    printf '%s\n' '=== Cart-Flasher build ===' | tee -a "$BUILD_LOG"
}

run_logged() {
    "$@" 2>&1 | tee -a "$BUILD_LOG"
}

initialize_submodules() {
    echo "Initializing pinned core submodules" | tee -a "$BUILD_LOG"
    run_logged git submodule sync --recursive
    run_logged git submodule update --init --checkout \
        arm9/vendor/flashcart_core \
        arm9/vendor/libncgc
    if [ ! -f arm9/vendor/flashcart_core/device.h ] \
        || [ ! -f arm9/vendor/libncgc/include/ncgc/ntrcard.h ]; then
        echo "Error: core submodule initialization failed." | tee -a "$BUILD_LOG" >&2
        exit 1
    fi
    # git submodule sync writes host Git configuration as root under sudo.
    restore_ownership
}

clean() {
    require_docker
    echo "=== Cleaning Cart-Flasher ==="
    rm -f "$BUILD_LOG"
    sudo docker compose run --rm --user "$BUILD_UID:$BUILD_GID" \
        builder sh -ceu 'make clean'
}

build() {
    require_docker
    start_build_log
    initialize_submodules
    echo "Refreshing the builder image and BlocksDS packages without Docker cache" \
        | tee -a "$BUILD_LOG"
    echo "Running: sudo docker compose build --pull --no-cache" \
        | tee -a "$BUILD_LOG"
    run_logged sudo docker compose build --pull --no-cache
    echo "Running: sudo docker compose run --rm --user \"$BUILD_UID:$BUILD_GID\" builder" \
        | tee -a "$BUILD_LOG"
    # docker compose run uses the image built immediately above unless its
    # explicit --build option is supplied. The service runs clean-then-build
    # with no network and the invoking user's UID/GID.
    run_logged sudo docker compose run --rm \
        --user "$BUILD_UID:$BUILD_GID" builder
}

case "${1:-build}" in
    build)
        build
        ;;
    clean)
        clean
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac
