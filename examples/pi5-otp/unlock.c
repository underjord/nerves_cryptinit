/*
 * Copyright 2026 Frank Hunleth
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * Reads the 8 customer-programmable OTP rows on a Raspberry Pi 4/5 via
 * the VideoCore mailbox interface and prints them as 64 hex characters
 * on stdout — exactly what nerves_cryptinit's `cmd:` helper protocol
 * wants.
 *
 * For a production deployment with separate rootfs and data keys, wrap
 * this OTP read in HKDF and pass a "purpose" argument from the config.
 * See ../README.md for the recipe.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define IOCTL_MBOX_PROPERTY       _IOWR(100, 0, char *)
#define MBOX_TAG_GET_CUSTOMER_OTP 0x00030021u
#define OTP_ROW_COUNT             8

int main(void)
{
    int fd = open("/dev/vcio", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "unlock: open /dev/vcio: %s\n", strerror(errno));
        return 1;
    }

    /* Mailbox property buffer layout (all u32, native endian):
         [0] total buffer size
         [1] request/response code (0 = request)
         [2..]  zero or more tags
         end:   u32 = 0 (end tag)
       Each tag is:
         [0] tag id
         [1] value buffer size in bytes
         [2] request size / response indicator
         [3..]  value buffer
       For GET_CUSTOMER_OTP: request is (start_row, num_rows); response
       fills num_rows * u32 starting at the same offset. */
    uint32_t buf[5 + OTP_ROW_COUNT + 1];
    int i = 0;
    buf[i++] = sizeof(buf);
    buf[i++] = 0;
    buf[i++] = MBOX_TAG_GET_CUSTOMER_OTP;
    buf[i++] = (2 + OTP_ROW_COUNT) * sizeof(uint32_t);
    buf[i++] = 0;
    buf[i++] = 0;                  /* start row */
    buf[i++] = OTP_ROW_COUNT;      /* row count */
    for (int r = 0; r < OTP_ROW_COUNT; r++)
        buf[i++] = 0;              /* response slots */
    buf[i++] = 0;                  /* end tag */

    if (ioctl(fd, IOCTL_MBOX_PROPERTY, buf) < 0) {
        fprintf(stderr, "unlock: mailbox ioctl: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    close(fd);

    /* The first response u32 lands at index 5 (the start-row arg slot
       is reused). The 8 rows occupy indices 5..12. */
    const uint32_t *rows = &buf[5];

    /* Sanity check: if every row is zero or every row is 0xFFFFFFFF the
       OTP almost certainly hasn't been provisioned, and using the value
       would derive a key shared by every unprovisioned device. Better
       to fail loudly than silently encrypt with a known-weak key. */
    int all_zero = 1, all_ones = 1;
    for (int r = 0; r < OTP_ROW_COUNT; r++) {
        if (rows[r] != 0x00000000u) all_zero = 0;
        if (rows[r] != 0xFFFFFFFFu) all_ones = 0;
    }
    if (all_zero || all_ones) {
        fprintf(stderr, "unlock: customer OTP appears unprovisioned (%s)\n",
                all_zero ? "all zero" : "all ones");
        return 1;
    }

    for (int r = 0; r < OTP_ROW_COUNT; r++)
        printf("%08x", rows[r]);

    return 0;
}
