#!/bin/sh
# LucidGrasp installer for Linux and macOS.
#
# Usage:
#   curl -fsSL https://github.com/dialga-cmd/LucidGrasp/releases/latest/download/install.sh | sh
#   curl -fsSL .../install.sh -o install.sh && sh install.sh --help
#
# The script resolves the latest release, verifies the SHA-256 checksum, installs
# any missing runtime dependencies, and installs LucidGrasp into a per-user
# prefix (~/.local) by default. Pass --system to install under /usr/local.
#
# Nothing in this script changes or patches the LucidGrasp source code.

set -eu

REPO="${LUCIDGRASP_REPO:-dialga-cmd/LucidGrasp}"
VERSION="${LUCIDGRASP_VERSION:-}"
PREFIX=""
SYSTEM=0
NO_DEPS=0
NO_VERIFY=0
USE_APPIMAGE=0
KEEP_QUARANTINE=0
UNINSTALL=0
DRY_RUN=0

TMP=""
MOUNT=""
SUDO=""
OS=""
ARCH=""
ASSET=""
BASE_URL=""

usage() {
    cat <<'EOF'
LucidGrasp installer (Linux and macOS)

Usage:
  install.sh [options]

Options:
  --version VERSION   Install a specific release (default: latest)
  --prefix DIR        Install prefix for Linux (default: ~/.local, or /usr/local with --system)
  --system            Install system-wide (/usr/local; macOS: /Applications). May use sudo.
  --appimage          Linux: install the AppImage instead of the tarball
  --no-deps           Do not install missing runtime dependencies
  --no-verify         Skip SHA-256 verification (not recommended)
  --keep-quarantine   macOS: leave the Gatekeeper quarantine flag in place
  --uninstall         Remove a previous installation
  --dry-run           Show what would be done, without changing anything
  -h, --help          Show this help

Environment:
  LUCIDGRASP_VERSION  Same as --version
  LUCIDGRASP_REPO     GitHub owner/repo to download from (for forks/testing)

Examples:
  curl -fsSL https://github.com/dialga-cmd/LucidGrasp/releases/latest/download/install.sh | sh
  sh install.sh --version 1.3.0 --prefix "$HOME/.local"
  sh install.sh --uninstall
EOF
}

log() { printf '%s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

cleanup() {
    if [ -n "$MOUNT" ] && [ -d "$MOUNT" ]; then
        hdiutil detach "$MOUNT" >/dev/null 2>&1 || true
    fi
    if [ -n "$TMP" ] && [ -d "$TMP" ]; then
        rm -rf "$TMP" 2>/dev/null || true
    fi
}

trap cleanup 0 1 2 3 15

while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        --version)
            [ $# -ge 2 ] || die "--version needs an argument"
            VERSION="$2"
            shift 2
            ;;
        --version=*)
            VERSION="${1#*=}"
            shift
            ;;
        --prefix)
            [ $# -ge 2 ] || die "--prefix needs an argument"
            PREFIX="$2"
            shift 2
            ;;
        --prefix=*)
            PREFIX="${1#*=}"
            shift
            ;;
        --system) SYSTEM=1; shift ;;
        --no-deps) NO_DEPS=1; shift ;;
        --no-verify) NO_VERIFY=1; shift ;;
        --appimage) USE_APPIMAGE=1; shift ;;
        --keep-quarantine) KEEP_QUARANTINE=1; shift ;;
        --uninstall) UNINSTALL=1; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        *) die "unknown option: $1 (try --help)" ;;
    esac
done

need_cmd uname
need_cmd curl
need_cmd mktemp

OS="$(uname -s)"
ARCH="$(uname -m)"

resolve_version() {
    if [ -n "$VERSION" ] && [ "$VERSION" != "latest" ]; then
        VERSION="${VERSION#v}"
        return
    fi

    need_cmd curl
    latest_url="$(curl -fsSIL -o /dev/null -w '%{url_effective}' \
        "https://github.com/${REPO}/releases/latest")" \
        || die "could not resolve the latest release; pass --version"
    tag="${latest_url##*/}"
    tag="${tag#v}"
    [ -n "$tag" ] || die "could not resolve the latest release; pass --version"
    VERSION="$tag"
}

