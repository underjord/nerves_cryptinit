/*
 * Copyright 2026 Frank Hunleth
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * LD_PRELOAD / DYLD_INSERT_LIBRARIES shim that lets us run the real init
 * binary against a fake root in $WORK/, intercepting the privileged
 * syscalls and printing a transcript so the integration tests can diff
 * against an expected log.
 */

#define _GNU_SOURCE

#include <dirent.h>
#include <dlfcn.h>
#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <linux/dm-ioctl.h>

#ifndef BLKSSZGET
#define BLKSSZGET _IO(0x12, 104)
#endif

#define log(MSG, ...) do { fprintf(stderr, "fixture: " MSG "\n", ## __VA_ARGS__); } while (0)

#ifndef __APPLE__
#define ORIGINAL(name) original_##name
#define REPLACEMENT(name) name
#define OVERRIDE(ret, name, args) \
    static ret (*original_##name) args; \
    __attribute__((constructor)) static void init_##name(void) { ORIGINAL(name) = dlsym(RTLD_NEXT, #name); } \
    ret REPLACEMENT(name) args

#define REPLACE(ret, name, args) ret REPLACEMENT(name) args
#else
#define ORIGINAL(name) name
#define REPLACEMENT(name) new_##name
#define OVERRIDE(ret, name, args) \
    ret REPLACEMENT(name) args; \
    __attribute__((used)) static struct { const void *replacement; const void *original; } _interpose_##name \
        __attribute__ ((section ("__DATA,__interpose"))) = { \
            (const void *)(unsigned long)&REPLACEMENT(name), \
            (const void *)(unsigned long)&ORIGINAL(name) \
        }; \
    ret REPLACEMENT(name) args

#define REPLACE(ret, name, args) OVERRIDE(ret, name, args)
#endif

static pid_t starting_pid = 0;
static const char *work = NULL;
static bool remounted_root = false;

__attribute__((constructor)) static void fixture_init(void)
{
#ifdef __APPLE__
    /* Interpose only rewrites calls from outside this dylib, so a plain
       getpid() here returns the real PID. */
    starting_pid = getpid();
#else
    /* Don't call the overridden getpid: its `original_getpid` is set by a
       sibling constructor whose order relative to this one is unspecified.
       Look up the real getpid directly. */
    pid_t (*real_getpid)(void) = dlsym(RTLD_NEXT, "getpid");
    starting_pid = real_getpid();
#endif
    if (starting_pid == 1)
        errx(EXIT_FAILURE, "fixture loaded as PID 1");

    work = getenv("WORK");
    if (!work)
        errx(EXIT_FAILURE, "fixture: WORK env var not set");

    /* Don't propagate the shim into helpers we exec. */
    unsetenv("LD_PRELOAD");
    unsetenv("DYLD_INSERT_LIBRARIES");
}

static int fixup_path(const char *input, char *output)
{
    if (input[0] != '/')
        errx(EXIT_FAILURE, "fixture: non-absolute path: \"%s\"", input);

    snprintf(output, PATH_MAX, "%s%s%s", work, remounted_root ? "/mnt" : "", input);
    return 0;
}

OVERRIDE(pid_t, getpid, (void))
{
    pid_t real_pid = ORIGINAL(getpid)();
    return (real_pid == starting_pid) ? 1 : real_pid;
}

#ifdef __APPLE__
REPLACE(int, mount, (const char *type, const char *dir, int flags, void *data))
{
    /* On macOS we get the args via compat.h's macro: source is in `type`,
       fstype is in `data`. */
    const char *fstype = (const char *)data;
    log("mount(\"%s\", \"%s\", \"%s\", %d, data)", type, dir, fstype, flags);
    if (strcmp(type, ".") == 0 && strcmp(dir, "/") == 0)
        remounted_root = true;
    return 0;
}

REPLACE(int, unmount, (const char *target, int flags))
{
    (void) flags;
    log("umount(\"%s\")", target);
    return 0;
}
#else
REPLACE(int, mount, (const char *source, const char *target,
                     const char *fstype, unsigned long flags,
                     const void *data))
{
    (void) data;
    log("mount(\"%s\", \"%s\", \"%s\", %lu, data)", source, target, fstype, flags);
    if (strcmp(source, ".") == 0 && strcmp(target, "/") == 0)
        remounted_root = true;
    return 0;
}

REPLACE(int, umount, (const char *target))
{
    log("umount(\"%s\")", target);
    return 0;
}
#endif

REPLACE(int, chroot, (const char *path))
{
    log("chroot(\"%s\")", path);
    return 0;
}

OVERRIDE(int, chdir, (const char *path))
{
    log("chdir(\"%s\")", path);
    char new_path[PATH_MAX];
    if (fixup_path(path, new_path) < 0)
        return -1;
    return ORIGINAL(chdir)(new_path);
}

OVERRIDE(int, open, (const char *pathname, int flags, ...))
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t) va_arg(ap, int);
        va_end(ap);
    }

    char new_path[PATH_MAX];
    if (fixup_path(pathname, new_path) < 0)
        return -1;
    return ORIGINAL(open)(new_path, flags, mode);
}

