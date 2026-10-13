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

#include "dm.h"
#include "log.h"
#include "util.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <linux/dm-ioctl.h>

/* The dm-ioctl ABI passes everything through one buffer: a fixed-size
   dm_ioctl header followed by per-call data. 16 KB is plenty for our
   uses (the table-load string is shorter than that even with a 256-byte
   key). */
#define DM_BUFFER_SIZE 16384

static void dm_init_header(struct dm_ioctl *dm, const char *name)
{
    memset(dm, 0, sizeof(*dm));
    dm->version[0] = DM_VERSION_MAJOR;
    dm->version[1] = DM_VERSION_MINOR;
    dm->version[2] = DM_VERSION_PATCHLEVEL;
    dm->data_size = DM_BUFFER_SIZE;
    dm->data_start = sizeof(*dm);
    snprintf(dm->name, sizeof(dm->name), "%s", name);
}

/* dm table lengths are always in 512-byte sectors, whatever the device's
   logical block size is. */
static off_t source_sector_count(const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        fatal("could not open %s", path);

    off_t size = lseek(fd, 0, SEEK_END);
    close(fd);
    if (size < 0)
        fatal("could not seek %s", path);

    return size / 512;
}

void dm_device_path(int dm_index, char *out, size_t out_size)
{
    snprintf(out, out_size, "/dev/dm-%d", dm_index);
}

/* The verity superblock that `veritysetup format` writes at the start of
   the hash area. 512 bytes, little-endian, NUL-padded strings. The kernel
   target ignores it; we parse it to build the table line. Layout matches
   cryptsetup's struct verity_sb. */
#define VERITY_SB_MAGIC "verity\0\0"
#define VERITY_SB_MAGIC_LEN 8

struct verity_sb {
    uint8_t  signature[8];
    uint32_t version;
    uint32_t hash_type;
    uint8_t  uuid[16];
    uint8_t  algorithm[32];
    uint32_t data_block_size;
    uint32_t hash_block_size;
    uint64_t data_blocks;
    uint16_t salt_size;
    uint8_t  pad1[6];
    uint8_t  salt[256];
    uint8_t  pad2[168];
} __attribute__((packed));

void dm_setup_verity(const char *name, const char *device,
                     uint64_t hash_offset, const char *root_hash)
{
    /* Read and validate the superblock off the device. */
    struct verity_sb sb;
    int hash_fd = open(device, O_RDONLY | O_CLOEXEC);
    if (hash_fd < 0)
        fatal("could not open verity device %s", device);

    ssize_t got = pread(hash_fd, &sb, sizeof(sb), (off_t)hash_offset);
    close(hash_fd);
    if (got != (ssize_t)sizeof(sb))
        fatal("could not read verity superblock at offset %llu on %s",
              (unsigned long long)hash_offset, device);

    if (memcmp(sb.signature, VERITY_SB_MAGIC, VERITY_SB_MAGIC_LEN) != 0)
        fatal("no verity superblock at offset %llu on %s",
              (unsigned long long)hash_offset, device);
    if (sb.version != 1)
        fatal("unsupported verity superblock version %u", sb.version);

    uint32_t data_block_size = sb.data_block_size;
    uint32_t hash_block_size = sb.hash_block_size;
    if (data_block_size == 0 || hash_block_size == 0)
        fatal("verity superblock has zero block size");

    /* Algorithm name is NUL-padded; make sure it terminates. */
    char algorithm[sizeof(sb.algorithm) + 1];
    memcpy(algorithm, sb.algorithm, sizeof(sb.algorithm));
    algorithm[sizeof(sb.algorithm)] = '\0';
    if (algorithm[0] == '\0')
        fatal("verity superblock has empty algorithm");

    /* Salt → hex, or "-" when there is none. */
    char salt_hex[2 * sizeof(sb.salt) + 1];
    if (sb.salt_size == 0) {
        salt_hex[0] = '-';
        salt_hex[1] = '\0';
    } else {
        if (sb.salt_size > sizeof(sb.salt))
            fatal("verity superblock salt_size %u too large", sb.salt_size);
        bytes_to_hex(sb.salt, salt_hex, sb.salt_size);
    }

    /* The hash tree starts one hash block past the superblock. */
    uint64_t hash_start_block = hash_offset / hash_block_size + 1;

    /* dm table lengths are always in 512-byte sectors. */
    uint64_t length_sectors = sb.data_blocks * (data_block_size / 512);

    int dm_control = open("/dev/mapper/control", O_RDWR | O_CLOEXEC);
    if (dm_control < 0)
        fatal("could not open /dev/mapper/control (CONFIG_DM_VERITY?)");

    uint8_t buffer[DM_BUFFER_SIZE];
    struct dm_ioctl *dm = (struct dm_ioctl *)buffer;

    /* 1. Create the device (read-only). */
    dm_init_header(dm, name);
    dm->flags = DM_READONLY_FLAG;
    snprintf(dm->uuid, sizeof(dm->uuid), "VERITY-%s", name);
    OK_OR_FATAL(ioctl(dm_control, DM_DEV_CREATE, buffer),
                "DM_DEV_CREATE for %s", name);

    /* 2. Load the dm-verity table. */
    dm_init_header(dm, name);
    dm->target_count = 1;
    dm->flags = DM_READONLY_FLAG;

    struct dm_target_spec *target = (struct dm_target_spec *)(buffer + dm->data_start);
    target->sector_start = 0;
    target->length = length_sectors;
    snprintf(target->target_type, sizeof(target->target_type), "verity");

    char *params = (char *)(target + 1);
    size_t params_max = DM_BUFFER_SIZE - dm->data_start - sizeof(struct dm_target_spec);
    int n = snprintf(params, params_max,
                     "1 %s %s %u %u %llu %llu %s %s %s",
                     device, device,
                     data_block_size, hash_block_size,
                     (unsigned long long)sb.data_blocks,
                     (unsigned long long)hash_start_block,
                     algorithm, root_hash, salt_hex);
    if (n < 0 || (size_t)n >= params_max)
        fatal("dm-verity table line too long");

    OK_OR_FATAL(ioctl(dm_control, DM_TABLE_LOAD, buffer),
                "DM_TABLE_LOAD for %s (algorithm %s)", name, algorithm);

    /* 3. Resume the device. */
    dm_init_header(dm, name);
    dm->flags = DM_READONLY_FLAG;
    OK_OR_FATAL(ioctl(dm_control, DM_DEV_SUSPEND, buffer),
                "DM_DEV_SUSPEND for %s", name);

    close(dm_control);
}