validate_version() {
    case "$VERSION" in
        ""|*[!A-Za-z0-9._-]*) die "invalid version: $VERSION" ;;
    esac
}

default_prefix() {
    if [ -n "$PREFIX" ]; then
        case "$PREFIX" in
            /*) ;;
            *) PREFIX="$(pwd)/${PREFIX}" ;;
        esac
        printf '%s\n' "$PREFIX"
    elif [ "$SYSTEM" -eq 1 ] || [ "$(id -u)" -eq 0 ]; then
        printf '%s\n' "/usr/local"
    else
        printf '%s\n' "$HOME/.local"
    fi
}

setup_root() {
    if [ "$(id -u)" -eq 0 ]; then
        SUDO=""
        return
    fi

    # Find the nearest existing ancestor of the prefix: a missing nested
    # directory is only unwritable if its closest existing parent is.
    probe="$PREFIX"
    while [ ! -e "$probe" ] && [ "$probe" != "/" ]; do
        probe="$(dirname "$probe")"
    done

    if [ "$SYSTEM" -eq 1 ] || [ ! -w "$probe" ]; then
        need_cmd sudo
        SUDO="sudo"
    else
        SUDO=""
    fi
}

as_root_always() {
    if [ "$DRY_RUN" -eq 1 ]; then
        log "[dry-run] sudo $*"
        return 0
    fi
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    else
        need_cmd sudo
        sudo "$@"
    fi
}

as_root() {
    if [ "$DRY_RUN" -eq 1 ]; then
        if [ -n "$SUDO" ]; then
            log "[dry-run] $SUDO $*"
        else
            log "[dry-run] $*"
        fi
        return 0
    fi
    if [ -n "$SUDO" ]; then
        "$SUDO" "$@"
    else
        "$@"
    fi
}

# Package installs always need root, even when the application itself is being
# installed into a writable per-user prefix.
as_pkg_root() {
    if [ "$DRY_RUN" -eq 1 ]; then
        log "[dry-run] sudo $*"
        return 0
    fi
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    else
        need_cmd sudo
        sudo "$@"
    fi
}

fetch() {
    url="$1"
    out="$2"
    if [ "$DRY_RUN" -eq 1 ]; then
        log "[dry-run] download $url"
        return 0
    fi
    curl -fL --proto '=https' --tlsv1.2 --retry 3 --retry-delay 2 \
        -o "$out" "$url"
}

verify_asset() {
    file="$1"
    base="${file##*/}"
    if [ "$NO_VERIFY" -eq 1 ]; then
        warn "skipping SHA-256 verification for $base"
        return 0
    fi
    sumfile="${file}.sha256"
    if ! fetch "${BASE_URL}/${base}.sha256" "$sumfile"; then
        die "no SHA-256 checksum is published for ${base}; rerun with --no-verify to skip verification"
    fi
    if command -v sha256sum >/dev/null 2>&1; then
        ( cd "$TMP" && sha256sum -c "${base}.sha256" ) \
            || die "SHA-256 verification failed for $base"
    elif command -v shasum >/dev/null 2>&1; then
        ( cd "$TMP" && shasum -a 256 -c "${base}.sha256" ) \
            || die "SHA-256 verification failed for $base"
    else
        die "no sha256sum or shasum found; install coreutils or use --no-verify"
    fi
    log "Verified SHA-256 for $base"
}

download_asset() {
    name="$1"
    fetch "${BASE_URL}/${name}" "${TMP}/${name}" \
        || die "download failed: ${BASE_URL}/${name}"
    verify_asset "${TMP}/${name}"
}

missing_libs() {
    bin="$1"
    if ! command -v ldd >/dev/null 2>&1; then
        return 0
    fi
    ldd "$bin" 2>/dev/null | awk '/not found/ { print $1 }' | sort -u || true
}

