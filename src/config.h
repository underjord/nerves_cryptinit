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

#ifndef CONFIG_H
#define CONFIG_H

struct config {
    /* Owns the parsed config buffer. The const char * fields below point
       into it and are valid until config_free(). */
    char *buffer;

    /* Rootfs encryption (presence enables it). */
    const char *rootfs_key;        /* file:PATH | hex:BYTES | cmd:PATH ARGS */
    const char *rootfs_cipher;     /* default applied by dm.c */

    /* Data partition (presence of data_device enables it). */
    const char *data_device;
    const char *data_key;
    const char *data_cipher;

    /* Verity is mandatory; the root hash (the trust anchor) arrives on the
       signed kernel cmdline as verity_roothash=. The hash tree is always
       co-located on the rootfs device, after the filesystem, so that it is
       covered by dm-crypt when encryption is enabled. Its offset is derived
       at boot from the filesystem size (see fs.c), so there is no config
       knob for it. */

    /* How long to wait for block devices to appear. */
    const char *device_timeout;
};

/* Parse the config from a NUL-terminated string. The string is copied. */
int config_parse(const char *text, struct config *out);

/* Read a config file and parse it. A missing file is not an error — the
   resulting config is empty (which means "no encryption, no verity"). */
int config_parse_file(const char *path, struct config *out);

void config_free(struct config *cfg);

#endif
