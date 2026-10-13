#!/bin/bash
set -e

cd "$(dirname "$0")"

MAKE=$(command -v gmake make | head -1)
INIT="$(pwd)/../src/init"
FIXTURE="$(pwd)/fixture/init_fixture.so"

fail=0

# --- unit tests ----------------------------------------------------------

echo "Building unit tests..."
$MAKE -C unit >/dev/null

echo "Running unit tests..."
for t in unit/test_*; do
    [ -f "$t" ] && [ -x "$t" ] || continue
    if ! "$t"; then
        fail=1
    fi
done

# --- integration tests ---------------------------------------------------

echo "Building integration test fixture..."
$MAKE -C fixture >/dev/null

if [ ! -x "$INIT" ]; then
    echo "src/init is not built — run 'make' from the project root first."
    exit 1
fi

# Write an `nbytes`-wide little-endian integer into a file at a byte offset.
#   $1 file  $2 offset  $3 nbytes  $4 value
write_le() {
    local file="$1" off="$2" n="$3" val="$4" i byte
    for ((i = 0; i < n; i++)); do
        byte=$(( (val >> (8 * i)) & 0xff ))
        printf "\\x$(printf '%02x' "$byte")" \
            | dd of="$file" bs=1 seek=$((off + i)) conv=notrunc 2>/dev/null
    done
}

# Stage a minimal squashfs superblock at the start of a device file so init
# can probe the filesystem size and derive the verity hash offset from it
# (offset = round_up(fs_size, 4096)). init only reads the magic at offset 0
# and bytes_used at offset 40; nothing else needs to be valid.
#   $1 file     target device file under $WORK_DIR
#   $2 fs_size  bytes_used to advertise (use a 4096 multiple to keep the
#               derived hash offset equal to fs_size)
stage_squashfs_header() {
    local file="$1" fs_size="$2"
    mkdir -p "$(dirname "$file")"
    printf 'hsqs' | dd of="$file" bs=1 seek=0 conv=notrunc 2>/dev/null  # magic 0x73717368
    write_le "$file" 40 8 "$fs_size"                                    # bytes_used (u64)
}

# Stage a block-device file carrying a minimal dm-verity superblock that
# init can parse: sha256, 4096-byte data/hash blocks, no salt. Verity is
# mandatory, so every integration test needs one of these on its rootfs
# device (or, when dm-crypt is in play, on the decrypted /dev/dm-N file).
#   $1 file          target device file under $WORK_DIR
#   $2 hash_offset   byte offset of the superblock (== filesystem size)
#   $3 data_blocks   number of data blocks (small, < 256, for the test)
stage_verity_device() {
    local file="$1" offset="$2" data_blocks="$3"
    mkdir -p "$(dirname "$file")"
    # Size the file to just past the superblock.
    dd if=/dev/zero of="$file" bs=1 count=512 seek="$offset" 2>/dev/null
    printf 'verity' | dd of="$file" bs=1 seek=$((offset + 0))  conv=notrunc 2>/dev/null  # signature
    printf '\x01'   | dd of="$file" bs=1 seek=$((offset + 8))  conv=notrunc 2>/dev/null  # version = 1
    printf '\x01'   | dd of="$file" bs=1 seek=$((offset + 12)) conv=notrunc 2>/dev/null  # hash_type = 1
    printf 'sha256' | dd of="$file" bs=1 seek=$((offset + 32)) conv=notrunc 2>/dev/null  # algorithm
    printf '\x10'   | dd of="$file" bs=1 seek=$((offset + 65)) conv=notrunc 2>/dev/null  # data_block_size = 4096
    printf '\x10'   | dd of="$file" bs=1 seek=$((offset + 69)) conv=notrunc 2>/dev/null  # hash_block_size = 4096
    printf "\\x$(printf '%02x' "$data_blocks")" \
                    | dd of="$file" bs=1 seek=$((offset + 72)) conv=notrunc 2>/dev/null  # data_blocks
    # salt_size stays 0 → salt rendered as "-"
}

run_integration_test() {
    local test_script="$1"
    # Test outputs (expected/results) live in the parent dir; WORK_DIR is the
    # fake root and the init's cleanup_dir will scrub anything inside it.
    local TMP_DIR
    TMP_DIR=$(mktemp -d -t cryptinit_test.XXXXXX)
    WORK_DIR="$TMP_DIR/root"
    CMDLINE_FILE="$WORK_DIR/proc/cmdline"
    CONFIG="$WORK_DIR/etc/initramfs.conf"
    EXPECTED="$TMP_DIR/expected"
    RESULTS="$TMP_DIR/results"

    mkdir -p "$WORK_DIR/proc" "$WORK_DIR/dev" "$WORK_DIR/sys" "$WORK_DIR/mnt"

    # The test script writes the cmdline, optionally /etc/initramfs.conf,
    # stages anything in $WORK_DIR it needs, and writes $EXPECTED.
    source "./$test_script"

    set +e
    (LD_PRELOAD=$FIXTURE DYLD_INSERT_LIBRARIES=$FIXTURE WORK="$WORK_DIR" \
        "$INIT") >"$RESULTS" 2>&1
    set -e

    if diff -u "$EXPECTED" "$RESULTS" >/dev/null; then
        echo "PASS: $test_script"
    else
        echo "FAIL: $test_script"
        diff -u "$EXPECTED" "$RESULTS" || true
        fail=1
    fi

    rm -rf "$TMP_DIR"
}

echo "Running integration tests..."
for test_script in [0-9][0-9][0-9]_*; do
    [ -f "$test_script" ] || continue
    run_integration_test "$test_script"
done

if [ "$fail" -ne 0 ]; then
    echo "TESTS FAILED"
    exit 1
fi

echo "All tests passed."