manual_linux_deps() {
    cat <<'EOF'

Install the runtime dependencies with your distribution's package manager:

  Debian/Ubuntu:  sudo apt install qt6-base-dev libopencv-dev
  Fedora:         sudo dnf install qt6-qtbase opencv
  openSUSE:       sudo zypper install libQt6Widgets6 libQt6Network6 libQt6DBus6 opencv
  Arch:           sudo pacman -S qt6-base opencv
  Alpine:         sudo apk add qt6-qtbase opencv

On Debian/Ubuntu, the -dev packages are the simplest reliable way to get the
runtime libraries because the runtime package names carry an OpenCV version
suffix (for example libopencv-core406).
EOF
}

apt_pkg_exists() {
    apt-cache show "$1" >/dev/null 2>&1
}

install_deps_apt() {
    as_pkg_root apt-get update -y
    pkgs=""
    for p in \
        libqt6core6t64 libqt6core6 \
        libqt6gui6t64 libqt6gui6 \
        libqt6widgets6t64 libqt6widgets6 \
        libqt6network6t64 libqt6network6 \
        libqt6dbus6t64 libqt6dbus6 \
        libqt6opengl6t64 libqt6opengl6 \
        qt6-qpa-plugins qt6-image-formats-plugins
    do
        if apt_pkg_exists "$p"; then
            pkgs="$pkgs $p"
        fi
    done

    ocv_ver="$(apt-cache pkgnames 2>/dev/null \
        | sed -n 's/^libopencv-core\([0-9][0-9]*\)$/\1/p' \
        | sort -V | tail -n 1 || true)"
    if [ -n "$ocv_ver" ]; then
        for p in "libopencv-core${ocv_ver}" "libopencv-imgproc${ocv_ver}" \
                 "libopencv-imgcodecs${ocv_ver}" "libopencv-features2d${ocv_ver}"
        do
            if apt_pkg_exists "$p"; then
                pkgs="$pkgs $p"
            fi
        done
    fi

    if [ -n "$pkgs" ]; then
        # Intentional word splitting: $pkgs is a space-separated package list.
        # shellcheck disable=SC2086
        as_pkg_root apt-get install -y --no-install-recommends $pkgs || true
    fi

    if [ -n "$(missing_libs "$1")" ]; then
        as_pkg_root apt-get install -y --no-install-recommends qt6-base-dev libopencv-dev || true
    fi
}

install_deps_dnf() {
    # Nobara, Fedora, RHEL and friends.
    pkgs=""
    for p in qt6-qtbase qt6-qtbase-gui opencv-core opencv-imgproc \
             opencv-imgcodecs opencv-features2d
    do
        if rpm -q "$p" >/dev/null 2>&1 \
            || dnf -q list --available "$p" >/dev/null 2>&1; then
            pkgs="$pkgs $p"
        fi
    done
    if [ -n "$pkgs" ]; then
        # shellcheck disable=SC2086
        as_pkg_root dnf install -y $pkgs || true
    fi
    if [ -n "$(missing_libs "$1")" ]; then
        as_pkg_root dnf install -y qt6-qtbase opencv || true
    fi
}

install_deps_zypper() {
    pkgs=""
    for p in libQt6Core6 libQt6Gui6 libQt6Widgets6 libQt6Network6 \
             libQt6DBus6 libQt6OpenGL6 opencv
    do
        if zypper --non-interactive info "$p" >/dev/null 2>&1; then
            pkgs="$pkgs $p"
        fi
    done
    for p in libopencv410 libopencv409 libopencv408 libopencv406; do
        if zypper --non-interactive info "$p" >/dev/null 2>&1; then
            pkgs="$pkgs $p"
            break
        fi
    done
    if [ -n "$pkgs" ]; then
        # shellcheck disable=SC2086
        as_pkg_root zypper --non-interactive install -y $pkgs || true
    fi
    if [ -n "$(missing_libs "$1")" ]; then
        as_pkg_root zypper --non-interactive install -y qt6-base-devel opencv-devel || true
    fi
}

install_deps_pacman() {
    as_pkg_root pacman -S --needed --noconfirm qt6-base opencv
}

install_deps_apk() {
    as_pkg_root apk add --no-cache qt6-qtbase opencv
}

