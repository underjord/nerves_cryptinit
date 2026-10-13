# QEMU integration tests

Real kernel, real dm-crypt, real dm-verity, real fork+exec. Closes the
gap between the fast `LD_PRELOAD`-based tests in `tests/unit/` and
`tests/0NN_*` (which verify *that the right syscalls happen*) and "does
this actually unlock and verify the rootfs on a kernel" (which only QEMU
can answer).

These tests are **not part of the default `make check` loop** — they
need Docker and take 30+ minutes the first time. The fast suite is what
you should run while iterating; this is what you should run before
shipping.

## Requirements

- Docker (any version with build/run support — verified on Docker
  Desktop for Mac and on native Linux).
- A working `qemu-system-aarch64` on the host `PATH`:
  - macOS: `brew install qemu`.
  - Linux: install the distro package (`apt install qemu-system-arm`,
    `dnf install qemu-system-aarch64`, ...). If you use a hand-built or
    `~/.local` QEMU instead, make sure it actually runs — a binary linked
    against libraries your system no longer ships (e.g. an old
    `libfuse3.so.3` when the distro now has `libfuse3.so.4`) will fail at
    launch with `error while loading shared libraries`, and the harness
    will report a QEMU exit code with that message captured in
    `build/test-<scenario>/qemu.log`.

QEMU runs on the host, not in the container, so it must be installed on
the host even though everything else is containerized.

### A note on Linux file ownership

The container assembles per-test images as root, so on Linux the files it
writes to the bind-mounted repo (`tests/qemu/build/...`) land as
`root:root` on the host. The build/assemble steps `chown` them back to the
invoking user (via `HOST_UID`/`HOST_GID`) so the host-side runner can write
its logs and so `make clean`/`distclean` can remove them. macOS Docker
Desktop already remaps bind-mount ownership to the host user, so this is a
no-op there. If you have a `build/` tree left over from before this was in
place, it may still be root-owned — clear it once with
`sudo rm -rf build` (or `sudo chown -R $(id -u):$(id -g) build`).

The repo's main build is unaffected; everything QEMU-specific lives
under `tests/qemu/` and gets built inside a container.

## Usage

From the project root:

```
make qemu-check          # build (first time only) + run all scenarios
make qemu-build          # build only (kernel + cross cryptinit + helper)
```

From this directory:

```
make build               # equivalent to `make qemu-build` from root
make 01-plaintext        # run a single scenario
make scenarios           # run all scenarios against existing build
make clean               # drop per-scenario assembly dirs
make distclean           # also drop the cached Buildroot volume
```

## How it works

A single Docker image (`nerves-cryptinit-builder`, ~700 MB) carries the
build-time tools: a Buildroot prerequisites set, the cross-toolchain it
produces, plus `cpio`, `squashfs-tools`, and `fwup` (built from source —
see below) for offline rootfs encryption.

The first `make build` runs `tests/qemu/build.sh` inside the container
and produces, in `tests/qemu/build/output/`:

- `Image` — Linux kernel for `qemu-system-aarch64 -machine virt`,
  patched in via `kernel.fragment` to enable `dm-crypt`, `dm-verity`,
  squashfs, devtmpfs, and pmsg.
- `init` — the project's `src/init` cross-compiled statically with
  Buildroot's musl toolchain.
- `test_init` — a tiny static `/sbin/init` for the test rootfs that
  prints the scenario's marker and powers off (`lib/test_init.c`).

Offline rootfs encryption uses `fwup`'s `raw_write` with
`cipher=aes-xts-plain64`, which produces ciphertext byte-compatible with
what `dm-crypt` decrypts at runtime — so there's no bespoke crypto helper
to build, and no privileged Docker for `losetup` + `cryptsetup`. fwup is
built from source in the image (`Dockerfile`) because `aes-xts-plain64`
support only landed in fwup 1.16.0, newer than the package in
Buildroot/Debian, and because fwup ships no arm64 `.deb`. That cipher is
AES-256-XTS only, so an encrypted scenario's `key.bin` must be 64 bytes.

dm-verity needs no build-time helper of its own: `assemble.sh` formats
each image with `veritysetup` (from `cryptsetup-bin` in the image), which
is a pure-userspace operation — no loop/dm, so no privileged Docker.

Buildroot's working directory lives in a named Docker volume
(`cryptinit-build`) rather than a host bind mount — Docker Desktop on
macOS doesn't fully support `chmod` on bind-mounted files, which
Buildroot needs.

Per scenario, `assemble.sh` (run inside the container) produces:

- `rootfs.img` — squashfs (`test_init` as `/sbin/init` + the scenario's
  `test-marker`) with a dm-verity hash tree appended after the
  filesystem at a block-aligned offset, the whole thing encrypted offline
  with `fwup` (aes-xts-plain64) if the scenario provides a `key.bin`. So the
  on-disk stack is flash → optional dm-crypt → dm-verity → squashfs, and
  the hash tree is encrypted too when a key is present.
- `roothash` — the verity root hash that `veritysetup format` just
  printed.
- `initramfs.cpio.gz` — `init` plus the scenario's `/etc/initramfs.conf`
  if it has one (a verity-only scenario ships none).

Because verity is mandatory, the harness computes the values that change
with the rootfs rather than making scenarios hardcode them. The root hash
is appended to the kernel cmdline by `run-test.sh` as `verity_roothash=`
(the signed trust anchor, supplied at boot). The verity hash offset is no
longer written anywhere — the build places the hash tree at
`round_up(squashfs_size, 4096)` and init derives that same offset from the
filesystem size at boot.

QEMU runs on the host (native arm64 on Apple Silicon, no emulation
tax), captures serial output, and `run-test.sh` greps for each line of
the scenario's `expected` file in turn. Reaching the rootfs marker is
itself proof the full crypt+verity stack worked — a bad root hash or a
tampered image faults before `/sbin/init` ever runs.

## Adding a scenario

A scenario is a directory under `tests/qemu/` containing:

| File             | Required | Purpose                                            |
|------------------|----------|----------------------------------------------------|
| `cmdline`        | yes      | Kernel cmdline (single line). No `verity_roothash=` — it's appended. |
| `expected`       | yes      | Substrings that must appear in serial output       |
| `test-marker`    | yes      | Bytes `test_init` prints once the rootfs is mounted |
| `initramfs.conf` | no       | If present, lands verbatim at `/etc/initramfs.conf` |
| `key.bin`        | no       | 64 raw bytes (AES-256-XTS); presence triggers encryption |

Then add the scenario name to `SCENARIOS` in `Makefile`.

Don't set `verity_roothash=` yourself — the harness computes it from the
assembled rootfs and appends it to the cmdline (see "How it works"). There
is no verity hash offset to set: init derives it from the filesystem size.
A scenario without a `key.bin` exercises the verity-only stack; one with a
`key.bin` exercises crypt+verity.

## Why aarch64

It matches the Pi/Nerves target and runs natively on Apple Silicon (no
QEMU TCG translation tax). x86_64 would also work; we just picked one.

## What's intentionally not here

- **No Pi-specific QEMU scenario.** QEMU doesn't faithfully simulate Pi
  OTP, mailbox, or signed-boot, so the Pi flow stays a documentation
  artifact in `examples/pi5-otp/`. QEMU verifies generic correctness;
  Pi recipe verifies platform integration.
- **No bootstrap scenario yet.** That's the most valuable thing this
  harness will eventually run, since the LD_PRELOAD harness can't test
  it. Slated for a follow-up.
