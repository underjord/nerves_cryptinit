# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

`nerves_cryptinit` is a tiny static-musl C initramfs `init`. It runs as PID 1, optionally sets up dm-crypt (and eventually dm-verity) for the rootfs and a data partition, then `switch_root`s and `execv`s the real init. With no `/etc/initramfs.conf` it just mounts `root=` and execs `init=` — encryption is opt-in by config presence.

The binary is deliberately small and knob-free. Anything board-specific (key derivation, OTP, first-boot conversion) is pushed out to userspace `cmd:` key helpers, never added to the init itself. Keep that boundary when extending.

## Commands

```
make            # build init + run host tests (default target is `build` only)
make build      # build src/init only
make check      # build, then run host test suite (tests/run_tests.sh)
make clean
make qemu-check # build kernel/rootfs in Docker + run QEMU scenarios (slow; see below)
```

Run a single host test layer or scenario:
```
make -C tests/unit            # build the unit-test binaries
tests/unit/test_key           # run one unit-test binary directly
make -C tests/qemu 01-plaintext   # run a single QEMU scenario
```

`make check` runs three things in order: unit tests (`tests/unit/test_*`), then integration tests (`tests/0NN_*` driven through the LD_PRELOAD fixture), then reports pass/fail. The init binary must be built first (`make build`) or the integration tests bail.

Cross-compile by setting `CC` (e.g. `make -C src CC=aarch64-linux-musl-gcc`). `DEBUG=1` enables the `debug()` log macro.

## Architecture

`main()` in `src/main.c` is the whole control flow, in order: mount /dev /sys /proc → parse `/proc/cmdline` → parse `/etc/initramfs.conf` → wait for `root` device → optionally dm-crypt the rootfs → (mandatory) dm-verity the rootfs → mount on `/mnt` → `switch_root()` → optionally dm-crypt the data partition (after the pivot, so its helper can live in the real rootfs) → `execv` the next init. Any unrecoverable error calls `fatal()`, which logs and exits 1 (the kernel then panics).

Each `src/*.c` is one concern with a matching header:
- **cmdline.c** — parses the bootloader-supplied kernel cmdline (`root=`, `rootfstype=`, `rootflags=`, `init=`, `verity_roothash=`, `ro`/`rw`). Defaults to read-only.
- **config.c** — parses `/etc/initramfs.conf` (`key = value` lines). A missing file is *not* an error: a config-less boot is valid (verity-only, no encryption), since the verity hash offset is derived from the filesystem rather than configured. Setting `rootfs_key` enables rootfs encryption; setting `data_device` enables the data partition.
- **fs.c** — `fs_verity_hash_offset()` probes the rootfs superblock (squashfs/EROFS/read-only ext4) on whatever layer sits below verity and returns the verity hash offset: the filesystem's on-disk size rounded up to 4096. This replaces the old `verity_hash_offset` config knob — the hash tree is always appended right after the filesystem at a 4096 boundary, so the offset is recoverable from the filesystem size.
- **key.c** — `key_resolve()` turns a key spec into a validated hex string. Three forms: `hex:DEADBEEF`, `file:/path`, `cmd:/path args` (fork+exec, read hex from stdout). This is the extension point for board-specific key sources.
- **dm.c** — `dm_setup_crypt()` and `dm_setup_verity()` talk the dm-ioctl ABI directly to `/dev/mapper/control` (CREATE → TABLE_LOAD → SUSPEND/resume). No cryptsetup/veritysetup dependency. Default crypt cipher `aes-xts-plain64`. Verity reads the on-disk `veritysetup` superblock itself (struct `verity_sb`) to recover algorithm/salt/block-sizes/data-block-count, since the kernel target needs them all on the table line; the root hash comes separately from the cmdline.
- **log.c** — `info()`/`fatal()` write each line to stderr, `/dev/kmsg`, and `/dev/pmsg0` (persistent ramoops ring, survives reboot/panic; absence is normal). Macros like `OK_OR_FATAL`/`OK_OR_WARN`/`ERR_RETURN` live in `log.h`.
- **util.c** — hex/byte conversion and string trimming.

Two config inputs, split by trust boundary: the **kernel cmdline** carries what the (signed) bootloader knows (`root=`, `init=`, verity root hash); **`/etc/initramfs.conf`** inside the CPIO carries what it doesn't (keys, data partition). The config is optional — a verity-only boot needs no config file.

**dm-verity is mandatory** — `main.c` fatals without `verity_roothash=` on the cmdline. Encryption is the optional layer *underneath*: the device stack is `root=` (flash) → optional `/dev/dm-0` (crypt) → `/dev/dm-1` (verity) → squashfs mount. `mount_source` threads through each layer and `main.c` tracks the index with `dm_devices`. Because verity sits above crypt, the verity hash tree — always co-located on the rootfs device right after the filesystem, never a separate partition — is encrypted when crypt is enabled. That co-location is why there's no `verity_hash_device` config knob, and why the hash offset can be *derived* (`fs.c`) from the filesystem size rather than configured: `main.c` probes `mount_source` (post-crypt, so it reads the decrypted view) and rounds the filesystem size up to 4096.

## Host builds and the compat shim

The code targets Linux but compiles and tests on macOS via `src/compat/compat.h` (auto-included when `uname` is Darwin). It defines Linux `MS_*` mount-flag constants and remaps Linux's 5-arg `mount()` onto BSD's 4-arg form so the test fixture sees consistent values. None of `compat/` ships to the target.

## Testing layers

1. **Unit tests** (`tests/unit/`) — link individual `src/*.c` files and assert on parsing/resolution logic. Fast.
2. **LD_PRELOAD integration tests** (`tests/0NN_*` + `tests/fixture/init_fixture.c`) — run the *real* init against a fake root in `$WORK/`. The fixture (`init_fixture.so`, also `DYLD_INSERT_LIBRARIES` on macOS) intercepts privileged syscalls (`mount`, `chroot`, dm ioctls, `getpid`→1, etc.) and prints a transcript; the test diffs it against the scenario's `EXPECTED` block. These verify *that the right syscalls happen in the right order*, not that a real kernel accepts them. To add one, copy a `tests/0NN_*` script: it writes `$CMDLINE_FILE`, optionally `$CONFIG`, stages files under `$WORK_DIR`, and writes `$EXPECTED`. Since verity is mandatory, every scenario needs `verity_roothash=` on its cmdline plus two things staged on the device init reads from: a squashfs superblock so init can derive the hash offset (`stage_squashfs_header`), and a real verity superblock at that offset (`stage_verity_device`). For the crypt case, stage both on the `/dev/dm-N` file the crypt layer would produce, since `pread` reads through the fixture's `open` redirect.
3. **QEMU integration tests** (`tests/qemu/`) — real aarch64 kernel, real dm-crypt, real fork+exec. Needs Docker + `qemu-system-aarch64`; first build is ~30 min (Buildroot kernel/toolchain, cached in a Docker volume). NOT part of `make check` — run before shipping. A scenario is a dir with `cmdline`, `expected`, optional `initramfs.conf`/`key.bin`/`rootfs/`, registered in `SCENARIOS` in `tests/qemu/Makefile`. See `tests/qemu/README.md`.

When you change syscall order or log output in `main.c`, the `EXPECTED` transcripts in `tests/0NN_*` will need updating to match.

## Examples

`examples/` holds reference `initramfs.conf` + helper configurations, not built by the main build: `plaintext-key/` (dev-only hex literal), `pi5-otp/` (production shape — key derived from Pi OTP via VideoCore mailbox, a `cmd:` helper), `bootstrap/` (first-boot conversion pattern).
