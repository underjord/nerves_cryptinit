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
#include "log.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int validate_hex(const char *s)
{
    size_t len = strlen(s);
    if (len == 0)
        ERR_RETURN("key is empty");
    if (len % 2 != 0)
        ERR_RETURN("key has odd length (%zu hex chars)", len);
    if (len > KEY_HEX_MAX)
        ERR_RETURN("key is too long (%zu chars, max %d)", len, KEY_HEX_MAX);
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            ERR_RETURN("non-hex character in key at offset %zu", i);
    }
    return 0;
}

static int resolve_hex(const char *value, char *out, size_t out_size)
{
    size_t len = strlen(value);
    if (len + 1 > out_size)
        ERR_RETURN("hex key doesn't fit in output buffer");
    memcpy(out, value, len + 1);
    return validate_hex(out);
}

static int resolve_file(const char *path, char *out, size_t out_size)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        ERR_RETURN("could not open key file %s: %s", path, strerror(errno));

    ssize_t n = read(fd, out, out_size - 1);
    close(fd);
    if (n < 0)
        ERR_RETURN("could not read key from %s", path);

    out[n] = '\0';
    trim_string_in_place(out);
    return validate_hex(out);
}

/* Tokenize argv from a single string by splitting on whitespace. The input
   is mutated (NULs written over separators); returned argv pointers point
   into it. On success returns argc; on failure returns -1. */
static int tokenize_argv(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p && isspace((unsigned char)*p))
            p++;
        if (!*p)
            break;
        if (argc >= max_args - 1)
            ERR_RETURN("too many cmd: arguments (max %d)", max_args - 1);
        argv[argc++] = p;
        while (*p && !isspace((unsigned char)*p))
            p++;
        if (*p) {
            *p = '\0';
            p++;
        }
    }
    argv[argc] = NULL;
    return argc;
}

static int resolve_cmd(const char *value, char *out, size_t out_size)
{
    char *line = strdup(value);
    if (!line)
        ERR_RETURN("strdup failed");

    char *argv[16];
    int argc = tokenize_argv(line, argv, 16);
    if (argc < 1) {
        free(line);
        ERR_RETURN("cmd: spec has no program");
    }

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        free(line);
        ERR_RETURN("pipe() failed");
    }

    pid_t child = fork();
    if (child < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        free(line);
        ERR_RETURN("fork() failed");
    }

    if (child == 0) {
        /* Child: stdout to pipe, then exec. */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execvp(argv[0], argv);
        /* exec failed */
        _exit(127);
    }

    /* Parent: read up to out_size-1 bytes from the pipe. */
    close(pipefd[1]);
    size_t total = 0;
    while (total < out_size - 1) {
        ssize_t n = read(pipefd[0], out + total, out_size - 1 - total);
        if (n <= 0)
            break;
        total += (size_t)n;
    }
    close(pipefd[0]);
    out[total] = '\0';

    int status;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR)
        ;

    free(line);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        ERR_RETURN("key helper '%s' exited with status %d",
                   value, WIFEXITED(status) ? WEXITSTATUS(status) : -1);

    trim_string_in_place(out);
    return validate_hex(out);
}

int key_resolve(const char *spec, char *out, size_t out_size)
{
    if (out_size < KEY_HEX_MAX + 1)
        ERR_RETURN("key buffer too small");
    if (!spec || !*spec)
        ERR_RETURN("empty key specification");

    if (strncmp(spec, "hex:", 4) == 0)
        return resolve_hex(spec + 4, out, out_size);
    if (strncmp(spec, "file:", 5) == 0)
        return resolve_file(spec + 5, out, out_size);
    if (strncmp(spec, "cmd:", 4) == 0)
        return resolve_cmd(spec + 4, out, out_size);

    ERR_RETURN("unknown key prefix in '%s' (expected hex:, file:, or cmd:)", spec);
}
