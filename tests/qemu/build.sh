#!/bin/bash
# Builds, inside the Docker container, all the host-side artifacts the
# QEMU tests need:
#   - Linux kernel for QEMU aarch64 (with dm-crypt + verity + squashfs)
#   - Static busybox for the test rootfs's userland
#   - nerves_cryptinit cross-built for aarch64
#
# Offline rootfs encryption is handled at assemble time by fwup (installed
# in the Docker image), so there's no host crypto helper to build here.
#
# The Buildroot build itself runs in /build (a named Docker volume, not a
# host bind mount, because Buildroot needs to chmod files freely and Docker
# Desktop on macOS bind mounts don't support that). Only the small final
# artifacts get copied to /work/tests/qemu/build/output on the host.

set -euo pipefail

SRC=/work
QEMU_DIR=$SRC/tests/qemu
HOST_OUTPUT=$QEMU_DIR/build/output

BR_VERSION=2024.08
BR_DIR=/build/buildroot-$BR_VERSION
BR_OUTPUT=/build/br-output

mkdir -p /build "$HOST_OUTPUT"

if [ ! -d "$BR_DIR" ]; then
    echo "==> Downloading Buildroot $BR_VERSION..."
    cd /build
    wget -q "https://buildroot.org/downloads/buildroot-$BR_VERSION.tar.gz"
    tar xf "buildroot-$BR_VERSION.tar.gz"
    rm "buildroot-$BR_VERSION.tar.gz"
fi

if [ ! -f "$BR_OUTPUT/images/Image" ]; then
    echo "==> Configuring Buildroot..."
    mkdir -p "$BR_OUTPUT"

    cd "$BR_DIR"
    make O="$BR_OUTPUT" qemu_aarch64_virt_defconfig

    # Layer in the kernel options we need on top of Buildroot's defconfig.
    {
        echo
        echo '# nerves_cryptinit additions'
        echo 'BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES="'"$QEMU_DIR/kernel.fragment"'"'
        echo 'BR2_STATIC_LIBS=y'
    } >> "$BR_OUTPUT/.config"

    make O="$BR_OUTPUT" olddefconfig

    echo "==> Building Buildroot (slow first-time step, ~30 minutes)..."
    make O="$BR_OUTPUT" -j"$(nproc)"
fi

echo "==> Cross-building nerves_cryptinit..."
TARGET_CC=$(ls "$BR_OUTPUT"/host/bin/aarch64-*-linux-*-gcc | head -1)
[ -n "$TARGET_CC" ] || { echo "ERROR: no cross-gcc found"; exit 1; }

# Build into a clean /tmp copy so any host-built artifacts in src/ don't
# fool make into thinking the binary is up-to-date.
rm -rf /tmp/cryptinit-src
cp -r "$SRC/src" /tmp/cryptinit-src
cd /tmp/cryptinit-src
rm -f init *.o compat/*.o
make CC="$TARGET_CC" \
     CFLAGS="-static -no-pie -fno-pie -Wall -Wextra -Os" \
     LDFLAGS="-static -no-pie"
cp init "$HOST_OUTPUT/init"

echo "==> Cross-building test_init (rootfs PID 1 for QEMU scenarios)..."
"$TARGET_CC" -static -no-pie -fno-pie -Os -Wall -Wextra \
    -o "$HOST_OUTPUT/test_init" \
    "$QEMU_DIR/lib/test_init.c"

echo "==> Copying kernel..."
cp "$BR_OUTPUT/images/Image" "$HOST_OUTPUT/Image"

echo "==> Done. Artifacts:"
ls -la "$HOST_OUTPUT"

# Hand the artifacts back to the invoking host user. Without this, files a
# root container writes to the bind-mounted repo stay root:root on Linux, so
# `make clean`/`distclean` (and reruns) can't remove them. macOS Docker
# Desktop already remaps ownership to the host user, so this is a no-op there.
if [ -n "${HOST_UID:-}" ]; then
    chown -R "$HOST_UID:${HOST_GID:-$HOST_UID}" "$HOST_OUTPUT" 2>/dev/null || true
fi
