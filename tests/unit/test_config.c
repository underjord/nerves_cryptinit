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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

#define CHECK_STREQ(a, b) do { \
    const char *_a = (a); const char *_b = (b); \
    if (!_a || !_b || strcmp(_a, _b) != 0) { \
        fprintf(stderr, "  FAIL %s:%d: \"%s\" != \"%s\"\n", \
                __FILE__, __LINE__, _a ? _a : "(null)", _b ? _b : "(null)"); \
        failures++; \
    } \
} while (0)

static void test_empty(void)
{
    struct config c;
    CHECK(config_parse("", &c) == 0);
    CHECK(c.rootfs_key == NULL);
    CHECK(c.data_device == NULL);
    config_free(&c);
}

static void test_minimal_rootfs_key(void)
{
    struct config c;
    CHECK(config_parse("rootfs_key=cmd:/usr/sbin/unlock\n", &c) == 0);
    CHECK_STREQ(c.rootfs_key, "cmd:/usr/sbin/unlock");
    CHECK(c.rootfs_cipher == NULL);
    config_free(&c);
}

static void test_full(void)
{
    struct config c;
    const char *text =
        "rootfs_key = cmd:/usr/sbin/unlock\n"
        "rootfs_cipher = aes-xts-plain64\n"
        "data_device = /dev/mmcblk0p3\n"
        "data_key = cmd:/usr/sbin/unlock\n"
        "device_timeout = 10s\n";
    CHECK(config_parse(text, &c) == 0);
    CHECK_STREQ(c.rootfs_key, "cmd:/usr/sbin/unlock");
    CHECK_STREQ(c.rootfs_cipher, "aes-xts-plain64");
    CHECK_STREQ(c.data_device, "/dev/mmcblk0p3");
    CHECK_STREQ(c.data_key, "cmd:/usr/sbin/unlock");
    CHECK_STREQ(c.device_timeout, "10s");
    config_free(&c);
}

static void test_comments(void)
{
    struct config c;
    const char *text =
        "# leading comment\n"
        "\n"
        "  # indented comment\n"
        "rootfs_key = hex:abc\n"
        "# trailing comment after value\n";
    CHECK(config_parse(text, &c) == 0);
    CHECK_STREQ(c.rootfs_key, "hex:abc");
    config_free(&c);
}

static void test_no_trailing_newline(void)
{
    struct config c;
    CHECK(config_parse("rootfs_key=hex:01", &c) == 0);
    CHECK_STREQ(c.rootfs_key, "hex:01");
    config_free(&c);
}

static void test_whitespace_trimming(void)
{
    struct config c;
    CHECK(config_parse("\t  rootfs_key  \t=\t  hex:cafe  \t\n", &c) == 0);
    CHECK_STREQ(c.rootfs_key, "hex:cafe");
    config_free(&c);
}

static void test_empty_value(void)
{
    struct config c;
    CHECK(config_parse("rootfs_cipher=\n", &c) == 0);
    CHECK_STREQ(c.rootfs_cipher, "");
    config_free(&c);
}

static void test_unknown_key_tolerated(void)
{
    struct config c;
    CHECK(config_parse("not_a_real_key = whatever\nrootfs_key = hex:00\n", &c) == 0);
    CHECK_STREQ(c.rootfs_key, "hex:00");
    config_free(&c);
}

static void test_malformed_line_tolerated(void)
{
    struct config c;
    CHECK(config_parse("garbage with no equals\nrootfs_key=hex:00\n", &c) == 0);
    CHECK_STREQ(c.rootfs_key, "hex:00");
    config_free(&c);
}

static void test_value_with_equals(void)
{
    /* Some values may legitimately contain '=' (e.g., cmd: helper args). The
       parser must split only on the first '='. */
    struct config c;
    CHECK(config_parse("rootfs_key = cmd:/bin/foo --key=value\n", &c) == 0);
    CHECK_STREQ(c.rootfs_key, "cmd:/bin/foo --key=value");
    config_free(&c);
}

static void test_missing_file_is_ok(void)
{
    struct config c;
    CHECK(config_parse_file("/no/such/path/initramfs.conf", &c) == 0);
    CHECK(c.rootfs_key == NULL);
    config_free(&c);
}

static void test_file_round_trip(void)
{
    char path[] = "/tmp/nerves_cryptinit_test_XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd >= 0) {
        const char *text = "rootfs_key = file:/etc/key\n";
        (void) !write(fd, text, strlen(text));
        close(fd);

        struct config c;
        CHECK(config_parse_file(path, &c) == 0);
        CHECK_STREQ(c.rootfs_key, "file:/etc/key");
        config_free(&c);
        unlink(path);
    }
}

static struct {
    const char *name;
    void (*fn)(void);
} cases[] = {
    { "empty", test_empty },
    { "minimal_rootfs_key", test_minimal_rootfs_key },
    { "full", test_full },
    { "comments", test_comments },
    { "no_trailing_newline", test_no_trailing_newline },
    { "whitespace_trimming", test_whitespace_trimming },
    { "empty_value", test_empty_value },
    { "unknown_key_tolerated", test_unknown_key_tolerated },
    { "malformed_line_tolerated", test_malformed_line_tolerated },
    { "value_with_equals", test_value_with_equals },
    { "missing_file_is_ok", test_missing_file_is_ok },
    { "file_round_trip", test_file_round_trip },
};

int main(void)
{
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        printf("test_config: %s\n", cases[i].name);
        cases[i].fn();
    }
    if (failures > 0) {
        fprintf(stderr, "test_config: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_config: OK\n");
    return 0;
}
