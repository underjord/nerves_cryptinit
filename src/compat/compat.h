/*
 * Copyright 2026 Frank Hunleth
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * Host-build compatibility shims so that this code compiles on macOS for
 * unit tests and the LD_PRELOAD test fixture. None of this is used on the
 * Linux target.
 */

#ifndef COMPAT_H
#define COMPAT_H

#ifdef __APPLE__

#include <sys/mount.h>

/* Linux mount flag values, defined here so both this code and the test
   fixture see the same numeric values regardless of host. */
#define MS_RDONLY        1
#define MS_NOSUID        2
#define MS_NODEV         4
#define MS_NOEXEC        8
#define MS_SYNCHRONOUS  16
#define MS_REMOUNT      32
#define MS_NOATIME    1024
#define MS_NODIRATIME 2048
#define MS_BIND       4096
#define MS_MOVE       8192

/* Bridge Linux's 5-arg mount(source, target, fstype, flags, data) to
   macOS's BSD 4-arg mount(type, dir, flags, data). The Linux "data"
   argument is dropped (the test fixture doesn't use it); source is passed
   through BSD's type slot and fstype rides in the data slot, which lets
   the fixture log all four meaningful values from a single call. */
#define mount(source, target, fstype, flags, data) \
    mount((const char *)(source), (target), (int)(flags), (void *)(uintptr_t)(fstype))
#define umount(target) unmount((target), 0)

#endif /* __APPLE__ */

#endif
