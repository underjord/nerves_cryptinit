/*
 * Copyright 2026 Frank Hunleth
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef FS_H
#define FS_H

#include <stddef.h>
#include <stdint.h>

/* The verity hash tree is appended right after the read-only rootfs image at
   a 4096-byte-aligned offset (see tests/qemu/assemble.sh, which is what the
   real images are built with). That alignment is the contract between the
   image builder and this init. */
#define VERITY_HASH_ALIGNMENT 4096

/* Probe the read-only rootfs image at the start of `device` and return the
   byte offset where the verity hash area begins — i.e. the filesystem's
   on-disk size rounded up to VERITY_HASH_ALIGNMENT. This replaces the old
   verity_hash_offset config knob: the size is recoverable from the
   filesystem superblock, so it never needs to be written down.

   Three read-only filesystems are recognized, by superblock magic:
     - squashfs  (magic at offset 0)
     - EROFS     (magic at offset 1024)
     - ext2/3/4  (magic at offset 1024+56)

   `device` is whatever sits one layer below verity: the raw rootfs
   partition, or the dm-crypt device when encryption is enabled.

   Returns 0 and writes the offset to *offset_out on success, -1 if the
   device can't be read or carries no recognized filesystem. */
int fs_verity_hash_offset(const char *device, uint64_t *offset_out);

/* Compute the verity hash offset for a filesystem of `fs_size` bytes:
   fs_size rounded up to VERITY_HASH_ALIGNMENT. Exposed for unit tests; the
   probe above uses it internally. */
uint64_t fs_hash_offset_for_size(uint64_t fs_size);

#endif
