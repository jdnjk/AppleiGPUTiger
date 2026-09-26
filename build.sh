#!/bin/bash
#
#  build.sh — AppleIntelTGLGraphics
#
#  Builds the kext against MacKernelSDK on a real Mac (or macOS VM).
#  This CANNOT run on Windows — kernel extensions must be compiled with
#  Apple clang targeting the XNU kernel and pseudo-signed with ldid.
#
#  Usage:
#    ./build.sh                        # release
#    DEBUG=1 ./build.sh                # debug (kprintf logging, verbose probe)
#    MAC_KERNEL_SDK=/path/sdk ./build.sh   # explicit SDK location
#
#  Requirements:
#    - Xcode 12 or newer with command line tools
#    - MacKernelSDK: cloned to ../MacKernelSDK, ./MacKernelSDK, or set
#      MAC_KERNEL_SDK env var (GitHub Actions does the latter)
#    - codesign (ships with macOS) for ad-hoc signing

set -e

PRODUCT="AppleIntelTGLGraphics"
SRC="AppleIntelTGLGraphics"
OUT="build"

SDK="${MAC_KERNEL_SDK:-../MacKernelSDK}"
if [ ! -d "$SDK" ] && [ -d "./MacKernelSDK" ]; then
    SDK="./MacKernelSDK"
fi
if [ ! -d "$SDK" ]; then
    SDK="$(mktemp -d)/MacKernelSDK"
    git clone --depth=1 https://github.com/acidanthera/MacKernelSDK "$SDK"
fi

EXTRA_CFLAGS=""
if [ -n "$DEBUG" ]; then
    EXTRA_CFLAGS="-DDEBUG"
fi

mkdir -p "$OUT/$PRODUCT.kext/Contents/MacOS"

echo "[1/4] Compiling..."
# Note: '-mkernel' (clang) here; '-kernel' is an ld-only flag used below.
# -DKERNEL is required: IOKit/pci/IOPCIDevice.h guards the real kernel-side
# class behind `#if defined(KERNEL)` (Xcode passes it for kext targets;
# a manual clang invocation must add it, otherwise the DriverKit user-space
# branch is compiled and configRead16 etc. do not exist).
OBJS=()
for src in "$SRC"/*.cpp; do
    obj="$OUT/$(basename "${src%.cpp}").o"
    echo "  compiling $(basename "$src")"
    xcrun clang++ -arch x86_64 -mkernel -DKERNEL \
        -std=c++17 -fno-exceptions -fno-rtti -fno-stack-protector \
        -msoft-float -fapple-kext \
        -I"$SDK/Headers" \
        $EXTRA_CFLAGS \
        -c "$src" -o "$obj"
    OBJS+=("$obj")
done

echo "[2/4] Linking..."
# Kext linking differs between linker generations:
#   New linker (Xcode 15+ / ld-prime): `-kext` implies static + kernel and
#     preserves relocations; `-static` and `-keep_relocs` were REMOVED.
#   Classic ld64 (<= Xcode 14): needs the full
#     `-static -kernel -kext -keep_relocs` combination.
# Try the modern form first, fall back to the classic one.
link_kext() {
    xcrun ld "$@" \
        "${OBJS[@]}" -lkmod \
        -o "$OUT/$PRODUCT"
}

MODERN_ARGS=(-arch x86_64 -kext
    -L"$SDK/Library/x86_64"
    -sectcreate __TEXT __info_plist "$SRC/Info.plist"
    -exported_symbol _kmod_start
    -exported_symbol _kmod_stop)

CLASSIC_ARGS=(-static -arch x86_64 -kernel -kext -keep_relocs
    -L"$SDK/Library/x86_64"
    -sectcreate __TEXT __info_plist "$SRC/Info.plist"
    -exported_symbol _kmod_start
    -exported_symbol _kmod_stop)

if ! link_kext "${MODERN_ARGS[@]}" 2>"$OUT/ld.log"; then
    if grep -qE "unknown options|must be used with" "$OUT/ld.log"; then
        echo "  new linker rejected the flags, retrying with classic ld64 form"
        link_kext "${CLASSIC_ARGS[@]}"
    else
        cat "$OUT/ld.log"
        exit 1
    fi
fi

echo "[3/4] Assembling kext bundle..."
cp "$OUT/$PRODUCT" "$OUT/$PRODUCT.kext/Contents/MacOS/$PRODUCT"
cp "$SRC/Info.plist" "$OUT/$PRODUCT.kext/Contents/Info.plist"

echo "[4/4] Ad-hoc signing..."
# `ld -kext` produces an MH_KEXT_BUNDLE (filetype 0xB), which ldid does NOT
# support (it only handles EXECUTE/DYLIB/DYLINKER/BUNDLE). macOS's own
# codesign understands kext bundles natively, so use it for ad-hoc signing.
# An unsigned kext also works when injected by OpenCore, so failure here is
# non-fatal.
if ! codesign --force --sign - "$OUT/$PRODUCT.kext" 2>&1; then
    echo "WARNING: ad-hoc signing failed; kext stays unsigned (OK for OpenCore injection)"
fi

echo "Done: $OUT/$PRODUCT.kext"
echo "Next: copy into EFI/OC/Kexts and add to config.plist, or install to /L/E for testing."
