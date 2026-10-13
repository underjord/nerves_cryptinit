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

#include "util.h"
#include "log.h"

#include <ctype.h>
#include <string.h>

void trim_string_in_place(char *str)
{
    char *first = str;
    while (*first && isspace((unsigned char)*first))
        first++;

    if (*first == '\0') {
        str[0] = '\0';
        return;
    }

    char *last = first + strlen(first) - 1;
    while (last != first && isspace((unsigned char)*last))
        last--;

    size_t len = last - first + 1;
    memmove(str, first, len);
    str[len] = '\0';
}

static int hexchar_to_nibble(char c, uint8_t *out)
{
    if (c >= '0' && c <= '9') { *out = (uint8_t)(c - '0'); return 0; }
    if (c >= 'a' && c <= 'f') { *out = (uint8_t)(c - 'a' + 10); return 0; }
    if (c >= 'A' && c <= 'F') { *out = (uint8_t)(c - 'A' + 10); return 0; }
    return -1;
}

int hex_to_bytes(const char *str, uint8_t *bytes, size_t numbytes)
{
    if (strlen(str) != numbytes * 2)
        ERR_RETURN("hex string wrong length: expected %zu chars, got %zu", numbytes * 2, strlen(str));

    for (size_t i = 0; i < numbytes; i++) {
        uint8_t hi, lo;
        if (hexchar_to_nibble(str[2 * i], &hi) < 0 || hexchar_to_nibble(str[2 * i + 1], &lo) < 0)
            ERR_RETURN("invalid hex character");
        bytes[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

int bytes_to_hex(const uint8_t *bytes, char *str, size_t byte_count)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < byte_count; i++) {
        str[2 * i] = hex[bytes[i] >> 4];
        str[2 * i + 1] = hex[bytes[i] & 0xf];
    }
    str[2 * byte_count] = '\0';
    return 0;
}
