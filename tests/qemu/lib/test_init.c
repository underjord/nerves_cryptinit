/*
 * Copyright 2026 Frank Hunleth
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * Minimal /sbin/init for the QEMU test rootfs. Reads /etc/test-marker
 * verbatim, writes it to stdout (the kernel console), then powers off.
 * Statically linked — no libc shared objects in the rootfs.
 *
 * Each test scenario provides its own /etc/test-marker; the
 * `expected` file just lists substrings the runner greps for.
 */

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/reboot.h>

int main(void)
{
    int fd = open("/etc/test-marker", O_RDONLY);
    if (fd >= 0) {
        char buf[4096];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0)
            (void) !write(STDOUT_FILENO, buf, (size_t)n);
        close(fd);
    } else {
        const char *msg = "test_init: /etc/test-marker missing\n";
        (void) !write(STDOUT_FILENO, msg, 36);
    }

    sync();
    syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
            LINUX_REBOOT_CMD_POWER_OFF, 0);
    return 0;
}
