# pi5-otp

A Raspberry Pi 5 (or Pi 4) key helper that derives the dm-crypt key from
the device's customer-programmable OTP rows, read via the VideoCore
mailbox interface (`/dev/vcio`).

## What this gets you

Combined with signed boot (Pi's `SIGNED_BOOT=1` + customer key fused to
OTP), you have:

- **Tamper-resistance.** An attacker swapping the kernel/initramfs is
  blocked by signed boot. They can't read the OTP without their
  replacement firmware also being signed.
- **Disk-clone resistance.** Each device's OTP is unique (programmed
  at provisioning), so a byte-for-byte SD-card clone to a different Pi
  produces the wrong key and dm-crypt fails to decrypt.

## What this does NOT get you

- Protection against an attacker decapping the SoC and reading the
  fuses. Pi has no TEE; if your threat model includes that, you need
  different hardware.
- Anything if you forget to fuse the customer key hash. With
  `SIGNED_BOOT=1` but the key only in EEPROM (not OTP), the bootloader
  enforces signed firmware but a sufficiently determined attacker can
  reflash the EEPROM. Production devices want both.

See the project README's "convincing Pi authenticated-boot story" notes
for the iteration loop that lets you develop without burning fuses.

## Files

- `unlock.c` — the helper. ~80 lines, no dependencies beyond libc.
- `Makefile` — builds `unlock` linked statically against musl (or
  whatever toolchain you give it).
- `initramfs.conf` — config snippet for the CPIO.

## Provisioning

Per device, once, in the factory jig:

1. Generate a random 256-bit secret.
2. Program rows 0..7 of customer OTP via `vcmailbox` (or
   `rpi-sb-provisioner`'s API). Lock.
3. Encrypt the rootfs offline with the same secret (concatenated as
   hex, the way `unlock` will produce it).
4. Optionally also fuse the signing-key hash so secure boot is
   permanent.

The dev loop in the project README's Pi notes lets you skip steps 2 and
4 and use a file-based stand-in helper. Steps 1 and 3 are still needed
either way.

## How to drop this into your build

1. `make` here produces `unlock`.
2. Append a CPIO containing `/usr/sbin/unlock` (the binary) and
   `/etc/initramfs.conf` (this directory's copy) to your boot image.
3. Sign `boot.img` with your customer key and flash.

## Adapting for production

The helper as written concatenates the 8 OTP rows directly into 64 hex
chars and prints them. That's fine for one partition. For a real
deployment with separate rootfs and data keys, wrap the OTP read in
HKDF and key per partition:

```
key_for_purpose = HKDF(otp_bytes, salt = "", info = "cryptinit/" || purpose)
```

Then pass the purpose as an arg in your `cmd:` line:

```
rootfs_key = cmd:/usr/sbin/unlock rootfs
data_key   = cmd:/usr/sbin/unlock data
```

Adding HKDF is ~50 LOC of hand-rolled SHA-256 + HKDF, or you can link
against a small library like `libhydrogen` or `monocypher`.