OVERRIDE(int, mkdir, (const char *path, mode_t mode))
{
    log("mkdir(\"%s\", %03o)", path, mode);
    char new_path[PATH_MAX];
    if (fixup_path(path, new_path) < 0)
        return -1;
    return ORIGINAL(mkdir)(new_path, mode);
}

REPLACE(int, rmdir, (const char *path))
{
    log("rmdir(\"%s\")", path);
    return 0;
}

OVERRIDE(int, unlinkat, (int fd, const char *path, int flag))
{
    if (flag == AT_REMOVEDIR) {
        log("unlinkat(\"%s\", AT_REMOVEDIR)", path);
    }
    /* Non-directory file removal is logged by the directory traversal that
       precedes it; readdir order isn't deterministic so logging individual
       file unlinks would make tests flaky. */
    return ORIGINAL(unlinkat)(fd, path, flag);
}

OVERRIDE(int, execv, (const char *file, char *const argv[]))
{
    log("execv(\"%s\")", file);
    char new_path[PATH_MAX];
    if (fixup_path(file, new_path) < 0)
        return -1;
    return ORIGINAL(execv)(new_path, argv);
}

OVERRIDE(int, execvp, (const char *file, char *const argv[]))
{
    log("execvp(\"%s\")", file);
    char new_path[PATH_MAX];
    if (fixup_path(file, new_path) < 0)
        return -1;
    return ORIGINAL(execvp)(new_path, argv);
}

static const char *dm_request_name(unsigned long request)
{
    switch (request) {
    case DM_DEV_CREATE:   return "DM_DEV_CREATE";
    case DM_TABLE_LOAD:   return "DM_TABLE_LOAD";
    case DM_DEV_SUSPEND:  return "DM_DEV_SUSPEND";
    default:              return "DM_UNKNOWN";
    }
}

static void log_dm_ioctl(unsigned long request, const struct dm_ioctl *dm)
{
    char extra[512] = {0};
    if (dm->target_count) {
        const struct dm_target_spec *target =
            (const struct dm_target_spec *)((const char *)dm + dm->data_start);
        const char *params = (const char *)(target + 1);
        snprintf(extra, sizeof(extra),
                 ", target=%llu,%llu,%s (%s)",
                 (unsigned long long)target->sector_start,
                 (unsigned long long)target->length,
                 target->target_type, params);
    }
    log("ioctl(%s, data_size=%u, data_start=%u, target_count=%u, "
        "flags=%u, dev=0x%llx, name=%s, uuid=%s%s)",
        dm_request_name(request),
        dm->data_size, dm->data_start, dm->target_count,
        dm->flags, (unsigned long long)dm->dev,
        dm->name, dm->uuid, extra);
}

OVERRIDE(int, ioctl, (int fd, unsigned long request, ...))
{
    va_list ap;
    va_start(ap, request);

    switch (request) {
    case DM_DEV_CREATE:
    case DM_TABLE_LOAD:
    case DM_DEV_SUSPEND: {
        const struct dm_ioctl *dm = va_arg(ap, const struct dm_ioctl *);
        va_end(ap);
        log_dm_ioctl(request, dm);
        return 0;
    }
    case BLKSSZGET: {
        /* Deliberately not 512. dm table lengths are in 512-byte sectors,
           so init must never size a mapping from this. */
        int *block_size = va_arg(ap, int *);
        va_end(ap);
        log("ioctl(BLKSSZGET) -> 4096");
        *block_size = 4096;
        return 0;
    }
    default: {
        /* Pass other ioctls through silently — mostly stdio terminal probes
           on macOS that we don't care about. */
        void *arg = va_arg(ap, void *);
        va_end(ap);
        return ORIGINAL(ioctl)(fd, request, arg);
    }
    }
}

REPLACE(unsigned int, sleep, (unsigned int seconds))
{
    if (seconds < 5) {
        log("sleep(%u)", seconds);
        return 0;
    }
    /* Long sleeps are probably real waits the harness wants to honour. */
    struct timespec ts = { .tv_sec = (time_t)seconds, .tv_nsec = 0 };
    nanosleep(&ts, NULL);
    return 0;
}

REPLACE(int, usleep, (useconds_t usec))
{
    /* device-wait poll loops use 100ms; skip them for fast tests. */
    if (usec < 1000000) {
        return 0;
    }
    log("usleep(%u)", usec);
    return 0;
}
