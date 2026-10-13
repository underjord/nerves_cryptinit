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

#include "cmdline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void test_minimal(void)
{
    struct cmdline c;
    CHECK(cmdline_parse("root=/dev/mmcblk0p2", &c) == 0);
    CHECK_STREQ(c.root, "/dev/mmcblk0p2");
    CHECK(c.rootfstype == NULL);
    CHECK(c.read_only == 1);
    cmdline_free(&c);
}

static void test_full(void)
{
    struct cmdline c;
    const char *line = "root=/dev/mmcblk0p2 rootfstype=squashfs ro "
                       "init=/sbin/init verity_roothash=deadbeef";
    CHECK(cmdline_parse(line, &c) == 0);
    CHECK_STREQ(c.root, "/dev/mmcblk0p2");
    CHECK_STREQ(c.rootfstype, "squashfs");
    CHECK_STREQ(c.init, "/sbin/init");
    CHECK_STREQ(c.verity_roothash, "deadbeef");
    CHECK(c.read_only == 1);
    cmdline_free(&c);
}

static void test_rw_overrides_default(void)
{
    struct cmdline c;
    CHECK(cmdline_parse("rw", &c) == 0);
    CHECK(c.read_only == 0);
    cmdline_free(&c);
}

static void test_empty(void)
{
    struct cmdline c;
    CHECK(cmdline_parse("", &c) == 0);
    CHECK(c.root == NULL);
    CHECK(c.read_only == 1);
    cmdline_free(&c);
}

static void test_null(void)
{
    struct cmdline c;
    CHECK(cmdline_parse(NULL, &c) == 0);
    CHECK(c.root == NULL);
    CHECK(c.read_only == 1);
    cmdline_free(&c);
}

static void test_extra_whitespace(void)
{
    struct cmdline c;
    CHECK(cmdline_parse("  root=/dev/sda1\t\trootfstype=ext4 \n", &c) == 0);
    CHECK_STREQ(c.root, "/dev/sda1");
    CHECK_STREQ(c.rootfstype, "ext4");
    cmdline_free(&c);
}

static void test_ignores_unknown(void)
{
    struct cmdline c;
    const char *line = "BOOT_IMAGE=/vmlinuz quiet root=/dev/sda1 "
                       "console=ttyS0,115200 splash";
    CHECK(cmdline_parse(line, &c) == 0);
    CHECK_STREQ(c.root, "/dev/sda1");
    cmdline_free(&c);
}

static void test_quoted_value(void)
{
    struct cmdline c;
    CHECK(cmdline_parse("rootflags=\"noatime,nodiratime\"", &c) == 0);
    CHECK_STREQ(c.rootflags, "noatime,nodiratime");
    cmdline_free(&c);
}

static void test_rootflags_with_commas(void)
{
    struct cmdline c;
    CHECK(cmdline_parse("rootflags=noatime,nodiratime", &c) == 0);
    CHECK_STREQ(c.rootflags, "noatime,nodiratime");
    cmdline_free(&c);
}

static struct {
    const char *name;
    void (*fn)(void);
} cases[] = {
    { "minimal", test_minimal },
    { "full", test_full },
    { "rw_overrides_default", test_rw_overrides_default },
    { "empty", test_empty },
    { "null", test_null },
    { "extra_whitespace", test_extra_whitespace },
    { "ignores_unknown", test_ignores_unknown },
    { "quoted_value", test_quoted_value },
    { "rootflags_with_commas", test_rootflags_with_commas },
};

int main(void)
{
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        printf("test_cmdline: %s\n", cases[i].name);
        cases[i].fn();
    }
    if (failures > 0) {
        fprintf(stderr, "test_cmdline: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_cmdline: OK\n");
    return 0;
}
