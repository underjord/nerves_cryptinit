# nerves_cryptinit examples

dm-verity is mandatory, so **verity-rootfs/** is the baseline every device
needs — start there. The other three add dm-crypt *underneath* verity for
secrecy, in increasing order of complexity; each of their configs also
carries the required verity settings.

- **plaintext-key/** — the simplest possible encrypted setup: a hex key
  literal in `/etc/initramfs.conf`. No helpers, no derivation, no device
  binding. Useful for development, useless for production. Ten lines of
  config.

- **pi5-otp/** — a Pi 5 helper that derives a key from the device's
  customer OTP rows, read via the VideoCore mailbox. This is the
  recommended production shape: combine it with signed boot.img and
  fused customer OTP and you get a per-device key that survives a
  disk clone attack.

- **bootstrap/** — a first-boot conversion helper. Detects an
  unprovisioned device, generates a key, encrypts the source rootfs
  partition into a destination partition, persists the key to OTP, and
  emits it for `cryptinit` to use. Documents the pattern; the actual
  implementation is platform-specific.

- **verity-rootfs/** — the mandatory baseline: an integrity-verified,
  read-only rootfs via dm-verity, with no encryption. Doesn't keep the
  rootfs secret; makes any tampering with it fail to boot. The root hash
  rides the signed kernel cmdline (the trust anchor); the
  algorithm/salt/block sizes come from the on-disk verity superblock. No
  runtime helper — just an offline `veritysetup format` step. The
  encryption configs above are this plus a `rootfs_key`.

Each example is self-contained: the `initramfs.conf` you'd append to
your CPIO, plus any helper sources, plus a README explaining the
assumptions and what to change.
