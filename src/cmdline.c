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
#include "log.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void match_kv(struct cmdline *out, const char *key, const char *value)
{
    if (strcmp(key, "root") == 0) out->root = value;
    else if (strcmp(key, "rootfstype") == 0) out->rootfstype = value;
    else if (strcmp(key, "rootflags") == 0) out->rootflags = value;
    else if (strcmp(key, "init") == 0) out->init = value;
    else if (strcmp(key, "verity_roothash") == 0) out->verity_roothash = value;
    /* Unknown keys are silently ignored — the kernel cmdline carries plenty
       of things we don't care about. */
}

static void match_flag(struct cmdline *out, const char *token)
{
    if (strcmp(token, "ro") == 0) out->read_only = 1;
    else if (strcmp(token, "rw") == 0) out->read_only = 0;
}

int cmdline_parse(const char *line, struct cmdline *out)
{
    memset(out, 0, sizeof(*out));
    out->read_only = 1;

    if (!line)
        return 0;

    out->buffer = strdup(line);
    if (!out->buffer)
        ERR_RETURN("strdup failed parsing cmdline");

    char *p = out->buffer;
    while (*p) {
        while (*p && isspace((unsigned char)*p))
            p++;
        if (!*p) break;

        char *token = p;
        while (*p && !isspace((unsigned char)*p))
            p++;
        if (*p) {
            *p = '\0';
            p++;
        }

        char *eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';
            char *value = eq + 1;
            /* Strip surrounding double quotes if present. */
            if (*value == '"') {
                value++;
                size_t vlen = strlen(value);
                if (vlen > 0 && value[vlen - 1] == '"')
                    value[vlen - 1] = '\0';
            }
            match_kv(out, token, value);
        } else {
            match_flag(out, token);
        }
    }
    return 0;
}

int cmdline_parse_file(const char *path, struct cmdline *out)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        ERR_RETURN("could not open %s", path);

    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n < 0)
        ERR_RETURN("could not read %s", path);

    buf[n] = '\0';
    return cmdline_parse(buf, out);
}

void cmdline_free(struct cmdline *cmd)
{
    free(cmd->buffer);
    memset(cmd, 0, sizeof(*cmd));
}