install_linux_deps() {
    bin="$1"
    if command -v apt-get >/dev/null 2>&1; then
        install_deps_apt "$bin"
    elif command -v dnf >/dev/null 2>&1; then
        install_deps_dnf "$bin"
    elif command -v zypper >/dev/null 2>&1; then
        install_deps_zypper "$bin"
    elif command -v pacman >/dev/null 2>&1; then
        install_deps_pacman
    elif command -v apk >/dev/null 2>&1; then
        install_deps_apk
    else
        warn "no supported package manager found"
        manual_linux_deps
        return 1
    fi
}

ensure_linux_deps() {
    bin="$1"
    missing="$(missing_libs "$bin")"
    if [ -z "$missing" ]; then
        log "All Linux runtime libraries are present."
        return 0
    fi

    warn "missing runtime libraries:"
    printf '%s\n' "$missing" >&2

    if [ "$NO_DEPS" -eq 1 ]; then
        warn "leaving dependencies alone because --no-deps was given"
        manual_linux_deps
        return 0
    fi

    log "Installing missing runtime dependencies..."
    install_linux_deps "$bin" || true

    missing="$(missing_libs "$bin")"
    if [ -n "$missing" ]; then
        warn "these libraries are still missing after the dependency install:"
        printf '%s\n' "$missing" >&2
        manual_linux_deps
        return 1
    fi
    log "Runtime dependencies are now satisfied."
}

write_manifest() {
    manifest="$1"
    shift
    printf '%s\n' "$@" > "${TMP}/install-manifest.txt"
    as_root mkdir -p "${manifest%/*}"
    as_root cp "${TMP}/install-manifest.txt" "$manifest"
}

install_linux_tarball() {
    need_cmd tar
    asset="lucidgrasp-linux-x64.tar.gz"
    download_asset "$asset"
    tar -xzf "${TMP}/${asset}" -C "$TMP"
    src="${TMP}/LucidGrasp"
    [ -x "${src}/LucidGrasp" ] || die "the release archive does not contain LucidGrasp"

    ensure_linux_deps "${src}/LucidGrasp" || die "could not install the required runtime libraries"

    app_dir="${PREFIX}/lib/lucidgrasp"
    bin_dir="${PREFIX}/bin"
    desktop_dir="${PREFIX}/share/applications"
    icon_dir="${PREFIX}/share/icons/hicolor/256x256/apps"
    metainfo_dir="${PREFIX}/share/metainfo"
    doc_dir="${PREFIX}/share/doc/lucidgrasp"

    as_root mkdir -p "$app_dir" "$bin_dir" "$desktop_dir" "$icon_dir" "$metainfo_dir" "$doc_dir"
    as_root cp "${src}/LucidGrasp" "${app_dir}/LucidGrasp"
    as_root chmod 755 "${app_dir}/LucidGrasp"

    if [ -f "${src}/io.github.dialga_cmd.LucidGrasp.desktop" ]; then
        sed "s|^Exec=.*|Exec=\"${app_dir}/LucidGrasp\"|" \
            "${src}/io.github.dialga_cmd.LucidGrasp.desktop" > "${TMP}/desktop-entry"
        as_root cp "${TMP}/desktop-entry" \
            "${desktop_dir}/io.github.dialga_cmd.LucidGrasp.desktop"
    fi
    if [ -f "${src}/io.github.dialga_cmd.LucidGrasp.metainfo.xml" ]; then
        as_root cp "${src}/io.github.dialga_cmd.LucidGrasp.metainfo.xml" "$metainfo_dir"
    fi
    if [ -f "${src}/lucidgrasp.png" ]; then
        as_root cp "${src}/lucidgrasp.png" \
            "${icon_dir}/io.github.dialga_cmd.LucidGrasp.png"
    fi
    for doc in README.md LICENSE CHANGELOG.md; do
        if [ -f "${src}/${doc}" ]; then
            as_root cp "${src}/${doc}" "$doc_dir"
        fi
    done

    as_root ln -sf "${app_dir}/LucidGrasp" "${bin_dir}/lucidgrasp"

    write_manifest "${app_dir}/install-manifest.txt" \
        "${bin_dir}/lucidgrasp" \
        "${app_dir}/LucidGrasp" \
        "${app_dir}/install-manifest.txt" \
        "${desktop_dir}/io.github.dialga_cmd.LucidGrasp.desktop" \
        "${icon_dir}/io.github.dialga_cmd.LucidGrasp.png" \
        "${metainfo_dir}/io.github.dialga_cmd.LucidGrasp.metainfo.xml" \
        "${doc_dir}/README.md" \
        "${doc_dir}/LICENSE" \
        "${doc_dir}/CHANGELOG.md"

    log "Installed LucidGrasp ${VERSION} to ${app_dir}"
    log "Command: ${bin_dir}/lucidgrasp"

    if [ -z "$SUDO" ]; then
        if command -v update-desktop-database >/dev/null 2>&1; then
            update-desktop-database "$desktop_dir" >/dev/null 2>&1 || true
        fi
        if command -v gtk-update-icon-cache >/dev/null 2>&1; then
            gtk-update-icon-cache -f -t "${PREFIX}/share/icons/hicolor" >/dev/null 2>&1 || true
        fi
    fi

    case ":${PATH}:" in
        *":${bin_dir}:"*) ;;
        *) warn "${bin_dir} is not on your PATH; add it to use the 'lucidgrasp' command." ;;
    esac
}

