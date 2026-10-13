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

#ifndef CMDLINE_H
#define CMDLINE_H

struct cmdline {
    /* Owns the parsed cmdline buffer. The const char * fields below point
       into it and are valid until cmdline_free(). */
    char *buffer;

    const char *root;             /* root=          */
    const char *rootfstype;       /* rootfstype=    */
    const char *rootflags;        /* rootflags=     */
    const char *init;             /* init=          */
    const char *verity_roothash;  /* verity_roothash= */
    int read_only;                /* 1 (default) unless "rw" seen */
};

/* Parse a cmdline string. The string is copied; caller can free. */
int cmdline_parse(const char *line, struct cmdline *out);

/* Read /proc/cmdline (or another path) and parse it. */
int cmdline_parse_file(const char *path, struct cmdline *out);

void cmdline_free(struct cmdline *cmd);

#endif
