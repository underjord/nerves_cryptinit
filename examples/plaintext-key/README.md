# plaintext-key

The smallest viable encrypted rootfs config: a hex key literal in
`/etc/initramfs.conf`. No fork+exec, no derivation, no binding to the
device. Every device built from the same image has the same key.

## When this is the right choice

- Development. You're still bringing up the build chain and don't want
  to fight a key helper before you can boot once.
- A fleet where you accept that an attacker who pulls the key off one
  device gets it for every device. (Genuinely fine for some threat
  models — if all your security comes from physical access controls
  and signed boot, the disk encryption is just defense-in-depth.)

If you want per-device keys, see `../pi5-otp/`.

## Files

- `initramfs.conf` — drop into the CPIO at `/etc/initramfs.conf`.
- *(no helpers)*

## How to use

1. Generate a 32-byte random key as 64 hex chars:
   ```
   openssl rand -hex 32
   ```
2. Paste it into `initramfs.conf` after `rootfs_key = hex:`.
3. Encrypt your rootfs offline with the same key (e.g.
   `cryptsetup --key-file=...` or any tool that produces
   plain-aes-xts-encrypted bytes).
4. Build your firmware so the encrypted partition lands at whatever
   `root=` the kernel cmdline points at, and append a CPIO that puts
   `initramfs.conf` at `/etc/initramfs.conf`.