install_linux_appimage() {
    asset="LucidGrasp-x86_64.AppImage"
    download_asset "$asset"

    app_dir="${PREFIX}/lib/lucidgrasp"
    bin_dir="${PREFIX}/bin"
    desktop_dir="${PREFIX}/share/applications"
    icon_dir="${PREFIX}/share/icons/hicolor/256x256/apps"
    metainfo_dir="${PREFIX}/share/metainfo"

    as_root mkdir -p "$app_dir" "$bin_dir" "$desktop_dir" "$icon_dir" "$metainfo_dir"
    as_root cp "${TMP}/${asset}" "${app_dir}/LucidGrasp.AppImage"
    as_root chmod 755 "${app_dir}/LucidGrasp.AppImage"

    # The AppImage runtime can extract itself without FUSE, so this works in
    # containers and on machines where /dev/fuse is unavailable. Extraction also
    # gives us the bundled desktop entry and icon, and a binary to inspect for
    # missing libraries.
    squashfs=""
    if ( cd "$TMP" && chmod 755 "${TMP}/${asset}" \
        && "${TMP}/${asset}" --appimage-extract >/dev/null 2>&1 ); then
        squashfs="${TMP}/squashfs-root"
    fi

    if [ -n "$squashfs" ] && [ -x "${squashfs}/usr/bin/LucidGrasp" ]; then
        ensure_linux_deps "${squashfs}/usr/bin/LucidGrasp" \
            || die "could not install the required runtime libraries"
    else
        warn "could not unpack the AppImage to check runtime dependencies"
    fi

    if [ -n "$squashfs" ]; then
        if [ -f "${squashfs}/usr/share/applications/io.github.dialga_cmd.LucidGrasp.desktop" ]; then
            sed "s|^Exec=.*|Exec=\"${app_dir}/LucidGrasp.AppImage\"|" \
                "${squashfs}/usr/share/applications/io.github.dialga_cmd.LucidGrasp.desktop" \
                > "${TMP}/desktop-entry"
            as_root cp "${TMP}/desktop-entry" \
                "${desktop_dir}/io.github.dialga_cmd.LucidGrasp.desktop"
        fi
        icon="$(find "${squashfs}/usr/share/icons" -name 'io.github.dialga_cmd.LucidGrasp.png' 2>/dev/null | head -n 1 || true)"
        if [ -n "$icon" ]; then
            as_root cp "$icon" "${icon_dir}/io.github.dialga_cmd.LucidGrasp.png"
        fi
        if [ -f "${squashfs}/usr/share/metainfo/io.github.dialga_cmd.LucidGrasp.metainfo.xml" ]; then
            as_root cp "${squashfs}/usr/share/metainfo/io.github.dialga_cmd.LucidGrasp.metainfo.xml" \
                "$metainfo_dir"
        fi
        rm -rf "$squashfs"
    fi

    cat > "${TMP}/lucidgrasp-wrapper" <<EOF
#!/bin/sh
app="${app_dir}/LucidGrasp.AppImage"
if [ -e /dev/fuse ] && { command -v fusermount >/dev/null 2>&1 || command -v fusermount3 >/dev/null 2>&1; }; then
    exec "\$app" "\$@"
fi
exec "\$app" --appimage-extract-and-run "\$@"
EOF
    as_root cp "${TMP}/lucidgrasp-wrapper" "${bin_dir}/lucidgrasp"
    as_root chmod 755 "${bin_dir}/lucidgrasp"

    write_manifest "${app_dir}/install-manifest.txt" \
        "${bin_dir}/lucidgrasp" \
        "${app_dir}/LucidGrasp.AppImage" \
        "${app_dir}/install-manifest.txt" \
        "${desktop_dir}/io.github.dialga_cmd.LucidGrasp.desktop" \
        "${icon_dir}/io.github.dialga_cmd.LucidGrasp.png" \
        "${metainfo_dir}/io.github.dialga_cmd.LucidGrasp.metainfo.xml"

    log "Installed LucidGrasp ${VERSION} AppImage to ${app_dir}"
    log "Command: ${bin_dir}/lucidgrasp"

    case ":${PATH}:" in
        *":${bin_dir}:"*) ;;
        *) warn "${bin_dir} is not on your PATH; add it to use the 'lucidgrasp' command." ;;
    esac
}

