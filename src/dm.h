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

#ifndef DM_H
#define DM_H

#include <stddef.h>
#include <stdint.h>

/* Default cipher when the config doesn't specify one. */
#define DM_DEFAULT_CIPHER "aes-xts-plain64"

/* Set up a dm-crypt mapping by issuing the create / load / suspend ioctls
   directly to /dev/mapper/control. The mapping appears as /dev/dm-<index>
   (and /dev/mapper/<name> if udev is around).

     name        - mapping name, e.g. "rootfs" or "data"
     src_path    - source block device (the encrypted partition)
     cipher      - dm-crypt cipher string (NULL → DM_DEFAULT_CIPHER)
     secret_hex  - hex-encoded key, validated by key_resolve()
     iv_offset   - dm-crypt IV offset: added to each sector number before
                   the IV is derived. Typically: 0

   Fatals on any kernel-side failure. */
void dm_setup_crypt(const char *name, const char *src_path,
                    const char *cipher, const char *secret_hex,
                    int iv_offset);

/* Set up a dm-verity mapping. The hash tree is co-located on the data
   device, appended after the filesystem, so that when `device` is a
   dm-crypt device the hash tree is encrypted too.

   The dm-verity *target* takes all of its parameters — hash algorithm,
   salt, data/hash block sizes, data-block count — on the table line; the
   kernel does not read the on-disk verity superblock. We do: init reads
   the superblock that `veritysetup format` writes at `hash_offset` on
   `device` and recovers those parameters from it. The one thing the
   superblock omits is the root hash, which is the trust anchor and arrives
   separately via `root_hash` (from the signed kernel cmdline).

     name        - mapping name, e.g. "vroot"
     device      - block device holding both the verified data and, at
                   hash_offset, the hash tree + superblock
     hash_offset - byte offset of the verity superblock on `device`
     root_hash   - hex-encoded expected root hash (verity_roothash=)

   The resulting device is read-only. Fatals on any failure, including a
   missing or malformed superblock. */
void dm_setup_verity(const char *name, const char *device,
                     uint64_t hash_offset, const char *root_hash);

/* Compose the device path "/dev/dm-N" into `out`. */
void dm_device_path(int dm_index, char *out, size_t out_size);

#endif
