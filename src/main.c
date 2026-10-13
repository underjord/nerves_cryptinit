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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __APPLE__
#include "compat/compat.h"
#endif

#include "cmdline.h"
#include "config.h"
#include "dm.h"
#include "fs.h"
#include "key.h"
#include "log.h"
#include "util.h"

#define DEVICE_WAIT_RETRY_MS 100
#define DEVICE_WAIT_DEFAULT_TIMEOUT_S 5

static void setup_initramfs(void)
{
    (void) mkdir("/dev", 0755);
    (void) mkdir("/sys", 0555);
    (void) mkdir("/proc", 0555);
    (void) mkdir("/mnt", 0755);

    OK_OR_WARN(mount("devtmpfs", "/dev", "devtmpfs",
                     MS_NOSUID | MS_NOEXEC, "mode=755,size=4m"),
               "mount /dev");
    OK_OR_WARN(mount("sysfs", "/sys", "sysfs",
                     MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL),
               "mount /sys");
    OK_OR_WARN(mount("proc", "/proc", "proc",
                     MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL),
               "mount /proc");
}

static int parse_timeout_seconds(const char *s, int default_value)
{
    if (!s || !*s)
        return default_value;

    char *end = NULL;
    long n = strtol(s, &end, 10);
    if (end == s || n < 0)
        return default_value;
    if (*end == 's' || *end == '\0')
        return (int)n;
    return default_value;
}

static void wait_for_device(const char *path, int timeout_s)
{
    int retries = (timeout_s * 1000) / DEVICE_WAIT_RETRY_MS;
    if (retries < 1) retries = 1;

    for (int i = 0; i < retries; i++) {
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            close(fd);
            return;
        }
        usleep(DEVICE_WAIT_RETRY_MS * 1000);
    }
    fatal("block device %s did not appear within %ds", path, timeout_s);
}

/* Files to keep when wiping the old initramfs. /dev is moved into the new
   root and /mnt is the new root itself; /sys and /proc get rmdir'd. */
static const char *root_skip[] = { ".", "..", "dev", "mnt", "proc", "sys", NULL };
static const char *child_skip[] = { ".", "..", NULL };

static int in_skip(const char *name, const char **skip)
{
    while (*skip) {
        if (strcmp(*skip, name) == 0)
            return 1;
        skip++;
    }
    return 0;
}

static void cleanup_dir(int dirfd, const char **skip)
{
    DIR *dir = fdopendir(dirfd);
    if (!dir)
        return;

    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        if (in_skip(e->d_name, skip))
            continue;

        if (e->d_type == DT_DIR) {
            int sub = openat(dirfd, e->d_name, O_RDONLY);
            if (sub >= 0)
                cleanup_dir(sub, child_skip);
            (void) unlinkat(dirfd, e->d_name, AT_REMOVEDIR);
        } else {
            (void) unlinkat(dirfd, e->d_name, 0);
        }
    }
    closedir(dir);
}

static void switch_root(void)
{
    OK_OR_WARN(mount("/dev", "/mnt/dev", NULL, MS_MOVE, NULL),
               "moving /dev to /mnt/dev");
    (void) umount("/sys");
    (void) umount("/proc");

    int rootfd = open("/", O_RDONLY | O_CLOEXEC);
    if (rootfd >= 0) {
        cleanup_dir(rootfd, root_skip);
        (void) rmdir("/sys");
        (void) rmdir("/proc");
    }

    OK_OR_WARN(chdir("/mnt"), "chdir /mnt");
    OK_OR_WARN(mount(".", "/", NULL, MS_MOVE, NULL), "mount-move new root");
    OK_OR_WARN(chroot("."), "chroot");
}

int main(int argc, char *argv[])
{
    (void) argc;

    log_init();
    info("v" PROGRAM_VERSION_STR " starting");

    setup_initramfs();

    struct cmdline cmd;
    if (cmdline_parse_file("/proc/cmdline", &cmd) < 0)
        fatal("could not parse /proc/cmdline");

    if (!cmd.root)
        fatal("no root= on kernel cmdline");

    struct config cfg;
    if (config_parse_file("/etc/initramfs.conf", &cfg) < 0)
        fatal("could not parse /etc/initramfs.conf");

    int timeout_s = parse_timeout_seconds(cfg.device_timeout, DEVICE_WAIT_DEFAULT_TIMEOUT_S);
    wait_for_device(cmd.root, timeout_s);

    const char *mount_source = cmd.root;
    char dm_path[32];

    int dm_devices = 0;

    if (cfg.rootfs_key) {
        char secret_hex[KEY_HEX_MAX + 1];
        OK_OR_FATAL(key_resolve(cfg.rootfs_key, secret_hex, sizeof(secret_hex)),
                    "could not resolve rootfs_key");
        dm_setup_crypt("rootfs", cmd.root, cfg.rootfs_cipher, secret_hex, 0);
        dm_device_path(dm_devices, dm_path, sizeof(dm_path));
        mount_source = dm_path;
        dm_devices++;
    }

    /* dm-verity is mandatory and layers on top of whatever we'll mount:
       when the rootfs is also encrypted, verity reads through the crypt
       device (flash → decrypt → verify → squashfs), so the hash tree —
       co-located on the same device — is encrypted too. The root hash is
       the signed trust anchor from the cmdline; everything else comes from
       the on-disk superblock (see dm.c). */
    if (!cmd.verity_roothash)
        fatal("no verity_roothash= on kernel cmdline");

    /* The hash tree is appended right after the filesystem, so its offset is
       the rootfs image size rounded up to the verity block alignment. Rather
       than carry that in the config, recover the size from the filesystem
       superblock on whatever layer sits below verity (the raw partition, or
       the dm-crypt device when encryption is enabled). */
    uint64_t hash_offset;
    OK_OR_FATAL(fs_verity_hash_offset(mount_source, &hash_offset),
                "could not derive verity hash offset from %s", mount_source);
    dm_setup_verity("vroot", mount_source, hash_offset, cmd.verity_roothash);
    dm_device_path(dm_devices, dm_path, sizeof(dm_path));
    mount_source = dm_path;
    dm_devices++;

    const char *fstype = cmd.rootfstype ? cmd.rootfstype : "squashfs";
    unsigned long mflags = cmd.read_only ? MS_RDONLY : 0;

    OK_OR_FATAL(mount(mount_source, "/mnt", fstype, mflags, cmd.rootflags),
                "mount %s on /mnt", mount_source);

    switch_root();

    if (cfg.data_device) {
        if (!cfg.data_key)
            fatal("data_device set without data_key");

        wait_for_device(cfg.data_device, timeout_s);

        char data_secret[KEY_HEX_MAX + 1];
        OK_OR_FATAL(key_resolve(cfg.data_key, data_secret, sizeof(data_secret)),
                    "could not resolve data_key");
        dm_setup_crypt("data", cfg.data_device, cfg.data_cipher, data_secret, 0);
    }

    const char *next_init = cmd.init ? cmd.init : "/sbin/init";
    if (argv[0])
        argv[0] = (char *)next_init;
    execv(next_init, argv);

    fatal("could not exec %s: %s", next_init, strerror(errno));
}
