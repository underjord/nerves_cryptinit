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
#include "log.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void match_kv(struct config *out, const char *key, const char *value)
{
    if (strcmp(key, "rootfs_key") == 0) out->rootfs_key = value;
    else if (strcmp(key, "rootfs_cipher") == 0) out->rootfs_cipher = value;
    else if (strcmp(key, "data_device") == 0) out->data_device = value;
    else if (strcmp(key, "data_key") == 0) out->data_key = value;
    else if (strcmp(key, "data_cipher") == 0) out->data_cipher = value;
    else if (strcmp(key, "device_timeout") == 0) out->device_timeout = value;
    else info("unknown config key: %s", key);
}

int config_parse(const char *text, struct config *out)
{
    memset(out, 0, sizeof(*out));

    if (!text)
        return 0;

    out->buffer = strdup(text);
    if (!out->buffer)
        ERR_RETURN("strdup failed parsing config");

    char *line = out->buffer;
    int line_no = 0;
    while (line && *line) {
        line_no++;
        char *eol = strchr(line, '\n');
        if (eol) {
            *eol = '\0';
        }

        char *cursor = line;
        while (*cursor && isspace((unsigned char)*cursor))
            cursor++;

        if (*cursor == '\0' || *cursor == '#')
            goto next;

        char *eq = strchr(cursor, '=');
        if (!eq) {
            info("config: ignoring malformed line %d", line_no);
            goto next;
        }

        *eq = '\0';
        char *key = cursor;
        char *value = eq + 1;
        trim_string_in_place(key);
        trim_string_in_place(value);

        if (*key == '\0') {
            info("config: ignoring empty key on line %d", line_no);
            goto next;
        }

        match_kv(out, key, value);

next:
        line = eol ? eol + 1 : NULL;
    }
    return 0;
}

int config_parse_file(const char *path, struct config *out)
{
    memset(out, 0, sizeof(*out));

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT)
            return 0;
        ERR_RETURN("could not open %s: %s", path, strerror(errno));
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        ERR_RETURN("fstat failed on %s", path);
    }

    if (st.st_size > 64 * 1024) {
        close(fd);
        ERR_RETURN("%s is suspiciously large (%lld bytes)", path, (long long)st.st_size);
    }

    char *buf = malloc((size_t)st.st_size + 1);
    if (!buf) {
        close(fd);
        ERR_RETURN("malloc failed reading %s", path);
    }

    ssize_t n = read(fd, buf, (size_t)st.st_size);
    close(fd);
    if (n < 0) {
        free(buf);
        ERR_RETURN("could not read %s", path);
    }
    buf[n] = '\0';

    int rc = config_parse(buf, out);
    free(buf);
    return rc;
}

void config_free(struct config *cfg)
{
    free(cfg->buffer);
    memset(cfg, 0, sizeof(*cfg));
}
