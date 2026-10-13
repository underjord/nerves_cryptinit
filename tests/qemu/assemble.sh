#!/bin/bash
# Per-scenario image assembly. Runs inside the Docker build container.
# Reads the scenario directory and produces:
#   build/test-<scenario>/rootfs.img         (squashfs + dm-verity tree,
#                                              optionally dm-crypt-encrypted)
#   build/test-<scenario>/roothash           (verity root hash, hex)
#   build/test-<scenario>/initramfs.cpio.gz
#
# dm-verity is mandatory, so every rootfs gets a hash tree appended after
# the filesystem at a 4096-aligned offset. init derives that offset at boot
# from the squashfs size, so it is NOT written anywhere; the harness only
# needs to compute the root hash, written to build/test-<scenario>/roothash
# for run-test.sh to append to the kernel cmdline as verity_roothash=.
#
# Layering is flash -> optional dm-crypt -> dm-verity -> squashfs: when a
# key.bin is present the whole image (filesystem AND hash tree) is encrypted
# offline with fwup, so the hash tree is encrypted on disk too. fwup's
# aes-xts-plain64 mode is byte-compatible with what dm-crypt decrypts at
# runtime (AES-256-XTS, 512-byte data units, plain64 IV).
#
# Usage: assemble.sh <scenario>

set -euo pipefail

SCENARIO=${1:?scenario name required}

QEMU_DIR=/work/tests/qemu
OUTPUT=$QEMU_DIR/build/output
SCEN_DIR=$QEMU_DIR/$SCENARIO
TEST_BUILD=$QEMU_DIR/build/test-$SCENARIO

[ -d "$SCEN_DIR" ] || { echo "no such scenario: $SCEN_DIR" >&2; exit 1; }

rm -rf "$TEST_BUILD"
mkdir -p "$TEST_BUILD"

# dm-verity geometry. 4096-byte data/hash blocks are veritysetup's defaults;
# init reads them (and the algorithm and salt) back out of the superblock.
DATA_BLOCK_SIZE=4096
HASH_BLOCK_SIZE=4096

# --- rootfs (squashfs + dm-verity hash tree) ---------------------------

ROOTFS_DIR=$TEST_BUILD/rootfs
mkdir -p "$ROOTFS_DIR/sbin" \
         "$ROOTFS_DIR/etc" \
         "$ROOTFS_DIR/dev" \
         "$ROOTFS_DIR/proc" \
         "$ROOTFS_DIR/sys"

# Statically-linked /sbin/init that prints the scenario's marker and
# powers off. Same binary for every scenario; the scenario provides
# /etc/test-marker.
install -m 755 "$OUTPUT/test_init" "$ROOTFS_DIR/sbin/init"

if [ -f "$SCEN_DIR/test-marker" ]; then
    install -m 644 "$SCEN_DIR/test-marker" "$ROOTFS_DIR/etc/test-marker"
fi

# Make the rootfs squashfs.
PLAIN=$TEST_BUILD/rootfs.plain.img
mksquashfs "$ROOTFS_DIR" "$PLAIN" \
    -comp xz -no-progress -quiet -all-root

# The hash tree is co-located, appended after the filesystem at a
# data-block-aligned offset. Pad the filesystem out to that offset so the
# data area is exactly [0, HASH_OFFSET) and bound veritysetup to it.
SQ_SIZE=$(stat -c%s "$PLAIN")
HASH_OFFSET=$(( (SQ_SIZE + DATA_BLOCK_SIZE - 1) / DATA_BLOCK_SIZE * DATA_BLOCK_SIZE ))
DATA_BLOCKS=$(( HASH_OFFSET / DATA_BLOCK_SIZE ))
PAD=$(( HASH_OFFSET - SQ_SIZE ))
if [ "$PAD" -gt 0 ]; then
    dd if=/dev/zero bs=1 count="$PAD" >> "$PLAIN" 2>/dev/null
fi

# Format: data device == hash device == the same file, hash area (superblock
# first) at HASH_OFFSET. Pure userspace — no loop/dm, so no privileged
# Docker needed. veritysetup picks a random salt and sha256 by default and
# records both in the superblock.
ROOT_HASH=$(veritysetup format \
    --data-block-size="$DATA_BLOCK_SIZE" \
    --hash-block-size="$HASH_BLOCK_SIZE" \
    --data-blocks="$DATA_BLOCKS" \
    --hash-offset="$HASH_OFFSET" \
    "$PLAIN" "$PLAIN" \
    | awk '/Root hash:/ { print $NF }')
