# verity-rootfs

An integrity-verified, read-only rootfs via dm-verity. dm-verity is
**mandatory** in nerves_cryptinit — every boot verifies the rootfs — so
this is also the baseline config: the no-encryption case, just verity.
The encryption examples (`../plaintext-key/`, `../pi5-otp/`,
`../bootstrap/`) all layer dm-crypt *underneath* this.

dm-verity builds a Merkle (hash) tree over the rootfs at build time and
records the tree's single root hash. At runtime the kernel re-checks each
block against the tree on read, and the tree against the root hash. Change
one byte of the rootfs — or the tree — and the read faults.

## What it does and doesn't do

- It guarantees the rootfs hasn't been modified, on disk or in flight.
  Squashfs + verity is the classic embedded combination, and it's the
  keystone of a measured/secure boot chain: the bootloader verifies the
  signed kernel+cmdline, the signed cmdline carries the verity root hash,
  and that transitively verifies the entire rootfs. The chain is only as
  good as the signature on the cmdline (see below).
- It does **not** keep the rootfs secret. For that, add dm-crypt; the two
  compose — see "Combining with encryption" below.
- The rootfs is read-only by construction. If you need a writable rootfs,
  verity is the wrong tool.

## The one thing that matters: sign the root hash

The root hash is the entire trust anchor. nerves_cryptinit reads it from
`verity_roothash=` on the kernel cmdline and reads everything else
(algorithm, salt, block sizes, block count) from the on-disk verity
superblock.

That split is deliberate: the superblock is just mechanical parameters and
lives next to the data, but the root hash must come from somewhere an
attacker can't rewrite. **If the cmdline isn't signed, verity is
theater** — an attacker who can modify the rootfs can recompute the tree,
read off the new root hash, and edit the cmdline to match. Put the root
hash on a cmdline that your bootloader's secure boot covers (on Pi, that's
the signed `boot.img`).

## Files

- `initramfs.conf` — an empty stub. A verity-only boot needs **no** config
  file at all (init derives everything it needs); it's included only for
  illustration. Drop it into the CPIO at `/etc/initramfs.conf` or omit it.
- *(no helpers — the work is an offline `veritysetup format` step)*

## How to use

1. Build your read-only rootfs image (e.g. a squashfs), call it
   `rootfs.img`.

2. Format it with veritysetup. This writes the hash tree + superblock and
   prints the root hash. To append the tree to the rootfs partition itself
   (single-partition layout), put the hash area at an offset past the
   filesystem with `--hash-offset`, and bound the data area to end exactly
   there with `--data-blocks` (otherwise the data size is ambiguous —
   veritysetup would count the region the tree occupies as data):

   ```
   # 48 MiB filesystem, default 4096-byte blocks:
   #   --data-blocks 12288 * 4096 = 50331648 = --hash-offset
   veritysetup format --data-blocks=12288 --hash-offset=50331648 \
       rootfs.img rootfs.img
   #   ...
   #   Root hash:  9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08
   ```

   The offset must be 4096-aligned and at least the size of the filesystem.
   You don't write it down anywhere: init derives it at boot from the
   filesystem size (it reads the squashfs/EROFS/ext4 superblock and rounds
   the size up to 4096), so the build just has to place the hash tree at
   exactly that boundary — `--hash-offset = round_up(fs_size, 4096)`, which
   is the natural choice anyway. The hash tree is always co-located on the
   rootfs device this way — there is no separate-hash-device option, because
   a separate hash partition couldn't be covered by dm-crypt (see "Combining
   with encryption").

   veritysetup picks a random salt and sha256 by default; both are stored
   in the superblock, so init recovers them automatically. There is
   nothing to copy out but the root hash.

3. Put the printed root hash on the **signed** kernel cmdline:

   ```
   root=/dev/mmcblk0p2 rootfstype=squashfs ro init=/sbin/init \
       verity_roothash=9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08
   ```

   Keep `ro` — the verity device is read-only and `init=` must point at a
   real init inside the verified rootfs.

4. Build firmware so the formatted image lands at `root=`, and append a
   CPIO that puts this `initramfs.conf` at `/etc/initramfs.conf`.

## Combining with encryption

Verity is always on; encryption is the optional layer underneath it. Add a
`rootfs_key` (from one of the encryption examples) and init decrypts first,
then verifies the plaintext:

```
flash (ciphertext) → dm-crypt /dev/dm-0 → dm-verity /dev/dm-1 → squashfs
```

Because verity sits *above* crypt, the verity superblock and hash tree —
co-located on the rootfs device — are inside the encrypted region, so they
are encrypted too. Run `veritysetup format` over the *decrypted* image; init
likewise derives the hash offset from the decrypted device (it probes
`mount_source` after the crypt layer). You get both confidentiality and
integrity, at the cost of a writable-but-verified rootfs being impossible —
verity is read-only.
