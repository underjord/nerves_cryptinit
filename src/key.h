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

#ifndef KEY_H
#define KEY_H

#include <stddef.h>

/* Maximum key length we'll accept, in hex characters (excluding NUL).
   256 bytes / 512 hex chars covers AES-256-XTS keys. */
#define KEY_HEX_MAX 512

/* Resolve a key specification to a hex string suitable for the dm-crypt
   table line. Recognized forms:
     hex:DEADBEEF...        - literal hex bytes from config
     file:/path             - read hex bytes from a file in the CPIO
     cmd:/path arg1 arg2    - fork+exec, read hex bytes from stdout

   Trailing whitespace is stripped from file contents and cmd output.
   The result is validated to contain only hex digits and have an even
   length.

   Returns 0 on success and writes the (NUL-terminated) hex string to
   `out`; returns -1 on failure. `out_size` is the size of the buffer
   including the trailing NUL — it must be at least KEY_HEX_MAX + 1. */
int key_resolve(const char *spec, char *out, size_t out_size);

#endif