[ -n "$ROOT_HASH" ] || { echo "veritysetup format produced no root hash" >&2; exit 1; }
echo "$ROOT_HASH" > "$TEST_BUILD/roothash"

# Pad the whole image (filesystem + hash tree) to 512-byte alignment for
# AES-XTS. The hash area is already block-aligned, so this is usually a
# no-op, but keep it for safety.
SIZE=$(stat -c%s "$PLAIN")
PAD=$(( (512 - SIZE % 512) % 512 ))
if [ "$PAD" -gt 0 ]; then
    dd if=/dev/zero bs=1 count="$PAD" >> "$PLAIN" 2>/dev/null
fi

# Encrypt if a key file is present, otherwise just copy. Encryption covers
# the hash tree too, since it is part of the same image.
#
# fwup writes the rootfs out with dm-crypt's aes-xts-plain64 cipher at
# block offset 0, exactly as init's crypt layer expects to read it back.
# The cipher only supports AES-256-XTS, so key.bin must be 64 raw bytes
# (128 hex chars); init then sets up dm-crypt with the matching key.
if [ -f "$SCEN_DIR/key.bin" ]; then
    KEY_LEN=$(stat -c%s "$SCEN_DIR/key.bin")
    [ "$KEY_LEN" -eq 64 ] || {
        echo "key.bin must be 64 bytes for aes-xts-plain64 (AES-256-XTS); got $KEY_LEN" >&2
        exit 1
    }
    # fwup wants the secret hex-encoded; key.bin holds the raw bytes.
    CRYPT_SECRET=$(od -An -v -tx1 "$SCEN_DIR/key.bin" | tr -d ' \n')
    export CRYPT_SECRET

    FWUP_CONF=$TEST_BUILD/encrypt.conf
    cat > "$FWUP_CONF" <<EOF
file-resource rootfs.img {
    host-path = "$PLAIN"
}

task encrypt {
    on-resource rootfs.img {
        raw_write(0, "cipher=aes-xts-plain64", "secret=\${CRYPT_SECRET}")
    }
}
EOF

    fwup -c -f "$FWUP_CONF" -o "$TEST_BUILD/encrypt.fw"
    fwup -a -d "$TEST_BUILD/rootfs.img" -i "$TEST_BUILD/encrypt.fw" -t encrypt -q -y
else
    cp "$PLAIN" "$TEST_BUILD/rootfs.img"
fi

# --- initramfs ---------------------------------------------------------

INITRAMFS_DIR=$TEST_BUILD/initramfs
mkdir -p "$INITRAMFS_DIR/etc" \
         "$INITRAMFS_DIR/dev" \
         "$INITRAMFS_DIR/proc" \
         "$INITRAMFS_DIR/sys" \
         "$INITRAMFS_DIR/mnt"

install -m 755 "$OUTPUT/init" "$INITRAMFS_DIR/init"

# Stage the scenario's initramfs.conf if it has one. init derives the verity
# hash offset from the filesystem size, so a verity-only scenario needs no
# config file at all.
if [ -f "$SCEN_DIR/initramfs.conf" ]; then
    cp "$SCEN_DIR/initramfs.conf" "$INITRAMFS_DIR/etc/initramfs.conf"
fi

(cd "$INITRAMFS_DIR" && find . -print0 | cpio --null -ov --format=newc 2>/dev/null) \
    | gzip > "$TEST_BUILD/initramfs.cpio.gz"

echo "Assembled: $TEST_BUILD/{initramfs.cpio.gz,rootfs.img}"
echo "  verity_hash_offset=$HASH_OFFSET data_blocks=$DATA_BLOCKS root_hash=$ROOT_HASH"

# Hand the assembled tree back to the invoking host user. On Linux, files a
# root container writes to a bind mount are owned by root:root on the host,
# so run-test.sh (which runs on the host as an unprivileged user) can't write
# serial.log/qemu.log into this directory. macOS Docker Desktop remaps bind
# mount ownership to the host user, which is why this only bites on Linux.
# The 2>/dev/null || true keeps it harmless where chown isn't supported.
if [ -n "${HOST_UID:-}" ]; then
    chown -R "$HOST_UID:${HOST_GID:-$HOST_UID}" "$TEST_BUILD" 2>/dev/null || true
fi