install_linux() {
    case "$ARCH" in
        x86_64|amd64) ;;
        *) die "there is no Linux build for ${ARCH} yet (only x86_64)" ;;
    esac

    PREFIX="$(default_prefix)"
    setup_root

    if [ "$USE_APPIMAGE" -eq 1 ]; then
        ASSET="LucidGrasp-x86_64.AppImage"
    else
        ASSET="lucidgrasp-linux-x64.tar.gz"
    fi

    if [ "$DRY_RUN" -eq 1 ]; then
        log "OS: ${OS} (${ARCH})"
        log "Version: ${VERSION}"
        log "Asset: ${ASSET}"
        log "Prefix: ${PREFIX}"
        if [ "$USE_APPIMAGE" -eq 1 ]; then
            log "Would install the AppImage and its desktop entry."
        else
            log "Would install the tarball and check/install Qt6 and OpenCV runtime libraries."
        fi
        return 0
    fi

    if [ "$USE_APPIMAGE" -eq 1 ]; then
        install_linux_appimage
    else
        install_linux_tarball
    fi
}

install_macos() {
    case "$ARCH" in
        x86_64) ;;
        arm64|aarch64)
            if [ "$NO_DEPS" -eq 0 ]; then
                if ! /usr/bin/arch -x86_64 /usr/bin/true >/dev/null 2>&1; then
                    log "Installing Rosetta 2 (the macOS build is x86_64)..."
                    as_root_always /usr/sbin/softwareupdate --install-rosetta --agree-to-license \
                        || die "Rosetta 2 install failed; run 'softwareupdate --install-rosetta' manually"
                fi
            else
                warn "Apple Silicon needs Rosetta 2 for the x86_64 build; not installing it because --no-deps was given"
            fi
            ;;
        *) die "there is no macOS build for ${ARCH} yet" ;;
    esac

    ASSET="lucidgrasp-macos-${VERSION}.dmg"

    if [ "$SYSTEM" -eq 1 ] || [ -w /Applications ]; then
        DEST="/Applications"
    else
        DEST="${HOME}/Applications"
    fi

    if [ "$(id -u)" -eq 0 ]; then
        SUDO=""
    elif [ "$SYSTEM" -eq 1 ]; then
        need_cmd sudo
        SUDO="sudo"
    elif [ -e "$DEST" ] && [ ! -w "$DEST" ]; then
        need_cmd sudo
        SUDO="sudo"
    else
        SUDO=""
    fi

    if [ "$DRY_RUN" -eq 1 ]; then
        log "OS: ${OS} (${ARCH})"
        log "Version: ${VERSION}"
        log "Asset: ${ASSET}"
        log "Destination: ${DEST}/LucidGrasp.app"
        log "Would download the DMG, verify it, and copy LucidGrasp.app."
        return 0
    fi

    need_cmd hdiutil
    need_cmd ditto
    download_asset "$ASSET"

    MOUNT="${TMP}/mnt"
    mkdir -p "$MOUNT"
    hdiutil attach -nobrowse -readonly -mountpoint "$MOUNT" "${TMP}/${ASSET}" >/dev/null \
        || die "could not mount ${ASSET}"
    [ -d "${MOUNT}/LucidGrasp.app" ] || die "${ASSET} does not contain LucidGrasp.app"

    if [ -w "$(dirname "$DEST")" ] || [ "$(id -u)" -eq 0 ]; then
        mkdir -p "$DEST"
    else
        as_root mkdir -p "$DEST"
    fi
    if [ -d "${DEST}/LucidGrasp.app" ]; then
        if [ -w "${DEST}/LucidGrasp.app" ] || [ "$(id -u)" -eq 0 ]; then
            rm -rf "${DEST}/LucidGrasp.app"
        else
            as_root rm -rf "${DEST}/LucidGrasp.app"
        fi
    fi

    if [ -w "$DEST" ]; then
        ditto "${MOUNT}/LucidGrasp.app" "${DEST}/LucidGrasp.app"
    else
        as_root ditto "${MOUNT}/LucidGrasp.app" "${DEST}/LucidGrasp.app"
    fi

    hdiutil detach "$MOUNT" >/dev/null 2>&1 || true
    MOUNT=""

    if [ "$KEEP_QUARANTINE" -eq 0 ]; then
        xattr -dr com.apple.quarantine "${DEST}/LucidGrasp.app" >/dev/null 2>&1 || true
    fi

    log "Installed LucidGrasp ${VERSION} to ${DEST}/LucidGrasp.app"
    log "Open it from Launchpad, Spotlight, or: open \"${DEST}/LucidGrasp.app\""
}

