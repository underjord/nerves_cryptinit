# bootstrap

A first-boot conversion helper that transforms a factory-flashed,
plaintext rootfs image into a per-device-encrypted rootfs **without**
needing a separate bootstrap initramfs.

The pattern works because nerves_cryptinit's `cmd:` key source isn't
just "derive a key" — it's "do whatever you need to do before mount,
then print a key on stdout." That's general enough to cover the
bootstrap step entirely in userspace.

## The shape

A single helper at `/usr/sbin/bootstrap-or-derive` runs every boot.
On its first invocation per device, it does the conversion; on every
subsequent boot, it just rederives the key and exits.

```
                    +---------+
                    | started |
                    +---------+
                         |
                first-boot detection
                  (e.g. OTP empty?
                   marker file?
                   first 128 KB of
                   data partition all 0xff?)
                         |
              +----------+----------+
              | yes                 | no
              v                     v
       generate random        read the key from
        per-device key         OTP / wherever
              |                     |
              v                     |
       store the key in             |
        OTP (one-way)               |
              |                     |
              v                     |
       set up dm-crypt with         |
        the new key on the          |
        destination partition       |
        (e.g. mmcblk0p2)            |
              |                     |
              v                     |
       copy bytes from source       |
        partition (mmcblk0p3,       |
        plaintext) to dest          |
        (encrypted on write)        |
              |                     |
              v                     |
       verify dest: dm-crypt,       |
        then dm-verity, read        |
        it through                  |
              |                     |
              v                     |
       wipe source so we            |
        never re-trigger            |
              |                     |
              +----------+----------+
                         |
                         v
                 print key as hex
                 on stdout, exit 0
```

## Why one helper instead of a separate bootstrap image

The two-image approach is what most projects do: factory image flashes
device, runs once, hands off to "real" image. It works but requires
maintaining two firmware images, two CI pipelines, two release
procedures.

By moving the conversion into the `cmd:` helper, you ship one image and
one binary. The helper has all the complexity (and yes, it is the
trickiest piece of any encryption-at-rest system because of the
mid-write power-loss case), but at least the trickiness is contained
in one userspace program where you can:

- Unit-test it with fake block devices on a host.
- Log every step to pmsg so a partial bootstrap is debuggable on the
  next boot.
- Persist progress markers between attempts to make recovery from
  power loss safe.

## What's not in this directory

A reference implementation. The detection mechanism, OTP API, source
and destination partition layout, and key derivation are all
platform-specific — there's no portable implementation that would be
useful as anything beyond a sketch.

What we can give you is the contract:

- **Input**: nothing. Helper figures out state from the device.
- **Output (stdout)**: 64 hex characters (256-bit key), no leading or
  trailing whitespace required (cryptinit trims).
- **Exit code**: 0 on success. Non-zero is fatal — cryptinit will
  panic, which is correct behaviour because the rootfs can't be
  unlocked without the key.
- **Side effects (first boot only)**: write key to OTP, encrypt
  destination partition, wipe source partition.
- **Side effects (every boot)**: must be idempotent — second run
  should detect "already provisioned" and skip the conversion path
  entirely.
- **dm-crypt parameters**: the destination mapping the helper writes
  through has to match the one cryptinit sets up on the next boot, or
  the rootfs decrypts to garbage with no error anywhere. Cipher equal
  to `rootfs_cipher` (default `aes-xts-plain64`), IV offset 0, device
  start offset 0, mapping spanning the whole partition.
- **Device waiting**: cryptinit waits only for `root=`. The helper
  waits for its own source and destination devices.
- **Reserved mapping names**: `rootfs`, `vroot` and `data` belong to
  cryptinit. Reusing one makes its DM_DEV_CREATE fail with EBUSY,
  which panics the boot.
- **Cleanup**: remove every dm device the helper created before it
  exits. cryptinit picks `/dev/dm-N` by counting the mappings it makes
  itself, so a leftover mapping shifts the index and it verifies the
  wrong device.

## How to use this contract

In your build:

1. Write a `bootstrap-or-derive` program for your platform.
2. Bundle it into the initramfs CPIO at `/usr/sbin/bootstrap-or-derive`.
3. Use this `initramfs.conf`:

   ```
   rootfs_key = cmd:/usr/sbin/bootstrap-or-derive
   ```

4. Lay out partitions so that, on a fresh device, the source
   partition holds the unencrypted factory rootfs and the destination
   partition is empty.

The first boot takes longer than subsequent boots (one full read+write
of the rootfs through dm-crypt). Plan your factory test for that.

## Mid-write power loss

The single hardest part. Strategies that work:

- Make the source partition the trigger. While the source still has
  the unencrypted image, bootstrap is "in progress." Only wipe the
  source once the destination verifies: map it with dm-crypt using the
  new key, map dm-verity on top of that mapping at the hash offset,
  then read the whole device through. A clean read against the known
  root hash proves the copy is byte-perfect. "It mounted" does not.
- Use a marker in OTP or a separate small partition to record state
  transitions. State machine: { not started, key generated, copying,
  destination verified, source wiped, done }.
- Idempotent restart: any step can be re-run without harm. A power
  loss in the middle of "copying" is recovered by retrying the copy
  from offset 0.
