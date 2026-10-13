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

#include "fs.h"
#include "log.h"

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

/* Enough to reach every superblock field we read: the ext4 high half of the
   block count lives at byte 1024+0x150 = 1360. One block covers it. */
#define FS_HEADER_LEN 4096

/* All on-disk fields below are little-endian. These readers are
   endian-agnostic (they assemble the value byte by byte) and bounds-checked
   by the caller, which knows how many header bytes it actually read. */
static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t le64(const uint8_t *p)
{
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}

/* squashfs: superblock at offset 0. Magic "hsqs" (0x73717368, LE), total
   archive size in the u64 at offset 40. */
static int squashfs_size(const uint8_t *h, size_t len, uint64_t *size_out)
{
    if (len < 48 || le32(h) != 0x73717368)
        return -1;
    *size_out = le64(h + 40);
    return 0;
}

/* EROFS: superblock at offset 1024. Magic 0xE0F5E1E2 (LE). Block size is
   1 << blkszbits (u8 at +12); total block count is the u32 at +36. */
static int erofs_size(const uint8_t *h, size_t len, uint64_t *size_out)
{
    const size_t sb = 1024;
    if (len < sb + 40 || le32(h + sb) != 0xE0F5E1E2)
        return -1;
    unsigned blkszbits = h[sb + 12];
    if (blkszbits == 0 || blkszbits > 16)
        return -1;
    *size_out = (uint64_t)le32(h + sb + 36) << blkszbits;
    return 0;
}

/* ext2/3/4: superblock at offset 1024. Magic 0xEF53 (u16) at +56. Block size
   is 1024 << s_log_block_size (u32 at +24); block count is s_blocks_count_lo
   (u32 at +4), extended by s_blocks_count_hi (u32 at +0x150) only when the
   64BIT incompat feature (s_feature_incompat & 0x80, at +0x60) is set. */
static int ext_size(const uint8_t *h, size_t len, uint64_t *size_out)
{
    const size_t sb = 1024;
    if (len < sb + 0x150 + 4 || le16(h + sb + 56) != 0xEF53)
        return -1;

    uint32_t log_block_size = le32(h + sb + 24);
    if (log_block_size > 6)   /* 1 KiB .. 64 KiB; anything larger is bogus */
        return -1;
    uint64_t block_size = (uint64_t)1024 << log_block_size;

    uint64_t blocks = le32(h + sb + 4);
    if (le32(h + sb + 0x60) & 0x80)   /* EXT4_FEATURE_INCOMPAT_64BIT */
        blocks |= (uint64_t)le32(h + sb + 0x150) << 32;

    *size_out = blocks * block_size;
    return 0;
}

uint64_t fs_hash_offset_for_size(uint64_t fs_size)
{
    uint64_t align = VERITY_HASH_ALIGNMENT;
    return (fs_size + align - 1) / align * align;
}

int fs_verity_hash_offset(const char *device, uint64_t *offset_out)
{
    int fd = open(device, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        ERR_RETURN("could not open %s to probe filesystem", device);

    uint8_t header[FS_HEADER_LEN];
    ssize_t got = pread(fd, header, sizeof(header), 0);
    close(fd);
    if (got < 0)
        ERR_RETURN("could not read filesystem header from %s", device);

    size_t len = (size_t)got;
    uint64_t fs_size;
    if (squashfs_size(header, len, &fs_size) == 0)
        debug("probed squashfs on %s: %llu bytes", device, (unsigned long long)fs_size);
    else if (erofs_size(header, len, &fs_size) == 0)
        debug("probed erofs on %s: %llu bytes", device, (unsigned long long)fs_size);
    else if (ext_size(header, len, &fs_size) == 0)
        debug("probed ext on %s: %llu bytes", device, (unsigned long long)fs_size);
    else
        ERR_RETURN("no squashfs/erofs/ext filesystem found on %s", device);

    if (fs_size == 0)
        ERR_RETURN("filesystem on %s reports zero size", device);

    *offset_out = fs_hash_offset_for_size(fs_size);
    return 0;
}
