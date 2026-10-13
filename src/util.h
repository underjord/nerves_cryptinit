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

#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdint.h>

#define PROGRAM_NAME "nerves_cryptinit"

#ifndef PROGRAM_VERSION
#define PROGRAM_VERSION unknown
#endif

#define xstr(s) str(s)
#define str(s) #s
#define PROGRAM_VERSION_STR xstr(PROGRAM_VERSION)

void trim_string_in_place(char *str);
int hex_to_bytes(const char *str, uint8_t *bytes, size_t numbytes);
int bytes_to_hex(const uint8_t *bytes, char *str, size_t byte_count);

#endif
