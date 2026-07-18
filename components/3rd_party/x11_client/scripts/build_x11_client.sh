#!/usr/bin/env bash
# Build minimal X11 client stack into INSTALL_PREFIX (for cross: target .so on host).
# Stage 1 (host CC): util-macros, xorgproto (meson), xtrans, xcb-proto — arch-independent.
# Stage 2 (cross CC): libXau, libXdmcp, libxcb, libX11 (libX11 >= 1.8 requires xcb.pc + libxcb).
set -euo pipefail

SRC_ROOT="$1"
INSTALL_PREFIX="$2"
CROSS_PREFIX="${3:-}" # e.g. aarch64-linux-gnu- or empty

die() { echo "x11_client: ERROR: $*" >&2; exit 1; }

[ -d "$SRC_ROOT" ] || die "source root not a directory: $SRC_ROOT"
mkdir -p "$INSTALL_PREFIX"

# xorgproto (meson) and xcb-proto install most .pc files under share/pkgconfig; libs use lib/pkgconfig.
_MAIX_PC_PATH="${INSTALL_PREFIX}/lib/pkgconfig:${INSTALL_PREFIX}/share/pkgconfig"

find_one() {
    local pat="$1"
    local d
    d=$(find "$SRC_ROOT" -mindepth 1 -maxdepth 1 -type d -name "$pat" 2>/dev/null | sort | head -1)
    [ -n "$d" ] || die "missing extracted dir $pat under $SRC_ROOT"
    echo "$d"
}

JOBS="${JOBS:-$(nproc)}"
export PKG_CONFIG_PATH="${_MAIX_PC_PATH}${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export ACLOCAL_PATH="${INSTALL_PREFIX}/share/aclocal:${ACLOCAL_PATH:-}"
export PATH="${INSTALL_PREFIX}/bin:${PATH:-}"

if [ -n "$CROSS_PREFIX" ]; then
    XHOST="--host=${CROSS_PREFIX%-}"
    export CC="${CROSS_PREFIX}gcc"
    export CXX="${CROSS_PREFIX}g++"
    export AR="${CROSS_PREFIX}ar"
    export STRIP="${CROSS_PREFIX}strip"
    export RANLIB="${CROSS_PREFIX}ranlib"
else
    XHOST=""
fi

# --- Stage 1: host tools / headers (no cross CC) ---
SAVE_CC="${CC:-}"
SAVE_CXX="${CXX:-}"
SAVE_AR="${AR:-}"
SAVE_STRIP="${STRIP:-}"
SAVE_RANLIB="${RANLIB:-}"
unset CC CXX AR STRIP RANLIB

UM="$(find_one 'util-macros-*')"
pushd "$UM" >/dev/null
./configure --prefix="$INSTALL_PREFIX"
make -j"$JOBS"
make install
popd >/dev/null

XP="$(find_one 'xorgproto-*')"
if ! command -v meson >/dev/null 2>&1; then
    die "need 'meson' on build host (e.g. sudo apt install meson)"
fi
NINJA_BIN="ninja"
command -v ninja >/dev/null 2>&1 || NINJA_BIN="ninja-build"
command -v "$NINJA_BIN" >/dev/null 2>&1 || die "need ninja (ninja or ninja-build)"
pushd "$XP" >/dev/null
rm -rf _maix_meson
meson setup _maix_meson --prefix="$INSTALL_PREFIX" --buildtype=release
"$NINJA_BIN" -C _maix_meson
"$NINJA_BIN" -C _maix_meson install
popd >/dev/null

XT="$(find_one 'xtrans-*')"
pushd "$XT" >/dev/null
./configure --prefix="$INSTALL_PREFIX"
make -j"$JOBS"
make install
popd >/dev/null

XCBP="$(find_one 'xcb-proto-*')"
pushd "$XCBP" >/dev/null
./configure --prefix="$INSTALL_PREFIX"
make -j"$JOBS"
make install
popd >/dev/null

# --- Stage 2: target libraries ---
if [ -n "$SAVE_CC" ]; then
    export CC="$SAVE_CC"
    export CXX="$SAVE_CXX"
    export AR="$SAVE_AR"
    export STRIP="$SAVE_STRIP"
    export RANLIB="$SAVE_RANLIB"
    # Autoconf picks aarch64-linux-gnu-pkg-config when cross-compiling; that only sees the sysroot,
    # not INSTALL_PREFIX. Use host pkg-config + PKG_CONFIG_PATH for bundled .pc files.
    export PKG_CONFIG="${PKG_CONFIG:-/usr/bin/pkg-config}"
    unset PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR || true
fi

XA="$(find_one 'libXau-*')"
pushd "$XA" >/dev/null
./configure --prefix="$INSTALL_PREFIX" $XHOST \
    PKG_CONFIG_PATH="$_MAIX_PC_PATH" \
    CPPFLAGS="-I$INSTALL_PREFIX/include" \
    LDFLAGS="-L$INSTALL_PREFIX/lib"
make -j"$JOBS"
make install
popd >/dev/null

XD="$(find_one 'libXdmcp-*')"
pushd "$XD" >/dev/null
./configure --prefix="$INSTALL_PREFIX" $XHOST \
    PKG_CONFIG_PATH="$_MAIX_PC_PATH" \
    CPPFLAGS="-I$INSTALL_PREFIX/include" \
    LDFLAGS="-L$INSTALL_PREFIX/lib"
make -j"$JOBS"
make install
popd >/dev/null

XCB="$(find_one 'libxcb-*')"
pushd "$XCB" >/dev/null
# xcbgen from xcb-proto must be importable while cross-building (host Python).
_XCB_PYDIR="$(PKG_CONFIG_PATH="$_MAIX_PC_PATH" "${PKG_CONFIG:-/usr/bin/pkg-config}" --variable=pythondir xcb-proto 2>/dev/null || true)"
if [ -n "$_XCB_PYDIR" ] && [ -d "$_XCB_PYDIR" ]; then
    export PYTHONPATH="${_XCB_PYDIR}${PYTHONPATH:+:$PYTHONPATH}"
fi
command -v python3 >/dev/null 2>&1 || die "need python3 on build host for libxcb"
./configure --prefix="$INSTALL_PREFIX" $XHOST \
    --disable-static \
    --enable-shared \
    PKG_CONFIG_PATH="$_MAIX_PC_PATH" \
    CPPFLAGS="-I$INSTALL_PREFIX/include" \
    LDFLAGS="-L$INSTALL_PREFIX/lib -pthread"
make -j"$JOBS"
make install
popd >/dev/null

X11_DIR="$(find_one 'libX11-*')"
pushd "$X11_DIR" >/dev/null
# Avoid doc/man tools on minimal hosts
export XMLTO=""
export ac_cv_path_xmlto=no
_malloc0_flag=()
if [ -n "$CROSS_PREFIX" ]; then
    # Cross build cannot run the malloc(0) probe; glibc returns non-NULL for malloc(0).
    _malloc0_flag=(--disable-malloc0returnsnull)
fi
./configure --prefix="$INSTALL_PREFIX" $XHOST \
    --disable-static \
    --enable-shared \
    --disable-composecache \
    "${_malloc0_flag[@]}" \
    PKG_CONFIG_PATH="$_MAIX_PC_PATH" \
    CPPFLAGS="-I$INSTALL_PREFIX/include" \
    LDFLAGS="-L$INSTALL_PREFIX/lib -pthread"
make -j"$JOBS"
make install
popd >/dev/null

touch "$INSTALL_PREFIX/.stamp_x11_client"
echo "x11_client: installed under $INSTALL_PREFIX"
