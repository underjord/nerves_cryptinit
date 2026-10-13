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

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

/* Write an n-byte little-endian value into buf at off. */
static void put_le(uint8_t *buf, size_t off, unsigned n, uint64_t val)
{
    for (unsigned i = 0; i < n; i++)
        buf[off + i] = (uint8_t)(val >> (8 * i));
}

/* Write `len` bytes to a temp file and return its path in `path` (caller
   unlinks). Returns 0 on success. */
static int write_temp(const uint8_t *buf, size_t len, char *path)
{
    strcpy(path, "/tmp/nerves_cryptinit_fs_XXXXXX");
    int fd = mkstemp(path);
    if (fd < 0)
        return -1;
    ssize_t w = write(fd, buf, len);
    close(fd);
    return (w == (ssize_t)len) ? 0 : -1;
}

static void test_round_up(void)
{
    CHECK(fs_hash_offset_for_size(0) == 0);
    CHECK(fs_hash_offset_for_size(1) == 4096);
    CHECK(fs_hash_offset_for_size(4095) == 4096);
    CHECK(fs_hash_offset_for_size(4096) == 4096);
    CHECK(fs_hash_offset_for_size(4097) == 8192);
    CHECK(fs_hash_offset_for_size(32768) == 32768);
}

static void test_squashfs(void)
{
    uint8_t buf[4096] = {0};
    put_le(buf, 0, 4, 0x73717368);   /* "hsqs" */
    put_le(buf, 40, 8, 0x9001);      /* bytes_used = 36865 -> rounds to 40960 */

    char path[64];
    CHECK(write_temp(buf, sizeof(buf), path) == 0);
    uint64_t off = 0;
    CHECK(fs_verity_hash_offset(path, &off) == 0);
    CHECK(off == 40960);
    unlink(path);
}

static void test_erofs(void)
{
    uint8_t buf[4096] = {0};
    put_le(buf, 1024, 4, 0xE0F5E1E2);   /* magic */
    buf[1024 + 12] = 12;                /* blkszbits -> 4096-byte blocks */
    put_le(buf, 1024 + 36, 4, 5);       /* 5 blocks -> 20480 bytes */

    char path[64];
    CHECK(write_temp(buf, sizeof(buf), path) == 0);
    uint64_t off = 0;
    CHECK(fs_verity_hash_offset(path, &off) == 0);
    CHECK(off == 20480);
    unlink(path);
}

static void test_ext4(void)
{
    uint8_t buf[4096] = {0};
    put_le(buf, 1024 + 56, 2, 0xEF53);  /* s_magic */
    put_le(buf, 1024 + 24, 4, 2);       /* s_log_block_size -> 4096-byte blocks */
    put_le(buf, 1024 + 4, 4, 10);       /* s_blocks_count_lo = 10 -> 40960 bytes */

    char path[64];
    CHECK(write_temp(buf, sizeof(buf), path) == 0);
    uint64_t off = 0;
    CHECK(fs_verity_hash_offset(path, &off) == 0);
    CHECK(off == 40960);
    unlink(path);
}

static void test_ext4_64bit(void)
{
    uint8_t buf[4096] = {0};
    put_le(buf, 1024 + 56, 2, 0xEF53);
    put_le(buf, 1024 + 24, 4, 0);          /* 1024-byte blocks */
    put_le(buf, 1024 + 0x60, 4, 0x80);     /* EXT4_FEATURE_INCOMPAT_64BIT */
    put_le(buf, 1024 + 4, 4, 0);           /* count_lo */
    put_le(buf, 1024 + 0x150, 4, 1);       /* count_hi = 1 -> 2^32 blocks */

    char path[64];
    CHECK(write_temp(buf, sizeof(buf), path) == 0);
    uint64_t off = 0;
    CHECK(fs_verity_hash_offset(path, &off) == 0);
    CHECK(off == ((uint64_t)1 << 32) * 1024);
    unlink(path);
}

static void test_unknown(void)
{
    uint8_t buf[4096] = {0};   /* no magic anywhere */
    char path[64];
    CHECK(write_temp(buf, sizeof(buf), path) == 0);
    uint64_t off = 0;
    CHECK(fs_verity_hash_offset(path, &off) == -1);
    unlink(path);
}

static void test_missing_device(void)
{
    uint64_t off = 0;
    CHECK(fs_verity_hash_offset("/no/such/device", &off) == -1);
}

static struct {
    const char *name;
    void (*fn)(void);
} cases[] = {
    { "round_up", test_round_up },
    { "squashfs", test_squashfs },
    { "erofs", test_erofs },
    { "ext4", test_ext4 },
    { "ext4_64bit", test_ext4_64bit },
    { "unknown", test_unknown },
    { "missing_device", test_missing_device },
};

int main(void)
{
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        printf("test_fs: %s\n", cases[i].name);
        cases[i].fn();
    }
    if (failures > 0) {
        fprintf(stderr, "test_fs: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_fs: OK\n");
    return 0;
}