uninstall_linux() {
    PREFIX="$(default_prefix)"
    setup_root
    app_dir="${PREFIX}/lib/lucidgrasp"
    manifest="${app_dir}/install-manifest.txt"

    if [ ! -f "$manifest" ]; then
        die "no installation found at ${app_dir}; use --prefix or --system to match the original install"
    fi

    if [ "$DRY_RUN" -eq 1 ]; then
        log "Would remove the installation recorded in ${manifest}"
        return 0
    fi

    while IFS= read -r path; do
        [ -n "$path" ] || continue
        if [ -e "$path" ] || [ -L "$path" ]; then
            as_root rm -f "$path"
            log "Removed $path"
        fi
    done < "$manifest"

    as_root rmdir "$app_dir" 2>/dev/null || true
    as_root rmdir "${PREFIX}/share/doc/lucidgrasp" 2>/dev/null || true
    log "Uninstalled LucidGrasp from ${PREFIX}"
}

uninstall_macos() {
    if [ "$(id -u)" -eq 0 ]; then
        SUDO=""
    elif [ ! -w /Applications ]; then
        need_cmd sudo
        SUDO="sudo"
    else
        SUDO=""
    fi

    found=0
    for app in "/Applications/LucidGrasp.app" "${HOME}/Applications/LucidGrasp.app"; do
        [ -d "$app" ] || continue
        found=1
        if [ "$DRY_RUN" -eq 1 ]; then
            log "Would remove $app"
        elif [ -w "$(dirname "$app")" ] || [ "$(id -u)" -eq 0 ]; then
            rm -rf "$app"
            log "Removed $app"
        else
            as_root rm -rf "$app"
            log "Removed $app"
        fi
    done

    [ "$found" -eq 1 ] || die "no LucidGrasp.app found in /Applications or ~/Applications"
}

case "$OS" in
    Linux|Darwin) ;;
    *) die "this installer supports Linux and macOS; on Windows use install.ps1" ;;
esac

resolve_version
validate_version
BASE_URL="https://github.com/${REPO}/releases/download/v${VERSION}"

if [ "$UNINSTALL" -eq 1 ]; then
    if [ "$OS" = "Darwin" ]; then
        uninstall_macos
    else
        uninstall_linux
    fi
    exit 0
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/lucidgrasp.XXXXXX")"

if [ "$OS" = "Darwin" ]; then
    install_macos
else
    install_linux
fi

log "Done."