void dm_setup_crypt(const char *name, const char *src_path,
                    const char *cipher, const char *secret_hex,
                    int iv_offset)
{
    if (!cipher)
        cipher = DM_DEFAULT_CIPHER;

    int dm_control = open("/dev/mapper/control", O_RDWR | O_CLOEXEC);
    if (dm_control < 0)
        fatal("could not open /dev/mapper/control (CONFIG_DM_CRYPT?)");

    off_t sectors = source_sector_count(src_path);

    uint8_t buffer[DM_BUFFER_SIZE];
    struct dm_ioctl *dm = (struct dm_ioctl *)buffer;

    /* 1. Create the device. */
    dm_init_header(dm, name);
    snprintf(dm->uuid, sizeof(dm->uuid), "CRYPT-PLAIN-%s", name);
    OK_OR_FATAL(ioctl(dm_control, DM_DEV_CREATE, buffer),
                "DM_DEV_CREATE for %s", name);

    /* 2. Load the dm-crypt table. */
    dm_init_header(dm, name);
    dm->target_count = 1;
    dm->flags = DM_SECURE_DATA_FLAG;

    struct dm_target_spec *target = (struct dm_target_spec *)(buffer + dm->data_start);
    target->sector_start = 0;
    target->length = (uint64_t)sectors;
    snprintf(target->target_type, sizeof(target->target_type), "crypt");

    char *params = (char *)(target + 1);
    size_t params_max = DM_BUFFER_SIZE - dm->data_start - sizeof(struct dm_target_spec);
    int n = snprintf(params, params_max, "%s %s %d %s 0",
                     cipher, secret_hex, iv_offset, src_path);
    if (n < 0 || (size_t)n >= params_max)
        fatal("dm-crypt table line too long");

    OK_OR_FATAL(ioctl(dm_control, DM_TABLE_LOAD, buffer),
                "DM_TABLE_LOAD for %s (cipher %s)", name, cipher);

    /* 3. Resume the device (DM_DEV_SUSPEND with flags=0 means resume). */
    dm_init_header(dm, name);
    dm->flags = DM_SECURE_DATA_FLAG;
    OK_OR_FATAL(ioctl(dm_control, DM_DEV_SUSPEND, buffer),
                "DM_DEV_SUSPEND for %s", name);

    close(dm_control);
}
