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

#include "log.h"
#include "util.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int kmsg_fd = -1;
static int pmsg_fd = -1;

void log_init(void)
{
    if (kmsg_fd < 0)
        kmsg_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    /* /dev/pmsg0 is the persistent message ring (CONFIG_PSTORE_PMSG +
       CONFIG_PSTORE_RAM with a ramoops reserved-memory region). When
       present, our messages survive a reboot or panic and can be read
       back from /sys/fs/pstore/pmsg-ramoops-0 after the next boot. We
       always try; absence is normal. */
    if (pmsg_fd < 0)
        pmsg_fd = open("/dev/pmsg0", O_WRONLY | O_CLOEXEC);
}

static void log_write(const char *str, size_t len)
{
    (void) !write(STDERR_FILENO, str, len);
    if (kmsg_fd >= 0)
        (void) !write(kmsg_fd, str, len);
    if (pmsg_fd >= 0)
        (void) !write(pmsg_fd, str, len);
}

static void log_format(const char *fmt, va_list ap)
{
    char *msg;
    if (vasprintf(&msg, fmt, ap) < 0)
        return;

    char *line;
    int len = asprintf(&line, PROGRAM_NAME ": %s\n", msg);
    free(msg);

    if (len >= 0) {
        log_write(line, (size_t)len);
        free(line);
    }
}

void info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_format(fmt, ap);
    va_end(ap);
}

void fatal(const char *fmt, ...)
{
    info("FATAL ERROR:");

    va_list ap;
    va_start(ap, fmt);
    log_format(fmt, ap);
    va_end(ap);

    info("cannot continue");

    /* Brief pause so the kernel has a chance to flush the message
       before we exit and the kernel panics. */
    sleep(1);
    exit(1);
}
