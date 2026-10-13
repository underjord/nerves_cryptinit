#!/bin/bash
# Host-side test runner. Invokes the Docker container to assemble per-test
# images, then runs QEMU on the host (so we get native aarch64 + HVF
# acceleration on Apple Silicon).
#
# Usage: run-test.sh <scenario>
#
# Each scenario directory is expected to contain:
#   cmdline           - kernel command line (single line). Do NOT put
#                       verity_roothash= here; the harness computes it and
#                       appends it (see below).
#   expected          - one or more substrings that must appear in serial
#   initramfs.conf    - (optional) goes into /etc/initramfs.conf. There is
#                       no verity hash offset to set; init derives it from
#                       the filesystem size.
#   key.bin           - (optional) raw 32 or 64 byte key; presence triggers
#                       offline encryption of the rootfs
#   rootfs/           - (optional) directory tree merged into the rootfs
#                       (typically just /sbin/init)
#
# dm-verity is mandatory: assemble.sh formats every rootfs with a hash tree
# and emits build/test-<scenario>/roothash, which we append to the cmdline.

set -euo pipefail

cd "$(dirname "$0")"

SCENARIO=${1:?scenario name required}
[ -d "$SCENARIO" ] || { echo "no such scenario: $SCENARIO" >&2; exit 1; }

REPO=$(cd ../.. && pwd)
OUTPUT="$(pwd)/build/output"
TEST_BUILD="$(pwd)/build/test-$SCENARIO"

if [ ! -f "$OUTPUT/Image" ] || [ ! -f "$OUTPUT/init" ]; then
    echo "Build artifacts missing. Run 'make qemu-build' first." >&2
    exit 1
fi

echo "==> Assembling images for $SCENARIO..."
docker run --rm \
    -e HOST_UID="$(id -u)" \
    -e HOST_GID="$(id -g)" \
    -v "$REPO":/work \
    -v cryptinit-build:/build \
    nerves-cryptinit-builder \
    /work/tests/qemu/assemble.sh "$SCENARIO"

echo "==> Booting QEMU..."
CMDLINE=$(tr -d '\n' < "$SCENARIO/cmdline")

# dm-verity is mandatory and the root hash is the signed trust anchor, so it
# rides the kernel cmdline. assemble.sh just computed it (it changes with the
# rootfs), so append it here rather than hardcoding it in the scenario.
[ -f "$TEST_BUILD/roothash" ] || { echo "missing $TEST_BUILD/roothash (assemble failed?)" >&2; exit 1; }
CMDLINE="$CMDLINE verity_roothash=$(cat "$TEST_BUILD/roothash")"

SERIAL=$TEST_BUILD/serial.log

# Pick the fastest accelerator available. On Apple Silicon, HVF boots in a
# couple of seconds; without it QEMU falls back to TCG emulation, which is
# slow enough that the boot can blow past a tight timeout (and leave an
# empty serial log when killed mid-boot). HVF needs -cpu host; TCG uses a
# concrete CPU model.
ACCEL=(-accel tcg -cpu cortex-a72)
TIMEOUT=180
if qemu-system-aarch64 -accel help 2>/dev/null | grep -qw hvf \
   && [ "$(uname -s)" = Darwin ] \
   && [ "$(sysctl -n kern.hv_support 2>/dev/null)" = 1 ]; then
    ACCEL=(-accel hvf -cpu host)
    TIMEOUT=60
fi
echo "    (accelerator: ${ACCEL[*]}, timeout ${TIMEOUT}s)"

# Route the guest serial console straight to a file and detach stdin/monitor
# instead of using -nographic. -nographic multiplexes the serial console and
# the QEMU monitor onto stdio; when launched from an interactive terminal
# (stdin is a TTY) while stdout is redirected to a file, QEMU tries to drive
# that TTY and can stall — booting fine from a script but hanging for a real
# user, with nothing reaching the log. Explicit -serial file: + </dev/null +
# -monitor none makes capture deterministic regardless of how it's launched.
# QEMU's own stdout/stderr (e.g. an HVF init error) goes to qemu.log so a
# failure is never a blank log.
#
# -no-reboot makes QEMU exit when the guest reboots (which is how our test
# /sbin/init signals "done"). The timeout guards against a hang.
QEMU_LOG=$TEST_BUILD/qemu.log
set +e
timeout "$TIMEOUT" qemu-system-aarch64 \
    -machine virt \
    "${ACCEL[@]}" \
    -m 256M \
    -display none \
    -serial "file:$SERIAL" \
    -monitor none \
    -no-reboot \
    -kernel "$OUTPUT/Image" \
    -initrd "$TEST_BUILD/initramfs.cpio.gz" \
    -drive file="$TEST_BUILD/rootfs.img",if=virtio,format=raw \
    -append "$CMDLINE" </dev/null >"$QEMU_LOG" 2>&1
QEMU_EXIT=$?
set -e

# Check expected — every line of $expected must appear somewhere in serial.
EXPECTED=$SCENARIO/expected
fail=0
while IFS= read -r line; do
    [ -z "$line" ] && continue
    case "$line" in '#'*) continue ;; esac
    if ! grep -qF -- "$line" "$SERIAL"; then
        echo "  missing: $line"
        fail=1
    fi
done < "$EXPECTED"

if [ "$fail" -eq 0 ]; then
    echo "PASS: $SCENARIO"
    exit 0
else
    echo "FAIL: $SCENARIO (qemu exit $QEMU_EXIT)"
    if [ "$QEMU_EXIT" -eq 124 ]; then
        echo "  (exit 124 = timed out after ${TIMEOUT}s; accelerator was ${ACCEL[*]})"
    fi
    echo "--- last 40 lines of guest serial ($SERIAL) ---"
    tail -40 "$SERIAL" 2>/dev/null || true
    echo "--- last 20 lines of qemu stdout/stderr ($QEMU_LOG) ---"
    tail -20 "$QEMU_LOG" 2>/dev/null || true
    exit 1
fi
