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
#    - ldid in PATH (brew install ldid) for pseudo-signing

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
xcrun clang++ -arch x86_64 -mkernel -DKERNEL \
    -std=c++17 -fno-exceptions -fno-rtti -fno-stack-protector \
    -msoft-float -fapple-kext \
    -I"$SDK/Headers" \
    $EXTRA_CFLAGS \
    -c "$SRC/$PRODUCT.cpp" -o "$OUT/$PRODUCT.o"

echo "[2/4] Linking..."
# -static is mandatory with -kernel: kexts have no dynamic linker, all
# references are statically bound and unresolved symbols (OSMetaClass etc.)
# are resolved by the kernel's kmod loader at load time.
# libkmod comes from MacKernelSDK's kmod (kmod_start/kmod_stop wrappers).
xcrun ld -static -arch x86_64 -kernel \
    -L"$SDK/Library/x86_64" -lkmod \
    -sectcreate __TEXT __info_plist "$SRC/Info.plist" \
    -exported_symbol _kmod_start \
    -exported_symbol _kmod_stop \
    "$OUT/$PRODUCT.o" -o "$OUT/$PRODUCT"

echo "[3/4] Assembling kext bundle..."
cp "$OUT/$PRODUCT" "$OUT/$PRODUCT.kext/Contents/MacOS/$PRODUCT"
cp "$SRC/Info.plist" "$OUT/$PRODUCT.kext/Contents/Info.plist"

echo "[4/4] Pseudo-signing..."
if command -v ldid >/dev/null; then
    ldid -S "$OUT/$PRODUCT.kext/Contents/MacOS/$PRODUCT"
else
    echo "WARNING: ldid not found; kext is unsigned (OK for OpenCore injection)"
fi

echo "Done: $OUT/$PRODUCT.kext"
echo "Next: copy into EFI/OC/Kexts and add to config.plist, or install to /L/E for testing."
