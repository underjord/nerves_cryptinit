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

#include "key.h"

#include <fcntl.h>
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

#define CHECK_STREQ(a, b) do { \
    const char *_a = (a); const char *_b = (b); \
    if (!_a || !_b || strcmp(_a, _b) != 0) { \
        fprintf(stderr, "  FAIL %s:%d: \"%s\" != \"%s\"\n", \
                __FILE__, __LINE__, _a ? _a : "(null)", _b ? _b : "(null)"); \
        failures++; \
    } \
} while (0)

static void test_hex_lowercase(void)
{
    char out[KEY_HEX_MAX + 1];
    CHECK(key_resolve("hex:deadbeef", out, sizeof(out)) == 0);
    CHECK_STREQ(out, "deadbeef");
}

static void test_hex_uppercase_passes_through(void)
{
    /* dm-crypt accepts either case, so we don't normalize. */
    char out[KEY_HEX_MAX + 1];
    CHECK(key_resolve("hex:DEADBEEF", out, sizeof(out)) == 0);
    CHECK_STREQ(out, "DEADBEEF");
}

static void test_hex_full_aes256(void)
{
    char out[KEY_HEX_MAX + 1];
    /* 64 hex chars = 32 bytes = AES-256 key */
    const char *spec = "hex:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    CHECK(key_resolve(spec, out, sizeof(out)) == 0);
    CHECK_STREQ(out, spec + 4);
}

static void test_hex_odd_length_rejected(void)
{
    char out[KEY_HEX_MAX + 1];
    CHECK(key_resolve("hex:abc", out, sizeof(out)) == -1);
}

static void test_hex_non_hex_rejected(void)
{
    char out[KEY_HEX_MAX + 1];
    CHECK(key_resolve("hex:nothex", out, sizeof(out)) == -1);
}

static void test_hex_empty_rejected(void)
{
    char out[KEY_HEX_MAX + 1];
    CHECK(key_resolve("hex:", out, sizeof(out)) == -1);
}

static void test_unknown_prefix_rejected(void)
{
    char out[KEY_HEX_MAX + 1];
    CHECK(key_resolve("kerberos:foo", out, sizeof(out)) == -1);
}

static void test_empty_spec_rejected(void)
{
    char out[KEY_HEX_MAX + 1];
    CHECK(key_resolve("", out, sizeof(out)) == -1);
    CHECK(key_resolve(NULL, out, sizeof(out)) == -1);
}

static void test_file(void)
{
    char path[] = "/tmp/cryptinit_key_test_XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd < 0) return;

    const char *contents = "cafebabe\n";
    (void) !write(fd, contents, strlen(contents));
    close(fd);

    char spec[256];
    snprintf(spec, sizeof(spec), "file:%s", path);

    char out[KEY_HEX_MAX + 1];
    CHECK(key_resolve(spec, out, sizeof(out)) == 0);
    CHECK_STREQ(out, "cafebabe");

    unlink(path);
}

static void test_file_with_garbage_rejected(void)
{
    char path[] = "/tmp/cryptinit_key_test_XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd < 0) return;

    const char *contents = "not really hex\n";
    (void) !write(fd, contents, strlen(contents));
    close(fd);

    char spec[256];
    snprintf(spec, sizeof(spec), "file:%s", path);

    char out[KEY_HEX_MAX + 1];
    CHECK(key_resolve(spec, out, sizeof(out)) == -1);

    unlink(path);
}

static void test_file_missing_rejected(void)
{
    char out[KEY_HEX_MAX + 1];
    CHECK(key_resolve("file:/nonexistent/path", out, sizeof(out)) == -1);
}

static void test_cmd_echo(void)
{
    char out[KEY_HEX_MAX + 1];
    CHECK(key_resolve("cmd:/bin/echo deadc0de", out, sizeof(out)) == 0);
    CHECK_STREQ(out, "deadc0de");
}

static void test_cmd_with_args(void)
{
    char out[KEY_HEX_MAX + 1];
    /* /bin/echo joins multiple args with spaces — but we trim trailing
       whitespace and the arg has no internal space, so we get the hex. */
    CHECK(key_resolve("cmd:/bin/echo -n abcd1234", out, sizeof(out)) == 0);
    CHECK_STREQ(out, "abcd1234");
}

static void test_cmd_failure_propagates(void)
{
    char out[KEY_HEX_MAX + 1];
    /* `false` exits non-zero. */
    CHECK(key_resolve("cmd:/usr/bin/false", out, sizeof(out)) == -1);
}

static void test_cmd_garbage_output_rejected(void)
{
    char out[KEY_HEX_MAX + 1];
    CHECK(key_resolve("cmd:/bin/echo notvalid", out, sizeof(out)) == -1);
}

static void test_cmd_missing_program(void)
{
    char out[KEY_HEX_MAX + 1];
    CHECK(key_resolve("cmd:/no/such/binary", out, sizeof(out)) == -1);
}

static void test_cmd_empty_spec_rejected(void)
{
    char out[KEY_HEX_MAX + 1];
    CHECK(key_resolve("cmd:", out, sizeof(out)) == -1);
}

static struct {
    const char *name;
    void (*fn)(void);
} cases[] = {
    { "hex_lowercase", test_hex_lowercase },
    { "hex_uppercase_passes_through", test_hex_uppercase_passes_through },
    { "hex_full_aes256", test_hex_full_aes256 },
    { "hex_odd_length_rejected", test_hex_odd_length_rejected },
    { "hex_non_hex_rejected", test_hex_non_hex_rejected },
    { "hex_empty_rejected", test_hex_empty_rejected },
    { "unknown_prefix_rejected", test_unknown_prefix_rejected },
    { "empty_spec_rejected", test_empty_spec_rejected },
    { "file", test_file },
    { "file_with_garbage_rejected", test_file_with_garbage_rejected },
    { "file_missing_rejected", test_file_missing_rejected },
    { "cmd_echo", test_cmd_echo },
    { "cmd_with_args", test_cmd_with_args },
    { "cmd_failure_propagates", test_cmd_failure_propagates },
    { "cmd_garbage_output_rejected", test_cmd_garbage_output_rejected },
    { "cmd_missing_program", test_cmd_missing_program },
    { "cmd_empty_spec_rejected", test_cmd_empty_spec_rejected },
};

int main(void)
{
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        printf("test_key: %s\n", cases[i].name);
        cases[i].fn();
    }
    if (failures > 0) {
        fprintf(stderr, "test_key: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_key: OK\n");
    return 0;
}
