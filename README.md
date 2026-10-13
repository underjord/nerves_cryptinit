# nerves_cryptinit

A tiny Linux initramfs that integrity-verifies your rootfs with dm-verity —
optionally decrypting it first with dm-crypt — then pivots to the real init.

**Status: early — design in progress, expect breakage.**

## When to use this

This initramfs always verifies the rootfs (dm-verity is mandatory). On top of
that it optionally gives you:

- An encrypted rootfs (dm-crypt), decrypted below the verity layer so the hash
  tree is encrypted too
- An encrypted data partition for user state

If you don't want an integrity-verified rootfs, you don't need this — let the
kernel mount `root=` and exec `init=` directly.

## What it deliberately doesn't do

- Boot splashes (use `erlinit`'s splash support instead — splash files belong in
  the rootfs, not the initramfs)
- A/B slot selection (use the bootloader; on Pi this is `tryboot`)
- Factory bootstrap or first-boot conversion (write a `cmd:` key helper that
  does this in userspace — see `examples/bootstrap/`)
- LUKS, LVM, MD-RAID, network boot, kernel module loading, recovery shells
- Device-specific key derivation, board detection, vendor workarounds

The init binary itself never grows knobs for those. Plug them in via the
`cmd:` key-source helper, which is just a userspace program that prints a hex
key on stdout.

## Configuration

Two inputs:

1. **Kernel command line** (set by the bootloader, signed if you're using
   secure boot) — supplies `root=`, `rootfstype=`, `rootflags=`, `ro`/`rw`,
   `init=`, and `verity_roothash=`. The root hash *must* live here, on the
   signed cmdline: it's the trust anchor, and a root hash an attacker can edit
   buys you nothing.
2. **`/etc/initramfs.conf`** inside the CPIO — supplies the things the
   bootloader doesn't know about: encryption keys and data partition setup.
   This file is optional: a plain verity-only boot needs no config at all.

Verity is always on, so `verity_roothash=` on the cmdline is required; init
refuses to boot without it. The verity hash tree's offset is *derived* from
the rootfs filesystem size (squashfs, EROFS, or read-only ext4) — the hash
tree is appended right after the filesystem at a 4096-byte boundary, so init
reads the size out of the filesystem superblock rather than taking it from
config. Everything else is by presence: setting `rootfs_key` enables rootfs
encryption, setting `data_device` sets up a data partition mapping after
switch_root.

See `examples/` for ready-to-run configurations.

## Quick start

(Coming soon — first the binary needs to exist.)

## Logging

Init writes every log line to three places:

- `stderr` (so it shows up on the kernel console if one's wired up)
- `/dev/kmsg` (so it interleaves with kernel log output)
- `/dev/pmsg0` if present, which survives reboots and panics and can be
  read back on the next boot from `/sys/fs/pstore/pmsg-ramoops-0`. This
  is the right way to debug "the device just doesn't come up" failures
  in the field.

To enable pmsg, your kernel needs `CONFIG_PSTORE_PMSG` and
`CONFIG_PSTORE_RAM` plus a `reserved-memory` ramoops region in your
device tree. Roughly:

```
reserved-memory {
    #address-cells = <2>;
    #size-cells = <2>;
    ranges;
    ramoops@8f000000 {
        compatible = "ramoops";
        reg = <0x0 0x8f000000 0x0 0x100000>;
        record-size = <0x1000>;
        pmsg-size = <0x8000>;
    };
};
```

Without ramoops, init falls back to stderr + kmsg only, which is fine
for local debugging.

## Building

```
make           # build init + run host tests
make build     # build only
make clean
```

The init binary is plain static-musl C; no Buildroot tree, no toolchain
download, no out-of-tree build setup. Cross-compile by setting `CC` to your
toolchain's gcc.

## License

Apache-2.0, copyright Frank Hunleth. The vendored kernel uapi header
`src/compat/linux/dm-ioctl.h` is LGPL-2.0-or-later WITH Linux-syscall-note.
License texts are in `LICENSES/`; the repository is [REUSE](https://reuse.software)
compliant, so `reuse lint` is the authority on per-file provenance.
